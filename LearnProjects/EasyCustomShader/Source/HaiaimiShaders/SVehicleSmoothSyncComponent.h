// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SVehicleSmoothSyncComponent.generated.h"

USTRUCT()
struct FVehicleState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	FQuat Rotation;

	UPROPERTY()
	FVector LinearVelocity;

	UPROPERTY()
	FVector AngularVelocity;

	UPROPERTY()
	float OwnerTime;

	FVehicleState() :
		Position(ForceInitToZero),
		Rotation(ForceInitToZero),
		LinearVelocity(ForceInitToZero),
		AngularVelocity(ForceInitToZero),
		OwnerTime(0.f)
	{}

	void CopyFromRigidState(const FRigidBodyState& BodyState)
	{
		Position = BodyState.Position;
		Rotation = BodyState.Quaternion;
		LinearVelocity = BodyState.LinVel;
		AngularVelocity = BodyState.AngVel;
	}
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class USVehicleSmoothSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USVehicleSmoothSyncComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason)override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_SendStateToServer(FVehicleState NewState);

	UFUNCTION(NetMulticast, Unreliable)
	void MultiCast_SendStateToAllClient(FVehicleState NewState);

	void SendState();

	void AddState(FVehicleState NewState);

	UFUNCTION(BlueprintCallable)
	static void AdjustOrientation(class UStaticMeshComponent* LMesh, class UStaticMeshComponent* RMesh);

private:
	void SmoothVehicleMovement(float DeltaTime);

	void Interpolate(float DestTime, FVehicleState* StartState, FVehicleState* EndState);

	void Extrapolate(float DestTime, FVehicleState* LastState);

	FVehicleState* GetNearestStateSnapshot(const FVehicleState& InState, float& OutTime);

	void SmoothPhysics(float DeltaTime, FBodyInstance* BodyInstance);

	bool AdjustVehicleOrientation(const FVehicleState& InState, FQuat& OutDiffQuat);

	bool IsPointInCone(const FPlane& ComparedPlane, const FVector& StartPoint, const FVector& EndPoint);

	FVehicleState CurveMovement(float DestTime, FVehicleState* PreState, FVehicleState* StartState, FVehicleState* EndState = nullptr);

	void PrintDebug(FString&& InString);

private:
	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 1, ClampMax = 50), Category = BaseConfig)
	int32 MaxSnapshots;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 1, ClampMax = 60), Category = BaseConfig)
	int32 SendStateRate;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = BaseConfig)
	float PingLimit;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 1.0f), Category = Interpolation)
	float PositionLerp;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 1.0f), Category = Interpolation)
	float AngleLerp;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 1.0f), Category = Interpolation)
	float LinearVelocityLerp;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 1.0f), Category = Interpolation)
	float AngleVelocityLerp;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = Interpolation)
	float MinAngleTolerance;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = Interpolation)
	float MaxAngleTolerance;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 90.0f), Category = Interpolation)
	float RollDiffTolerance;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 90.0f), Category = Interpolation)
	float YawDiffTolerance;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f, ClampMax = 90.0f), Category = Interpolation)
	float PitchDiffTolerance;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = Interpolation)
	float AccTimeCoefficient;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = Interpolation)
	float InterVelCoefficient;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 0.0f), Category = Interpolation)
	float InterAngCoefficient;

	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = 1.0f), Category = Extrapolation)
	float ExtrapolationCoefficient;

	TDoubleLinkedList<FVehicleState> StateSnapshotList;

	float CurSimulationTime;

	float LastSendStateTime;

	float LastReceiveStateTime;

	FVehicleState TargetVehicleState;

	UPROPERTY()
	class USkinnedMeshComponent* UpdatedComponent;

	bool bForceVehicleRotation;

	bool bShouldAccelerate;

private:
	FCalculateCustomPhysics SmoothSyncDelegate;
};
