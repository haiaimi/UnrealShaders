As the light travels from a point $p$ to a point $q$ in the atmosphere,
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

The radiance arriving at $p$ is the product of:

* the solar irradiance at the top of the atmosphere,
* the transmittance between the Sun and $q$ (i.e. the fraction of the Sunlight at the top of the atmosphere that reaches $q$),

* the Rayleigh scattering coefficient at $q$ (i.e. the fraction of the light arriving at $q$ which is scattered, in any direction),
* the Rayleigh phase function (i.e. the fraction of the scattered light at $q$ which is actually scattered towards $p$),
* the transmittance between $q$ and $p$ (i.e. the fraction of the light scattered at $q$ towards $p$ that reaches $p$).