`As the light travels from a point $p$ to a point $q$ in the atmosphere,
it is partially absorbed and scattered out of its initial direction because of
the air molecules and the aerosol particles. Thus, the light arriving at $q$
is only a fraction of the light from $p$, and this fraction, which depends on
wavelength, is called the
<a href="https://en.wikipedia.org/wiki/Transmittance">transmittance</a>. The
following sections describe how we compute it, how we store it in a precomputed
texture, and how we read it back.



For 3 aligned points $p$, $q$ and $r$ inside the atmosphere, in this
order, the transmittance between $p$ and $r$ is the product of the
transmittance between $p$ and $q$ and between $q$ and $r$. In
particular, the transmittance between $p$ and $q$ is the transmittance
between $p$ and the nearest intersection $i$ of the half-line $[p,q)$
with the top or bottom atmosphere boundary, divided by the transmittance between
$q$ and $i$ (or 0 if the segment $[p,q]$ intersects the ground):



Also, the transmittance between $p$ and $q$ and between $q$ and $p$
are the same. Thus, to compute the transmittance between arbitrary points, it
is sufficient to know the transmittance between a point $p$ in the atmosphere,
and points $i$ on the top atmosphere boundary. This transmittance depends on
only two parameters, which can be taken as the radius $r=\Vert op\Vert$ and
the cosine of the "view zenith angle",
$\mu=op\cdot pi/\Vert op\Vert\Vert pi\Vert$. To compute it, we
first need to compute the length $\Vert pi\Vert$, and we need to know when
the segment $[p,i]$ intersects the ground



A point at distance $d$ from $p$ along $[p,i)$ has coordinates
$[d\sqrt{1-\mu^2}, r+d\mu]^\top$, whose squared norm is $d^2+2r\mu d+r^2$.
Thus, by definition of $i$, we have
$\Vert pi\Vert^2+2r\mu\Vert pi\Vert+r^2=r_{\mathrm{top}}^2$,
from which we deduce the length $\Vert pi\Vert$:



We can now compute the transmittance between $p$ and $i$. From its
definition and the
<a href="https://en.wikipedia.org/wiki/Beer-Lambert_law">Beer-Lambert law</a>,
this involves the integral of the number density of air molecules along the
segment $[p,i]$, as well as the integral of the number density of aerosols
and the integral of the number density of air molecules that absorb light
(e.g. ozone) - along the same segment. These 3 integrals have the same form and,
when the segment $[p,i]$ does not intersect the ground, they can be computed
numerically with the help of the following auxilliary function (using the <a
href="https://en.wikipedia.org/wiki/Trapezoidal_rule">trapezoidal rule</a>):



```c++
Number GetLayerDensity(IN(DensityProfileLayer) layer, Length altitude) {
  Number density = layer.exp_term * exp(layer.exp_scale * altitude) +
      layer.linear_term * altitude + layer.constant_term;
  return clamp(density, Number(0.0), Number(1.0));
}

Number GetProfileDensity(IN(DensityProfile) profile, Length altitude) {
  return altitude < profile.layers[0].width ?
      GetLayerDensity(profile.layers[0], altitude) :
      GetLayerDensity(profile.layers[1], altitude);
}
```



```c++
Length ComputeOpticalLengthToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere, IN(DensityProfile) profile,
    Length r, Number mu) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  // Number of intervals for the numerical integration.
  const int SAMPLE_COUNT = 500;
  // The integration step, i.e. the length of each integration interval.
  Length dx =
      DistanceToTopAtmosphereBoundary(atmosphere, r, mu) / Number(SAMPLE_COUNT);
  // Integration loop.
  Length result = 0.0 * m;
  for (int i = 0; i <= SAMPLE_COUNT; ++i) {
    Length d_i = Number(i) * dx;
    // Distance between the current sample point and the planet center.
    Length r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
    // Number density at the current sample point (divided by the number density
    // at the bottom of the atmosphere, yielding a dimensionless number).
    Number y_i = GetProfileDensity(profile, r_i - atmosphere.bottom_radius);
    // Sample weight (from the trapezoidal rule).
    Number weight_i = i == 0 || i == SAMPLE_COUNT ? 0.5 : 1.0;
    result += y_i * weight_i * dx;
  }
  return result;
}
```



With this function the transmittance between $p$ and $i$ is now easy to
compute (we continue to assume that the segment does not intersect the ground):

```c++
DimensionlessSpectrum ComputeTransmittanceToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere, Length r, Number mu) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  return exp(-(
      atmosphere.rayleigh_scattering *
          ComputeOpticalLengthToTopAtmosphereBoundary(
              atmosphere, atmosphere.rayleigh_density, r, mu) +
      atmosphere.mie_extinction *
          ComputeOpticalLengthToTopAtmosphereBoundary(
              atmosphere, atmosphere.mie_density, r, mu) +
      atmosphere.absorption_extinction *
          ComputeOpticalLengthToTopAtmosphereBoundary(
              atmosphere, atmosphere.absorption_density, r, mu)));
}
```



###Precomputation

The above function is quite costly to evaluate, and a lot of evaluations are
needed to compute single and multiple scattering. Fortunately this function
depends on only two parameters and is quite smooth, so we can precompute it in a
small 2D texture to optimize its evaluation



For this we need a mapping between the function parameters $(r,\mu)$ and the
texture coordinates $(u,v)$, and vice-versa, because these parameters do not
have the same units and range of values. And even if it was the case, storing a
function $f$ from the $[0,1]$ interval in a texture of size $n$ would sample the
function at $0.5/n$, $1.5/n$, ... $(n-0.5)/n$, because texture samples are at
the center of texels. Therefore, this texture would only give us extrapolated
function values at the domain boundaries ($0$ and $1$). To avoid this we need
to store $f(0)$ at the center of texel 0 and $f(1)$ at the center of texel
$n-1$. This can be done with the following mapping from values $x$ in $[0,1]$ to
texture coordinates $u$ in $[0.5/n,1-0.5/n]$ - and its inverse:



```c++
Number GetTextureCoordFromUnitRange(Number x, int texture_size) {
  return 0.5 / Number(texture_size) + x * (1.0 - 1.0 / Number(texture_size));
}

Number GetUnitRangeFromTextureCoord(Number u, int texture_size) {
  return (u - 0.5 / Number(texture_size)) / (1.0 - 1.0 / Number(texture_size));
}
```



Using these functions, we can now define a mapping between $(r,\mu)$ and the
texture coordinates $(u,v)$, and its inverse, which avoid any extrapolation
during texture lookups. In the <a href=
"http://evasion.inrialpes.fr/~Eric.Bruneton/PrecomputedAtmosphericScattering2.zip"

>original implementation</a> this mapping was using some ad-hoc constants chosen
>for the Earth atmosphere case. Here we use a generic mapping, working for any
>atmosphere, but still providing an increased sampling rate near the horizon.
>Our improved mapping is based on the parameterization described in our
><a href="https://hal.inria.fr/inria-00288758/en">paper</a> for the 4D textures:
>we use the same mapping for $r$, and a slightly improved mapping for $\mu$
>(considering only the case where the view ray does not intersect the ground).
>More precisely, we map $\mu$ to a value $x_{\mu}$ between 0 and 1 by considering
>the distance $d$ to the top atmosphere boundary, compared to its minimum and
>maximum values $d_{\mathrm{min}}=r_{\mathrm{top}}-r$ and
>$d_{\mathrm{max}}=\rho+H$ (cf. the notations from the
><a href="https://hal.inria.fr/inria-00288758/en">paper</a> and the figure
>below):





With these definitions, the mapping from $(r,\mu)$ to the texture coordinates
$(u,v)$ can be implemented as follows:

```c++
vec2 GetTransmittanceTextureUvFromRMu(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
      atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the horizon.
  Length rho =
      SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
  // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon).
  Length d = DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
  Length d_min = atmosphere.top_radius - r;
  Length d_max = rho + H;
  Number x_mu = (d - d_min) / (d_max - d_min);
  Number x_r = rho / H;
  return vec2(GetTextureCoordFromUnitRange(x_mu, TRANSMITTANCE_TEXTURE_WIDTH),
              GetTextureCoordFromUnitRange(x_r, TRANSMITTANCE_TEXTURE_HEIGHT));
}
```



and the inverse mapping follows immediately:

```c++
void GetRMuFromTransmittanceTextureUv(IN(AtmosphereParameters) atmosphere,
    IN(vec2) uv, OUT(Length) r, OUT(Number) mu) {
  assert(uv.x >= 0.0 && uv.x <= 1.0);
  assert(uv.y >= 0.0 && uv.y <= 1.0);
  Number x_mu = GetUnitRangeFromTextureCoord(uv.x, TRANSMITTANCE_TEXTURE_WIDTH);
  Number x_r = GetUnitRangeFromTextureCoord(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT);
  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
      atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the horizon, from which we can compute r:
  Length rho = H * x_r;
  r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
  // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
  // from which we can recover mu:
  Length d_min = atmosphere.top_radius - r;
  Length d_max = rho + H;
  Length d = d_min + x_mu * (d_max - d_min);
  mu = d == 0.0 * m ? Number(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
  mu = ClampCosine(mu);
}
```



It is now easy to define a fragment shader function to precompute a texel of
the transmittance texture:

```c++
DimensionlessSpectrum ComputeTransmittanceToTopAtmosphereBoundaryTexture(
    IN(AtmosphereParameters) atmosphere, IN(vec2) frag_coord) {
  const vec2 TRANSMITTANCE_TEXTURE_SIZE =
      vec2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);
  Length r;
  Number mu;
  GetRMuFromTransmittanceTextureUv(
      atmosphere, frag_coord / TRANSMITTANCE_TEXTURE_SIZE, r, mu);
  return ComputeTransmittanceToTopAtmosphereBoundary(atmosphere, r, mu);
}
```



### Lookup

With the help of the above precomputed texture, we can now get the
transmittance between a point and the top atmosphere boundary with a single
texture lookup (assuming there is no intersection with the ground):



```c++
DimensionlessSpectrum GetTransmittanceToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  vec2 uv = GetTransmittanceTextureUvFromRMu(atmosphere, r, mu);
  return DimensionlessSpectrum(texture(transmittance_texture, uv));
}
```



Also, with $r_d=\Vert oq\Vert=\sqrt{d^2+2r\mu d+r^2}$ and $\mu_d=
oq\cdot pi/\Vert oq\Vert \Vert pi\Vert=(r\mu+d)/r_d$ the values of
$r$ and $\mu$ at $q$, we can get the transmittance between two arbitrary
points $p$ and $q$ inside the atmosphere with only two texture lookups
(recall that the transmittance between $p$ and $q$ is the transmittance
between $p$ and the top atmosphere boundary, divided by the transmittance
between $q$ and the top atmosphere boundary, or the reverse - we continue to
assume that the segment between the two points does not intersect the ground):



```glsl
DimensionlessSpectrum GetTransmittance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Length d, bool ray_r_mu_intersects_ground) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  assert(d >= 0.0 * m);

  Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
  Number mu_d = ClampCosine((r * mu + d) / r_d);

  if (ray_r_mu_intersects_ground) {
    return min(
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r_d, -mu_d) /
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, -mu),
        DimensionlessSpectrum(1.0));
  } else {
    return min(
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, mu) /
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r_d, mu_d),
        DimensionlessSpectrum(1.0));
  }
}
```



where <code>ray_r_mu_intersects_ground</code> should be true if the ray
defined by $r$ and $\mu$ intersects the ground. We don't compute it here with
<code>RayIntersectsGround</code> because the result could be wrong for rays
very close to the horizon, due to the finite precision and rounding errors of
floating point operations. And also because the caller generally has more robust
ways to know whether a ray intersects the ground or not (see below).

Finally, we will also need the transmittance between a point in the
atmosphere and the Sun. The Sun is not a point light source, so this is an
integral of the transmittance over the Sun disc. Here we consider that the
transmittance is constant over this disc, except below the horizon, where the
transmittance is 0. As a consequence, the transmittance to the Sun can be
computed with <code>GetTransmittanceToTopAtmosphereBoundary</code>, times the
fraction of the Sun disc which is above the horizon.

This fraction varies from 0 when the Sun zenith angle $\theta_s$ is larger than the horizon zenith angle $\theta_h$ plus the Sun angular radius $\alpha_s$,to 1 when $\theta_s$ is smaller than $\theta_h-\alpha_s$. Equivalently, itvaries from 0 when $\mu_s=\cos\theta_s$ is smaller than
$\cos(\theta_h+\alpha_s)\approx\cos\theta_h-\alpha_s\sin\theta_h$ to 1 when$\mu_s$ is larger than$\cos(\theta_h-\alpha_s)\approx\cos\theta_h+\alpha_s\sin\theta_h$. In between, the visible Sun disc fraction varies approximately like a smoothstep (this can be verified by plotting the area of <a href="https://en.wikipedia.org/wiki/Circular_segment">circular segment</a> as a function of its <a href="https://en.wikipedia.org/wiki/Sagitta_(geometry)"sagitta</a>). Therefore, since $\sin\theta_h=r_{\mathrm{bottom}}/r$, we can approximate the transmittance to the Sun with the following function:

```glsl
DimensionlessSpectrum GetTransmittanceToSun(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu_s) {
  Number sin_theta_h = atmosphere.bottom_radius / r;
  Number cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
  return GetTransmittanceToTopAtmosphereBoundary(
          atmosphere, transmittance_texture, r, mu_s) *
      smoothstep(-sin_theta_h * atmosphere.sun_angular_radius / rad,
                 sin_theta_h * atmosphere.sun_angular_radius / rad,
                 mu_s - cos_theta_h);
}
```



###Single scattering

The single scattered radiance is the light arriving from the Sun at some
point after exactly one scattering event inside the atmosphere (which can be due
to air molecules or aerosol particles; we exclude reflections from the ground,
computed separately). The following sections describe
how we compute it, how we store it in a precomputed texture, and how we read it
back.

####Computation

Consider the Sun light scattered at a point $q$ by air molecules before
arriving at another point $p$ (for aerosols, replace "Rayleigh" with "Mie"
below):

The radiance arriving at $p$ is the product of:

* the solar irradiance at the top of the atmosphere,
* the transmittance between the Sun and $q$ (i.e. the fraction of the Sunlight at the top of the atmosphere that reaches $q$),

* the Rayleigh scattering coefficient at $q$ (i.e. the fraction of the light arriving at $q$ which is scattered, in any direction),
* the Rayleigh phase function (i.e. the fraction of the scattered light at $q$ which is actually scattered towards $p$),
* the transmittance between $q$ and $p$ (i.e. the fraction of the light scattered at $q$ towards $p$ that reaches $p$).



Thus, by noting $w_s$ the unit direction vector towards the Sun, and with
the following definitions:

$r=\Vert op\Vert$,
$d=\Vert pq\Vert$,
$\mu=(op\cdot pq)/rd$,
$\mu_s=(op\cdot w_s)/r$,
$\nu=(pq\cdot w_s)/d$

the values of $r$ and $\mu_s$ for $q$ are

$r_d=\Vert oq\Vert=\sqrt{d^2+2r\mu d +r^2}$,
$\mu_{s,d}=(oq\cdot w_s)/r_d=((op+pq)\cdot w_s)/r_d=(r\mu_s + d\nu)/r_d$

and the Rayleigh and Mie single scattering components can be computed as follows
(note that we omit the solar irradiance and the phase function terms, as well as
the scattering coefficients at the bottom of the atmosphere - we add them later
on for efficiency reasons):

```glsl
void ComputeSingleScatteringIntegrand(f
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Number mu_s, Number nu, Length d,
    bool ray_r_mu_intersects_ground,
    OUT(DimensionlessSpectrum) rayleigh, OUT(DimensionlessSpectrum) mie) {
  Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
  Number mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);
  DimensionlessSpectrum transmittance =
      GetTransmittance(
          atmosphere, transmittance_texture, r, mu, d,
          ray_r_mu_intersects_ground) *
      GetTransmittanceToSun(
          atmosphere, transmittance_texture, r_d, mu_s_d);
  rayleigh = transmittance * GetProfileDensity(
      atmosphere.rayleigh_density, r_d - atmosphere.bottom_radius);
  mie = transmittance * GetProfileDensity(
      atmosphere.mie_density, r_d - atmosphere.bottom_radius);
}
```



Consider now the Sun light arriving at $p$ from a given direction $w$,
after exactly one scattering event. The scattering event can occur at any point
$q$ between $p$ and the intersection $i$ of the half-line $[p,w)$ with
the nearest atmosphere boundary. Thus, the single scattered radiance at $p$
from direction $w$ is the integral of the single scattered radiance from $q$
to $p$ for all points $q$ between $p$ and $i$. To compute it, we first
need the length $\Vert pi\Vert$:

```glsl
Length DistanceToNearestAtmosphereBoundary(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu, bool ray_r_mu_intersects_ground) {
  if (ray_r_mu_intersects_ground) {
    return DistanceToBottomAtmosphereBoundary(atmosphere, r, mu);
  } else {
    return DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
  }
}
```

The single scattering integral can then be computed as follows (using
the <a href="https://en.wikipedia.org/wiki/Trapezoidal_rule">trapezoidal
rule</a>):



```glsl
void ComputeSingleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    OUT(IrradianceSpectrum) rayleigh, OUT(IrradianceSpectrum) mie) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  assert(nu >= -1.0 && nu <= 1.0);

  // Number of intervals for the numerical integration.
  const int SAMPLE_COUNT = 50;
  // The integration step, i.e. the length of each integration interval.
  Length dx =
      DistanceToNearestAtmosphereBoundary(atmosphere, r, mu,
          ray_r_mu_intersects_ground) / Number(SAMPLE_COUNT);
  // Integration loop.
  DimensionlessSpectrum rayleigh_sum = DimensionlessSpectrum(0.0);
  DimensionlessSpectrum mie_sum = DimensionlessSpectrum(0.0);
  for (int i = 0; i <= SAMPLE_COUNT; ++i) {
    Length d_i = Number(i) * dx;
    // The Rayleigh and Mie single scattering at the current sample point.
    DimensionlessSpectrum rayleigh_i;
    DimensionlessSpectrum mie_i;
    ComputeSingleScatteringIntegrand(atmosphere, transmittance_texture,
        r, mu, mu_s, nu, d_i, ray_r_mu_intersects_ground, rayleigh_i, mie_i);
    // Sample weight (from the trapezoidal rule).
    Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
    rayleigh_sum += rayleigh_i * weight_i;
    mie_sum += mie_i * weight_i;
  }
  rayleigh = rayleigh_sum * dx * atmosphere.solar_irradiance *
      atmosphere.rayleigh_scattering;
  mie = mie_sum * dx * atmosphere.solar_irradiance * atmosphere.mie_scattering;
}
```



Note that we added the solar irradiance and the scattering coefficient terms
that we omitted in <code>ComputeSingleScatteringIntegrand</code>, but not the
phase function terms - they are added at <a href="#rendering">render time</a>
for better angular precision. We provide them here for completeness:

```hlsl
InverseSolidAngle RayleighPhaseFunction(Number nu) {
  InverseSolidAngle k = 3.0 / (16.0 * PI * sr);
  return k * (1.0 + nu * nu);
}

InverseSolidAngle MiePhaseFunction(Number g, Number nu) {
  InverseSolidAngle k = 3.0 / (8.0 * PI * sr) * (1.0 - g * g) / (2.0 + g * g);
  return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, 1.5);
}
```



####Precomputation

The <code>ComputeSingleScattering</code> function is quite costly to
evaluate, and a lot of evaluations are needed to compute multiple scattering.
We therefore want to precompute it in a texture, which requires a mapping from
the 4 function parameters to texture coordinates. Assuming for now that we have
4D textures, we need to define a mapping from $(r,\mu,\mu_s,\nu)$ to texture
coordinates $(u,v,w,z)$. The function below implements the mapping defined in
our <a href="https://hal.inria.fr/inria-00288758/en">paper</a>, with some small improvements (refer to the paper and to the above figures for the notations):

1. the mapping for $\mu$ takes the minimal distance to the nearest atmosphere
   boundary into account, to map $\mu$ to the full $[0,1]$ interval (the original
   mapping was not covering the full $[0,1]$ interval).
2. the mapping for $\mu_s$ is more generic than in the paper (the original
   mapping was using ad-hoc constants chosen for the Earth atmosphere case). It is
   based on the distance to the top atmosphere boundary (for the sun rays), as for
   the $\mu$ mapping, and uses only one ad-hoc (but configurable) parameter. Yet,
   as the original definition, it provides an increased sampling rate near the
   horizon.



```glsl
vec4 GetScatteringTextureUvwzFromRMuMuSNu(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  assert(nu >= -1.0 && nu <= 1.0);

  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
      atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the horizon.
  Length rho =
      SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
  Number u_r = GetTextureCoordFromUnitRange(rho / H, SCATTERING_TEXTURE_R_SIZE);

  // Discriminant of the quadratic equation for the intersections of the ray
  // (r,mu) with the ground (see RayIntersectsGround).
  Length r_mu = r * mu;
  Area discriminant =
      r_mu * r_mu - r * r + atmosphere.bottom_radius * atmosphere.bottom_radius;
  Number u_mu;
  if (ray_r_mu_intersects_ground) {
    // Distance to the ground for the ray (r,mu), and its minimum and maximum
    // values over all mu - obtained for (r,-1) and (r,mu_horizon).
    Length d = -r_mu - SafeSqrt(discriminant);
    Length d_min = r - atmosphere.bottom_radius;
    Length d_max = rho;
    u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(d_max == d_min ? 0.0 :
        (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
  } else {
    // Distance to the top atmosphere boundary for the ray (r,mu), and its
    // minimum and maximum values over all mu - obtained for (r,1) and
    // (r,mu_horizon).
    Length d = -r_mu + SafeSqrt(discriminant + H * H);
    Length d_min = atmosphere.top_radius - r;
    Length d_max = rho + H;
    u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange(
        (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
  }

  Length d = DistanceToTopAtmosphereBoundary(
      atmosphere, atmosphere.bottom_radius, mu_s);
  Length d_min = atmosphere.top_radius - atmosphere.bottom_radius;
  Length d_max = H;
  Number a = (d - d_min) / (d_max - d_min);
  Number A =
      -2.0 * atmosphere.mu_s_min * atmosphere.bottom_radius / (d_max - d_min);
  Number u_mu_s = GetTextureCoordFromUnitRange(
      max(1.0 - a / A, 0.0) / (1.0 + a), SCATTERING_TEXTURE_MU_S_SIZE);

  Number u_nu = (nu + 1.0) / 2.0;
  return vec4(u_nu, u_mu_s, u_mu, u_r);
```



The inverse mapping follows immediately:



```glsl
void GetRMuMuSNuFromScatteringTextureUvwz(IN(AtmosphereParameters) atmosphere,
    IN(vec4) uvwz, OUT(Length) r, OUT(Number) mu, OUT(Number) mu_s,
    OUT(Number) nu, OUT(bool) ray_r_mu_intersects_ground) {
  assert(uvwz.x >= 0.0 && uvwz.x <= 1.0);
  assert(uvwz.y >= 0.0 && uvwz.y <= 1.0);
  assert(uvwz.z >= 0.0 && uvwz.z <= 1.0);
  assert(uvwz.w >= 0.0 && uvwz.w <= 1.0);

  // Distance to top atmosphere boundary for a horizontal ray at ground level.
  Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
      atmosphere.bottom_radius * atmosphere.bottom_radius);
  // Distance to the horizon.
  Length rho =
      H * GetUnitRangeFromTextureCoord(uvwz.w, SCATTERING_TEXTURE_R_SIZE);
  r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

  if (uvwz.z < 0.5) {
    // Distance to the ground for the ray (r,mu), and its minimum and maximum
    // values over all mu - obtained for (r,-1) and (r,mu_horizon) - from which
    // we can recover mu:
    Length d_min = r - atmosphere.bottom_radius;
    Length d_max = rho;
    Length d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
        1.0 - 2.0 * uvwz.z, SCATTERING_TEXTURE_MU_SIZE / 2);
    mu = d == 0.0 * m ? Number(-1.0) :
        ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
    ray_r_mu_intersects_ground = true;
  } else {
    // Distance to the top atmosphere boundary for the ray (r,mu), and its
    // minimum and maximum values over all mu - obtained for (r,1) and
    // (r,mu_horizon) - from which we can recover mu:
    Length d_min = atmosphere.top_radius - r;
    Length d_max = rho + H;
    Length d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
        2.0 * uvwz.z - 1.0, SCATTERING_TEXTURE_MU_SIZE / 2);
    mu = d == 0.0 * m ? Number(1.0) :
        ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
    ray_r_mu_intersects_ground = false;
  }

  Number x_mu_s =
      GetUnitRangeFromTextureCoord(uvwz.y, SCATTERING_TEXTURE_MU_S_SIZE);
  Length d_min = atmosphere.top_radius - atmosphere.bottom_radius;
  Length d_max = H;
  Number A =
      -2.0 * atmosphere.mu_s_min * atmosphere.bottom_radius / (d_max - d_min);
  Number a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
  Length d = d_min + min(a, A) * (d_max - d_min);
  mu_s = d == 0.0 * m ? Number(1.0) :
     ClampCosine((H * H - d * d) / (2.0 * atmosphere.bottom_radius * d));

  nu = ClampCosine(uvwz.x * 2.0 - 1.0);
}
```



We assumed above that we have 4D textures, which is not the case in practice.
We therefore need a further mapping, between 3D and 4D texture coordinates. The
function below expands a 3D texel coordinate into a 4D texture coordinate, and
then to $(r,\mu,\mu_s,\nu)$ parameters. It does so by "unpacking" two texel
coordinates from the $x$ texel coordinate. Note also how we clamp the $\nu$
parameter at the end. This is because $\nu$ is not a fully independent variable:
its range of values depends on $\mu$ and $\mu_s$ (this can be seen by computing
$\mu$, $\mu_s$ and $\nu$ from the cartesian coordinates of the zenith, view and
sun unit direction vectors), and the previous functions implicitely assume this
(their assertions can break if this constraint is not respected).



```glsl
void GetRMuMuSNuFromScatteringTextureFragCoord(
    IN(AtmosphereParameters) atmosphere, IN(vec3) frag_coord,
    OUT(Length) r, OUT(Number) mu, OUT(Number) mu_s, OUT(Number) nu,
    OUT(bool) ray_r_mu_intersects_ground) {
  const vec4 SCATTERING_TEXTURE_SIZE = vec4(
      SCATTERING_TEXTURE_NU_SIZE - 1,
      SCATTERING_TEXTURE_MU_S_SIZE,
      SCATTERING_TEXTURE_MU_SIZE,
      SCATTERING_TEXTURE_R_SIZE);
  Number frag_coord_nu =
      floor(frag_coord.x / Number(SCATTERING_TEXTURE_MU_S_SIZE));
  Number frag_coord_mu_s =
      mod(frag_coord.x, Number(SCATTERING_TEXTURE_MU_S_SIZE));
  vec4 uvwz =
      vec4(frag_coord_nu, frag_coord_mu_s, frag_coord.y, frag_coord.z) /
          SCATTERING_TEXTURE_SIZE;
  GetRMuMuSNuFromScatteringTextureUvwz(
      atmosphere, uvwz, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  // Clamp nu to its valid range of values, given mu and mu_s.
  nu = clamp(nu, mu * mu_s - sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)),
      mu * mu_s + sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)));
}
```



With this mapping, we can finally write a function to precompute a texel of
the single scattering in a 3D texture:

```glsl
void ComputeSingleScatteringTexture(IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture, IN(vec3) frag_coord,
    OUT(IrradianceSpectrum) rayleigh, OUT(IrradianceSpectrum) mie) {
  Length r;
  Number mu;
  Number mu_s;
  Number nu;
  bool ray_r_mu_intersects_ground;
  GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
      r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  ComputeSingleScattering(atmosphere, transmittance_texture,
      r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh, mie);
}
```



####Lookup

With the help of the above precomputed texture, we can now get the scattering
between a point and the nearest atmosphere boundary with two texture lookups (we
need two 3D texture lookups to emulate a single 4D texture lookup with
quadrilinear interpolation; the 3D texture coordinates are computed using the
inverse of the 3D-4D mapping defined in
<code>GetRMuMuSNuFromScatteringTextureFragCoord</code>):

```glsl
TEMPLATE(AbstractSpectrum)
AbstractSpectrum GetScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(AbstractScatteringTexture TEMPLATE_ARGUMENT(AbstractSpectrum))
        scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) {
  vec4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
      atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  Number tex_coord_x = uvwz.x * Number(SCATTERING_TEXTURE_NU_SIZE - 1);
  Number tex_x = floor(tex_coord_x);
  Number lerp = tex_coord_x - tex_x;
  vec3 uvw0 = vec3((tex_x + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
      uvwz.z, uvwz.w);
  vec3 uvw1 = vec3((tex_x + 1.0 + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
      uvwz.z, uvwz.w);
  return AbstractSpectrum(texture(scattering_texture, uvw0) * (1.0 - lerp) +
      texture(scattering_texture, uvw1) * lerp);
}
```

Finally, we provide here a convenience lookup function which will be useful
in the next section. This function returns either the single scattering, with
the phase functions included, or the $n$-th order of scattering, with $n>1$. It
assumes that, if <code>scattering_order</code> is strictly greater than 1, then
<code>multiple_scattering_texture</code> corresponds to this scattering order,
with both Rayleigh and Mie included, as well as all the phase function terms.

```glsl
RadianceSpectrum GetScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    int scattering_order) {
  if (scattering_order == 1) {
    IrradianceSpectrum rayleigh = GetScattering(
        atmosphere, single_rayleigh_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
    IrradianceSpectrum mie = GetScattering(
        atmosphere, single_mie_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
    return rayleigh * RayleighPhaseFunction(nu) +
        mie * MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
  } else {
    return GetScattering(
        atmosphere, multiple_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
  }
}
```



### Multiple scattering

The multiply scattered radiance is the light arriving from the Sun at some point in the atmosphere after two or more <i>bounces</i> (where a bounce is either a scattering event or a reflection from the ground). The following sections describe how we compute it, how we store it in a precomputed texture, and how we read it back.

Note that, as for single scattering, we exclude here the light paths whose <i>last</i> bounce is a reflection on the ground. The contribution from these paths is computed separately, at rendering time, in order to take the actual ground albedo into account (for intermediate reflections on the ground, which are precomputed, we use an average, uniform albedo).

#### Computation

Multiple scattering can be decomposed into the sum of double scattering, triple scattering, etc, where each term corresponds to the light arriving from the Sun at some point in the atmosphere after <i>exactly</i> 2, 3, etc bounces. Moreover, each term can be computed from the previous one. Indeed, the light arriving at some point $p$ from direction $w$ after $n$ bounces is an integral over all the possible points $q$ for the last bounce, which involves the light arriving at $q$ from any direction, after $n-1$ bounces.

This description shows that each scattering order requires a triple integral to be computed from the previous one (one integral over all the points $q$ on the segment from $p$ to the nearest atmosphere boundary in direction $w$, and a nested double integral over all directions at each point $q$). Therefore, if we wanted to compute each order "from scratch", we would need a triple integral for double scattering, a sextuple integral for triple scattering, etc. This would be clearly inefficient, because of all the redundant computations (the computations for order $n$ would basically redo all the computations for all previous orders, leading to quadratic complexity in the total number of orders). Instead, it is much more efficient to proceed as follows:

* precompute single scattering in a texture (as described above),

* for $n \ge 2$:

  precompute the $n$-th scattering in a texture, with a triple integral whose integrand uses lookups in the $(n-1)$-th scattering texture

This strategy avoids many redundant computations but does not eliminate all of them. Consider for instance the points $p$ and $p'$ in the figure below, and the computations which are necessary to compute the light arriving at these two points from direction $w$ after $n$ bounces. These computations involve, in particular, the evaluation of the radiance $L$ which is scattered at $q$ in direction $-w$, and coming from all directions after $n-1$ bounces:

Therefore, if we computed the n-th scattering with a triple integral as described above, we would compute $L$ redundantly (in fact, for all points $p$ between $q$ and the nearest atmosphere boundary in direction $-w$). To avoid this, and thus increase the efficiency of the multiple scattering computations, we refine the above algorithm as follows:

* precompute single scattering in a texture (as described above),
* for $n \ge 2$:
  * for each point $q$ and direction $w$, precompute the light which is scattered at $q$ towards direction $-w$, coming from any direction  after $n-1$ bounces (this involves only a double integral, whose integrand uses lookups in the $(n-1)$-th scattering texture),
  * for each point $p$ and direction $w$, precompute the light coming from direction $w$ after $n$ bounces (this involves only a single integral, whose integrand uses lookups in the texture computed at the previous line)

To get a complete algorithm, we must now specify how we implement the two steps in the above loop. This is what we do in the rest of this section.

#### First Step

The first step computes the radiance which is scattered at some point $q$ inside the atmosphere, towards some direction $-w$. Furthermore, we assume that this scattering event is the $n$-th bounce.

This radiance is the integral over all the possible incident directions $w_i$, of the product of the incident radiance $L_i$ arriving at $q$ from direction $w_i$ after $n-1$ bounces, which is the sum of:

* a term given by the precomputed scattering texture for the $(n-1)$-th order,
* if the ray $[q, w_i)$ intersects the ground at $r$, the contribution from the light paths with $n-1$ bounces and whose last bounce is at $r$, i.e. on the ground (these paths are excluded, by definition, from our precomputed textures, but we must take them into account here since the bounce on the ground is followed by a bounce at $q$). This contribution, in turn, is the product of:
  * the transmittance between $q$ and $r$,
  * the (average) ground albedo,
  * the <a href="https://www.cs.princeton.edu/~smr/cs348c-97/surveypaper.html">Lambertian BRDF</a> $1/\pi$,
  * the irradiance received on the ground after $n-2$ bounces. We explain in the<a href="#irradiance">next section</a> how we precompute it in a texture. For now, we assume that we can use the following function to retrieve this irradiance from a precomputed texture:

```glsl
IrradianceSpectrum GetIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(IrradianceTexture) irradiance_texture,
    Length r, Number mu_s);
```

* the scattering coefficient at $q$

* the scattering phase function for the directions $w$ and $w_i$

  

This leads to the following implementation (where <code>multiple_scattering_texture</code> is supposed to contain the $(n-1)$-th order of scattering, if $n>2$, <code>irradiance_texture</code> is the irradiance received on the ground after $n-2$ bounces, and <code>scattering_order</code> is
equal to $n$):

```glsl
RadianceDensitySpectrum ComputeScatteringDensity(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(IrradianceTexture) irradiance_texture,
    Length r, Number mu, Number mu_s, Number nu, int scattering_order) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  assert(nu >= -1.0 && nu <= 1.0);
  assert(scattering_order >= 2);

  // Compute unit direction vectors for the zenith, the view direction omega and
  // and the sun direction omega_s, such that the cosine of the view-zenith
  // angle is mu, the cosine of the sun-zenith angle is mu_s, and the cosine of
  // the view-sun angle is nu. The goal is to simplify computations below.
  vec3 zenith_direction = vec3(0.0, 0.0, 1.0);
  vec3 omega = vec3(sqrt(1.0 - mu * mu), 0.0, mu);
  Number sun_dir_x = omega.x == 0.0 ? 0.0 : (nu - mu * mu_s) / omega.x;
  Number sun_dir_y = sqrt(max(1.0 - sun_dir_x * sun_dir_x - mu_s * mu_s, 0.0));
  vec3 omega_s = vec3(sun_dir_x, sun_dir_y, mu_s);

  const int SAMPLE_COUNT = 16;
  const Angle dphi = pi / Number(SAMPLE_COUNT);
  const Angle dtheta = pi / Number(SAMPLE_COUNT);
  RadianceDensitySpectrum rayleigh_mie =
      RadianceDensitySpectrum(0.0 * watt_per_cubic_meter_per_sr_per_nm);

  // Nested loops for the integral over all the incident directions omega_i.
  for (int l = 0; l < SAMPLE_COUNT; ++l) {
    Angle theta = (Number(l) + 0.5) * dtheta;
    Number cos_theta = cos(theta);
    Number sin_theta = sin(theta);
    bool ray_r_theta_intersects_ground =
        RayIntersectsGround(atmosphere, r, cos_theta);
   
    // The distance and transmittance to the ground only depend on theta, so we
    // can compute them in the outer loop for efficiency.
    Length distance_to_ground = 0.0 * m;
    DimensionlessSpectrum transmittance_to_ground = DimensionlessSpectrum(0.0);
    DimensionlessSpectrum ground_albedo = DimensionlessSpectrum(0.0);
    if (ray_r_theta_intersects_ground) {
      distance_to_ground =
          DistanceToBottomAtmosphereBoundary(atmosphere, r, cos_theta);
      transmittance_to_ground =
          GetTransmittance(atmosphere, transmittance_texture, r, cos_theta,
              distance_to_ground, true /* ray_intersects_ground */);
      ground_albedo = atmosphere.ground_albedo;
    }

    for (int m = 0; m < 2 * SAMPLE_COUNT; ++m) {
      Angle phi = (Number(m) + 0.5) * dphi;
      vec3 omega_i =
          vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
      SolidAngle domega_i = (dtheta / rad) * (dphi / rad) * sin(theta) * sr;

      // The radiance L_i arriving from direction omega_i after n-1 bounces is
      // the sum of a term given by the precomputed scattering texture for the
      // (n-1)-th order:
      Number nu1 = dot(omega_s, omega_i);
      RadianceSpectrum incident_radiance = GetScattering(atmosphere,
          single_rayleigh_scattering_texture, single_mie_scattering_texture,
          multiple_scattering_texture, r, omega_i.z, mu_s, nu1,
          ray_r_theta_intersects_ground, scattering_order - 1);

      // and of the contribution from the light paths with n-1 bounces and whose
      // last bounce is on the ground. This contribution is the product of the
      // transmittance to the ground, the ground albedo, the ground BRDF, and
      // the irradiance received on the ground after n-2 bounces.
      vec3 ground_normal =
          normalize(zenith_direction * r + omega_i * distance_to_ground);
      IrradianceSpectrum ground_irradiance = GetIrradiance(atmosphere, irradiance_texture, atmosphere.bottom_radius, dot(ground_normal, omega_s));
      incident_radiance += transmittance_to_ground *
          ground_albedo * (1.0 / (PI * sr)) * ground_irradiance;

      // The radiance finally scattered from direction omega_i towards direction
      // -omega is the product of the incident radiance, the scattering
      // coefficient, and the phase function for directions omega and omega_i
      // (all this summed over all particle types, i.e. Rayleigh and Mie).
      Number nu2 = dot(omega, omega_i);
      Number rayleigh_density = GetProfileDensity(atmosphere.rayleigh_density, r - atmosphere.bottom_radius);
      Number mie_density = GetProfileDensity(atmosphere.mie_density, r - atmosphere.bottom_radius);
      rayleigh_mie += incident_radiance * (atmosphere.rayleigh_scattering * rayleigh_density * RayleighPhaseFunction(nu2) +
          atmosphere.mie_scattering * mie_density * MiePhaseFunction(atmosphere.mie_phase_function_g, nu2)) * domega_i;
	}
  }
  return rayleigh_mie;
}
```



#### Second step

The second step to compute the $n$-th order of scattering is to compute for each point $p$ and direction $w$, the radiance coming from direction $w$ after $n$ bounces, using a texture precomputed with the previous function.

This radiance is the integral over all points $q$ between $p$ and the nearest atmosphere boundary in direction $w$ of the product of:

* a term given by a texture precomputed with the previous function, namely the radiance scattered at $q$ towards $p$, coming from any direction after$n-1$ bounces,
* the transmittance betweeen $p$ and $q$

Note that this excludes the light paths with $n$ bounces and whose last bounce is on the ground, on purpose. Indeed, we chose to exclude these paths from our precomputed textures so that we can compute them at render time instead, using the actual ground albedo.

The implementation for this second step is straightforward:

```glsl
RadianceSpectrum ComputeMultipleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ScatteringDensityTexture) scattering_density_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu >= -1.0 && mu <= 1.0);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  assert(nu >= -1.0 && nu <= 1.0);

  // Number of intervals for the numerical integration.
  const int SAMPLE_COUNT = 50;
  // The integration step, i.e. the length of each integration interval.
  Length dx =
      DistanceToNearestAtmosphereBoundary(
          atmosphere, r, mu, ray_r_mu_intersects_ground) /
              Number(SAMPLE_COUNT);
  // Integration loop.
  RadianceSpectrum rayleigh_mie_sum =
      RadianceSpectrum(0.0 * watt_per_square_meter_per_sr_per_nm);
  for (int i = 0; i <= SAMPLE_COUNT; ++i) {
    Length d_i = Number(i) * dx;
    // The r, mu and mu_s parameters at the current integration point (see the
    // single scattering section for a detailed explanation).
    Length r_i =
        ClampRadius(atmosphere, sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r));
    Number mu_i = ClampCosine((r * mu + d_i) / r_i);
    Number mu_s_i = ClampCosine((r * mu_s + d_i * nu) / r_i);

    // The Rayleigh and Mie multiple scattering at the current sample point.
    RadianceSpectrum rayleigh_mie_i =
        GetScattering(atmosphere, scattering_density_texture, r_i, mu_i, mu_s_i, nu, ray_r_mu_intersects_ground) *
        GetTransmittance(atmosphere, transmittance_texture, r, mu, d_i,ray_r_mu_intersects_ground) * dx;
    // Sample weight (from the trapezoidal rule).
    Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
    rayleigh_mie_sum += rayleigh_mie_i * weight_i;
    }
  return rayleigh_mie_sum;
}
```



#### Precomputation

As explained in the <a href="#multiple_scattering">overall algorithm</a> to compute multiple scattering, we need to precompute each order of scattering in a texture to save computations while computing the next order. And, in order to store a function in a texture, we need a mapping from the function parameters to texture coordinates. Fortunately, all the orders of scattering depend on the same $(r,\mu,\mu_s,\nu)$ parameters as single scattering, so we can simple reuse the mappings defined for single scattering. This immediately leads to the following simple functions to precompute a texel of the textures for the
<a href="#multiple_scattering_first_step">first</a> and<a href="#multiple_scattering_second_step">second</a> steps of each iteration over the number of bounces:

```glsl
RadianceDensitySpectrum ComputeScatteringDensityTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(IrradianceTexture) irradiance_texture,
    IN(vec3) frag_coord, int scattering_order) {
  Length r;
  Number mu;
  Number mu_s;
  Number nu;
  bool ray_r_mu_intersects_ground;
  GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
      r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  return ComputeScatteringDensity(atmosphere, transmittance_texture,
      single_rayleigh_scattering_texture, single_mie_scattering_texture,
      multiple_scattering_texture, irradiance_texture, r, mu, mu_s, nu,
      scattering_order);
}

RadianceSpectrum ComputeMultipleScatteringTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ScatteringDensityTexture) scattering_density_texture,
    IN(vec3) frag_coord, OUT(Number) nu) {
  Length r;
  Number mu;
  Number mu_s;
  bool ray_r_mu_intersects_ground;
  GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
      r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  return ComputeMultipleScattering(atmosphere, transmittance_texture,
      scattering_density_texture, r, mu, mu_s, nu,
      ray_r_mu_intersects_ground);
}
```



#### Lookup

Likewise, we can simply reuse the lookup function <code>GetScattering</code> implemented for single scattering to read a value from the precomputed textures for multiple scattering. In fact, this is what we did above in the<code>ComputeScatteringDensity</code> and <code>ComputeMultipleScattering</code> functions.

### Ground irradiance

The ground irradiance is the Sun light received on the ground after $n \ge 0$ bounces (where a bounce is either a scattering event or a reflection on the ground). We need this for two purposes:

* while precomputing the $n$-th order of scattering, with $n \ge 2$, in order to compute the contribution of light paths whose $(n-1)$-th bounce is on the ground (which requires the ground irradiance after $n-2$ bounces - see the <a href="#multiple_scattering_computation">Multiple scattering</a>
section),
* at rendering time, to compute the contribution of light paths whose last bounce is on the ground (these paths are excluded, by definition, from our precomputed scattering textures)

In the first case we only need the ground irradiance for horizontal surfaces at the bottom of the atmosphere (during precomputations we assume a perfectly spherical ground with a uniform albedo). In the second case, however, we need the ground irradiance for any altitude and any surface normal, and we want to precompute it for efficiency. In fact, as described in our <a href="https://hal.inria.fr/inria-00288758/en">paper</a> we precompute it only for horizontal surfaces, at any altitude (which requires only 2D textures, instead of 4D textures for the general case), and we use approximations for non-horizontal surfaces.

The following sections describe how we compute the ground irradiance, how we store it in a precomputed texture, and how we read it back.



#### Computation

The ground irradiance computation is different for the direct irradiance, i.e. the light received directly from the Sun, without any intermediate bounce, and for the indirect irradiance (at least one bounce). We start here with the direct irradiance.

The irradiance is the integral over an hemisphere of the incident radiance, times a cosine factor. For the direct ground irradiance, the incident radiance is the Sun radiance at the top of the atmosphere, times the transmittance through the atmosphere. And, since the Sun solid angle is small, we can approximate the transmittance with a constant, i.e. we can move it outside the irradiance integral, which can be performed over (the visible fraction of) the Sun disc rather than the hemisphere. Then the integral becomes equivalent to the ambient occlusion due to a sphere, also called a view factor, which is given in <a href="http://webserver.dmt.upm.es/~isidoro/tc3/Radiation%20View%20factors.pdf
">Radiative view factors</a> (page 10). For a small solid angle, these complex equations can be simplified as follows:

```glsl
IrradianceSpectrum ComputeDirectIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu_s) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu_s >= -1.0 && mu_s <= 1.0);

  Number alpha_s = atmosphere.sun_angular_radius / rad;
  // Approximate average of the cosine factor mu_s over the visible fraction of
  // the Sun disc.
  Number average_cosine_factor =
    mu_s < -alpha_s ? 0.0 : (mu_s > alpha_s ? mu_s :
        (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s));

  return atmosphere.solar_irradiance *
      GetTransmittanceToTopAtmosphereBoundary(
          atmosphere, transmittance_texture, r, mu_s) * average_cosine_factor;

}
```

For the indirect ground irradiance the integral over the hemisphere must be computed numerically. More precisely we need to compute the integral over all
the directions $w$ of the hemisphere, of the product of:

* the radiance arriving from direction $w$ after $n$ bounces,
* the cosine factor, i.e. $\omega _z$

This leads to the following implementation (where <code>multiple_scattering_texture</code> is supposed to contain the $n$-th
order of scattering, if $n>1$, and <code>scattering_order</code> is equal to $n$):

```glsl
IrradianceSpectrum ComputeIndirectIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    Length r, Number mu_s, int scattering_order) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  assert(scattering_order >= 1);

  const int SAMPLE_COUNT = 32;
  const Angle dphi = pi / Number(SAMPLE_COUNT);
  const Angle dtheta = pi / Number(SAMPLE_COUNT);

  IrradianceSpectrum result =
      IrradianceSpectrum(0.0 * watt_per_square_meter_per_nm);
  vec3 omega_s = vec3(sqrt(1.0 - mu_s * mu_s), 0.0, mu_s);
  for (int j = 0; j < SAMPLE_COUNT / 2; ++j) {
    Angle theta = (Number(j) + 0.5) * dtheta;
    for (int i = 0; i < 2 * SAMPLE_COUNT; ++i) {
      Angle phi = (Number(i) + 0.5) * dphi;
      vec3 omega = vec3(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));
      SolidAngle domega = (dtheta / rad) * (dphi / rad) * sin(theta) * sr;

      Number nu = dot(omega, omega_s);
      result += GetScattering(atmosphere, single_rayleigh_scattering_texture,single_mie_scattering_texture, multiple_scattering_texture,
          r, omega.z, mu_s, nu, false /* ray_r_theta_intersects_ground */,scattering_order) * omega.z * domega;
    }
  }
  return result;
}
```

#### Precomputation

In order to precompute the ground irradiance in a texture we need a mapping
from the ground irradiance parameters to texture coordinates. Since we
precompute the ground irradiance only for horizontal surfaces, this irradiance
depends only on $r$ and $\mu_s$, so we need a mapping from $(r,\mu_s)$ to
$(u,v)$ texture coordinates. The simplest, affine mapping is sufficient here,
because the ground irradiance function is very smooth:

```glsl
vec2 GetIrradianceTextureUvFromRMuS(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu_s) {
  assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  assert(mu_s >= -1.0 && mu_s <= 1.0);
  Number x_r = (r - atmosphere.bottom_radius) / (atmosphere.top_radius - atmosphere.bottom_radius);
  Number x_mu_s = mu_s * 0.5 + 0.5;
  return vec2(GetTextureCoordFromUnitRange(x_mu_s, IRRADIANCE_TEXTURE_WIDTH),
              GetTextureCoordFromUnitRange(x_r, IRRADIANCE_TEXTURE_HEIGHT));
}
```

The inverse mapping follows immediately:

```glsl
void GetRMuSFromIrradianceTextureUv(IN(AtmosphereParameters) atmosphere,
    IN(vec2) uv, OUT(Length) r, OUT(Number) mu_s) {
  assert(uv.x >= 0.0 && uv.x <= 1.0);
  assert(uv.y >= 0.0 && uv.y <= 1.0);
  Number x_mu_s = GetUnitRangeFromTextureCoord(uv.x, IRRADIANCE_TEXTURE_WIDTH);
  Number x_r = GetUnitRangeFromTextureCoord(uv.y, IRRADIANCE_TEXTURE_HEIGHT);
  r = atmosphere.bottom_radius + x_r * (atmosphere.top_radius - atmosphere.bottom_radius);
  mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
}
```

It is now easy to define a fragment shader function to precompute a texel of the ground irradiance texture, for the direct irradiance:

```glsl
const vec2 IRRADIANCE_TEXTURE_SIZE = vec2(IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT);

IrradianceSpectrum ComputeDirectIrradianceTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(vec2) frag_coord) {
  Length r;
  Number mu_s;
  GetRMuSFromIrradianceTextureUv(
      atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
  return ComputeDirectIrradiance(atmosphere, transmittance_texture, r, mu_s);
}
```

and the indirect one:

```glsl
IrradianceSpectrum ComputeIndirectIrradianceTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(vec2) frag_coord, int scattering_order) {
  Length r;
  Number mu_s;
  GetRMuSFromIrradianceTextureUv(
      atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
  return ComputeIndirectIrradiance(atmosphere,
      single_rayleigh_scattering_texture, single_mie_scattering_texture,
      multiple_scattering_texture, r, mu_s, scattering_order);
}
```

#### Lookup

Thanks to these precomputed textures, we can now get the ground irradiance
with a single texture lookup:

```
IrradianceSpectrum GetIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(IrradianceTexture) irradiance_texture,
    Length r, Number mu_s) {
  vec2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, mu_s);
  return IrradianceSpectrum(texture(irradiance_texture, uv));
}
```

### Rendering

Here we assume that the transmittance, scattering and irradiance textures have been precomputed, and we provide functions using them to compute the sky
color, the aerial perspective, and the ground radiance.

More precisely, we assume that the single Rayleigh scattering, without its phase function term, plus the multiple scattering terms (divided by the Rayleigh phase function for dimensional homogeneity) are stored in a <code>scattering_texture</code>. We also assume that the single Mie scattering is stored, without its phase function term:

<ul>
<li>either separately, in a <code>single_mie_scattering_texture</code> (this
option was not provided our <a href=
"http://evasion.inrialpes.fr/~Eric.Bruneton/PrecomputedAtmosphericScattering2.zip"
>original implementation</a>),</li>
<li>or, if the <code>COMBINED_SCATTERING_TEXTURES</code> preprocessor
macro is defined, in the <code>scattering_texture</code>. In this case, which is
only available with a GLSL compiler, Rayleigh and multiple scattering are stored
in the RGB channels, and the red component of the single Mie scattering is
stored in the alpha channel).

In the second case, the green and blue components of the single Mie scattering are extrapolated as described in our<a href="https://hal.inria.fr/inria-00288758/en">paper</a>, with the following function:

```glsl
#ifdef COMBINED_SCATTERING_TEXTURES

vec3 GetExtrapolatedSingleMieScattering(
    IN(AtmosphereParameters) atmosphere, IN(vec4) scattering) {
  // Algebraically this can never be negative, but rounding errors can produce
  // that effect for sufficiently short view rays.
  if (scattering.r <= 0.0) {
    return vec3(0.0);
  }
  return scattering.rgb * scattering.a / scattering.r *
	    (atmosphere.rayleigh_scattering.r / atmosphere.mie_scattering.r) *
	    (atmosphere.mie_scattering / atmosphere.rayleigh_scattering);
}
#endif
```



<p>We can then retrieve all the scattering components (Rayleigh + multiple
scattering on one side, and single Mie scattering on the other side) with the
following function, based on
<a href="#single_scattering_lookup"><code>GetScattering</code></a> (we duplicate
some code here, instead of using two calls to <code>GetScattering</code>, to
make sure that the texture coordinates computation is shared between the lookups
in <code>scattering_texture</code> and
<code>single_mie_scattering_texture</code>):

```glsl
IrradianceSpectrum GetCombinedScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    OUT(IrradianceSpectrum) single_mie_scattering) {
  vec4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
      atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  Number tex_coord_x = uvwz.x * Number(SCATTERING_TEXTURE_NU_SIZE - 1);
  Number tex_x = floor(tex_coord_x);
  Number lerp = tex_coord_x - tex_x;
  vec3 uvw0 = vec3((tex_x + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
      uvwz.z, uvwz.w);
  vec3 uvw1 = vec3((tex_x + 1.0 + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
      uvwz.z, uvwz.w);

#ifdef COMBINED_SCATTERING_TEXTURES
  vec4 combined_scattering = texture(scattering_texture, uvw0) * (1.0 - lerp) + texture(scattering_texture, uvw1) * lerp;
  IrradianceSpectrum scattering = IrradianceSpectrum(combined_scattering);
  single_mie_scattering = GetExtrapolatedSingleMieScattering(atmosphere, combined_scattering);

#else
  IrradianceSpectrum scattering = IrradianceSpectrum(texture(scattering_texture, uvw0) * (1.0 - lerp) + texture(scattering_texture, uvw1) * lerp);
  single_mie_scattering = IrradianceSpectrum(texture(single_mie_scattering_texture, uvw0) * (1.0 - lerp) + texture(single_mie_scattering_texture, uvw1) * lerp);
#endif

  return scattering;
}
```



#### Sky

To render the sky we simply need to display the sky radiance, which we can
get with a lookup in the precomputed scattering texture(s), multiplied by the
phase function terms that were omitted during precomputation. We can also return
the transmittance of the atmosphere (which we can get with a single lookup in
the precomputed transmittance texture), which is needed to correctly render the
objects in space (such as the Sun and the Moon). This leads to the following
function, where most of the computations are used to correctly handle the case
of viewers outside the atmosphere, and the case of light shafts:

```glsl
RadianceSpectrum GetSkyRadiance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    Position camera, IN(Direction) view_ray, Length shadow_length,
    IN(Direction) sun_direction, OUT(DimensionlessSpectrum) transmittance) {
  // Compute the distance to the top atmosphere boundary along the view ray,
  // assuming the viewer is in space (or NaN if the view ray does not intersect
  // the atmosphere).
  Length r = length(camera);
  Length rmu = dot(camera, view_ray);
  Length distance_to_top_atmosphere_boundary = -rmu -
      sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
  // If the viewer is in space and the view ray intersects the atmosphere, move
  // the viewer to the top atmosphere boundary (along the view ray):
  if (distance_to_top_atmosphere_boundary > 0.0 * m) {
    camera = camera + view_ray * distance_to_top_atmosphere_boundary;
    r = atmosphere.top_radius;
    rmu += distance_to_top_atmosphere_boundary;
  } else if (r > atmosphere.top_radius) {
    // If the view ray does not intersect the atmosphere, simply return 0.
    transmittance = DimensionlessSpectrum(1.0);
    return RadianceSpectrum(0.0 * watt_per_square_meter_per_sr_per_nm);
  }
  // Compute the r, mu, mu_s and nu parameters needed for the texture lookups.
  Number mu = rmu / r;
  Number mu_s = dot(camera, sun_direction) / r;
  Number nu = dot(view_ray, sun_direction);
  bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

  transmittance = ray_r_mu_intersects_ground ? DimensionlessSpectrum(0.0) :
      GetTransmittanceToTopAtmosphereBoundary(
          atmosphere, transmittance_texture, r, mu);
  IrradianceSpectrum single_mie_scattering;
  IrradianceSpectrum scattering;
  if (shadow_length == 0.0 * m) {
    scattering = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground,
        single_mie_scattering);
  } else {
    // Case of light shafts (shadow_length is the total length noted l in our
    // paper): we omit the scattering between the camera and the point at
    // distance l, by implementing Eq. (18) of the paper (shadow_transmittance
    // is the T(x,x_s) term, scattering is the S|x_s=x+lv term).
    Length d = shadow_length;
    Length r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    Number mu_p = (r * mu + d) / r_p;
    Number mu_s_p = (r * mu_s + d * nu) / r_p;

    scattering = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
        single_mie_scattering);
    DimensionlessSpectrum shadow_transmittance =
        GetTransmittance(atmosphere, transmittance_texture,
            r, mu, shadow_length, ray_r_mu_intersects_ground);
    scattering = scattering * shadow_transmittance;
    single_mie_scattering = single_mie_scattering * shadow_transmittance;

  }
  return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
      MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}
```

#### Aerial perspective

To render the aerial perspective we need the transmittance and the scattering
between two points (i.e. between the viewer and a point on the ground, which can
at an arbibrary altitude). We already have a function to compute the
transmittance between two points (using 2 lookups in a texture which only
contains the transmittance to the top of the atmosphere), but we don't have one
for the scattering between 2 points. Hopefully, the scattering between 2 points
can be computed from two lookups in a texture which contains the scattering to
the nearest atmosphere boundary, as for the transmittance (except that here the
two lookup results must be subtracted, instead of divided). This is what we
implement in the following function (the initial computations are used to
correctly handle the case of viewers outside the atmosphere):

```glsl
RadianceSpectrum GetSkyRadianceToPoint(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    Position camera, IN(Position) point, Length shadow_length,
    IN(Direction) sun_direction, OUT(DimensionlessSpectrum) transmittance) {
  // Compute the distance to the top atmosphere boundary along the view ray,
  // assuming the viewer is in space (or NaN if the view ray does not intersect
  // the atmosphere).
  Direction view_ray = normalize(point - camera);
  Length r = length(camera);
  Length rmu = dot(camera, view_ray);
  Length distance_to_top_atmosphere_boundary = -rmu -
      sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
  // If the viewer is in space and the view ray intersects the atmosphere, move
  // the viewer to the top atmosphere boundary (along the view ray):
  if (distance_to_top_atmosphere_boundary > 0.0 * m) {
    camera = camera + view_ray * distance_to_top_atmosphere_boundary;
    r = atmosphere.top_radius;
    rmu += distance_to_top_atmosphere_boundary;
  }

  // Compute the r, mu, mu_s and nu parameters for the first texture lookup.
  Number mu = rmu / r;
  Number mu_s = dot(camera, sun_direction) / r;
  Number nu = dot(view_ray, sun_direction);
  Length d = length(point - camera);
  bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

  transmittance = GetTransmittance(atmosphere, transmittance_texture,
      r, mu, d, ray_r_mu_intersects_ground);

  IrradianceSpectrum single_mie_scattering;
  IrradianceSpectrum scattering = GetCombinedScattering(
      atmosphere, scattering_texture, single_mie_scattering_texture,
      r, mu, mu_s, nu, ray_r_mu_intersects_ground,
      single_mie_scattering);

  // Compute the r, mu, mu_s and nu parameters for the second texture lookup.
  // If shadow_length is not 0 (case of light shafts), we want to ignore the
  // scattering along the last shadow_length meters of the view ray, which we
  // do by subtracting shadow_length from d (this way scattering_p is equal to
  // the S|x_s=x_0-lv term in Eq. (17) of our paper).
  d = max(d - shadow_length, 0.0 * m);
  Length r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
  Number mu_p = (r * mu + d) / r_p;
  Number mu_s_p = (r * mu_s + d * nu) / r_p;

  IrradianceSpectrum single_mie_scattering_p;
  IrradianceSpectrum scattering_p = GetCombinedScattering(
      atmosphere, scattering_texture, single_mie_scattering_texture,
      r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
      single_mie_scattering_p);

  // Combine the lookup results to get the scattering between camera and point.
  DimensionlessSpectrum shadow_transmittance = transmittance;
  if (shadow_length > 0.0 * m) {
    // This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
    shadow_transmittance = GetTransmittance(atmosphere, transmittance_texture,
        r, mu, d, ray_r_mu_intersects_ground);
  }
  scattering = scattering - shadow_transmittance * scattering_p;
  single_mie_scattering =
      single_mie_scattering - shadow_transmittance * single_mie_scattering_p;

#ifdef COMBINED_SCATTERING_TEXTURES

  single_mie_scattering = GetExtrapolatedSingleMieScattering(
      atmosphere, vec4(scattering, single_mie_scattering.r));

#endif

  // Hack to avoid rendering artifacts when the sun is below the horizon.
  single_mie_scattering = single_mie_scattering *
      smoothstep(Number(0.0), Number(0.01), mu_s);

  return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
      MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}
```

#### Ground

To render the ground we need the irradiance received on the ground after 0 or more bounce(s) in the atmosphere or on the ground. The direct irradiance can be computed with a lookup in the transmittance texture, via <code>GetTransmittanceToSun</code>, while the indirect irradiance is given by a lookup in the precomputed irradiance texture (this texture only contains the irradiance for horizontal surfaces; we use the approximation defined in our <a href="https://hal.inria.fr/inria-00288758/en">paper</a> for the other cases).
The function below returns the direct and indirect irradiances separately:

```glsl
IrradianceSpectrum GetSunAndSkyIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(IrradianceTexture) irradiance_texture,
    IN(Position) point, IN(Direction) normal, IN(Direction) sun_direction,
    OUT(IrradianceSpectrum) sky_irradiance) {
  Length r = length(point);
  Number mu_s = dot(point, sun_direction) / r;

  // Indirect irradiance (approximated if the surface is not horizontal).
  sky_irradiance = GetIrradiance(atmosphere, irradiance_texture, r, mu_s) *
      (1.0 + dot(normal, point) / r) * 0.5;

  // Direct irradiance.
  return atmosphere.solar_irradiance *
      GetTransmittanceToSun(
          atmosphere, transmittance_texture, r, mu_s) *
      max(dot(normal, sun_direction), 0.0);
}
```

