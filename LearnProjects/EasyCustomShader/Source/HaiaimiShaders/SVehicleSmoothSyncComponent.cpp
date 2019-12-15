// Fill out your copyright notice in the Description page of Project Settings.


#include "SVehicleSmoothSyncComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkinnedMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/StaticMeshComponent.h"

// Sets default values for this component's properties
USVehicleSmoothSyncComponent::USVehicleSmoothSyncComponent() :
	MaxSnapshots(30),
	SendStateRate(30),
	PingLimit(100.f),
	PositionLerp(0.3f),
	AngleLerp(0.5f),
	LinearVelocityLerp(0.3f),
	AngleVelocityLerp(0.4f),
	MinAngleTolerance(5.f),
	MaxAngleTolerance(60.f),
	RollDiffTolerance(10.f),
	YawDiffTolerance(10.f),
	PitchDiffTolerance(10.f),
	AccTimeCoefficient(2.f),
	InterVelCoefficient(100.f),
	InterAngCoefficient(100.f),
	ExtrapolationCoefficient(30.f),
	CurSimulationTime(0.f),
	LastSendStateTime(0.f),
	LastReceiveStateTime(0.f),
	UpdatedComponent(nullptr),
	bForceVehicleRotation(false),
	bShouldAccelerate(false)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicated(true);

	SmoothSyncDelegate.BindUObject(this, &USVehicleSmoothSyncComponent::SmoothPhysics);
}


// Called when the game starts
void USVehicleSmoothSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner())
	{
		UpdatedComponent = Cast<USkinnedMeshComponent>(GetOwner()->GetComponentByClass(USkinnedMeshComponent::StaticClass()));	
	}
}


void USVehicleSmoothSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

// Called every frame
void USVehicleSmoothSyncComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner()->bReplicateMovement)
		return;

	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		if (UGameplayStatics::GetRealTimeSeconds(GetWorld()) - 1.f / SendStateRate >= LastSendStateTime)
		{
			SendState();
		}
	}
	else
	{
		if (UpdatedComponent)
			//UpdatedComponent->GetBodyInstance()->AddCustomPhysics(SmoothSyncDelegate);
		SmoothVehicleMovement(DeltaTime);
	}
}

void USVehicleSmoothSyncComponent::SendState()
{
	if (UpdatedComponent)
	{
		FRigidBodyState BodyState;
		UpdatedComponent->GetRigidBodyState(BodyState);

		FVehicleState NewState;
		NewState.Position = BodyState.Position;
		NewState.Rotation = BodyState.Quaternion;
		NewState.LinearVelocity = BodyState.LinVel;
		NewState.AngularVelocity = BodyState.AngVel;
		NewState.OwnerTime = UGameplayStatics::GetRealTimeSeconds(this);
		LastSendStateTime = UGameplayStatics::GetRealTimeSeconds(this);

		Server_SendStateToServer(NewState);
	}
}

void USVehicleSmoothSyncComponent::AddState(FVehicleState NewState)
{
	StateSnapshotList.AddHead(NewState);

	for (auto& Iter : StateSnapshotList)
	{
		DrawDebugPoint(GetWorld(), Iter.Position, 10.f, FColor::Red, false, 100.f);
	}

	if (StateSnapshotList.Num() == 1 && CurSimulationTime == 0.f)
	{
		CurSimulationTime = NewState.OwnerTime - GetWorld()->DeltaTimeSeconds;
	}

	while (StateSnapshotList.Num() > MaxSnapshots)
	{
		StateSnapshotList.RemoveNode(StateSnapshotList.GetTail());   //Remove extern snapshot
	}

	LastReceiveStateTime = UGameplayStatics::GetRealTimeSeconds(GetWorld());
}

void USVehicleSmoothSyncComponent::AdjustOrientation(class UStaticMeshComponent* LMesh, class UStaticMeshComponent* RMesh)
{
	FVehicleState InState, CurState;
	InState.Position = LMesh->K2_GetComponentLocation();
	InState.Rotation = LMesh->K2_GetComponentRotation().Quaternion();
	CurState.Position = RMesh->K2_GetComponentLocation();
	CurState.Rotation = RMesh->K2_GetComponentRotation().Quaternion();
	const FVector DestRightDir = InState.Rotation.GetRightVector();
	const FVector DestForwardDir = InState.Rotation.GetForwardVector();
	const FVector DestUpDir = InState.Rotation.GetUpVector();

	const FVector CurRightDir = CurState.Rotation.GetRightVector();
	const FVector CurForwardDir = CurState.Rotation.GetForwardVector();
	const FVector CurUpDir = CurState.Rotation.GetUpVector();
	
	FVector CrossVec = FVector::CrossProduct(CurForwardDir, DestForwardDir);
	float DiffAngle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(CurForwardDir, DestForwardDir)));

	FPlane PlaneUpForward(FVector(InState.Position), FVector(InState.Position) + DestForwardDir, FVector(InState.Position) + FVector::UpVector);
	FVector Pitch2D = (FVector::PointPlaneProject(CurState.Position + CurForwardDir, PlaneUpForward) - FVector::PointPlaneProject(CurState.Position, PlaneUpForward)).GetSafeNormal();
	float CosValue_Up = FVector::DotProduct(Pitch2D, DestUpDir);

	DrawDebugDirectionalArrow(LMesh->GetWorld(), InState.Position, InState.Position + DestForwardDir * 1500.f, 50.f, FColor::Blue, false, 500.f);
	DrawDebugDirectionalArrow(LMesh->GetWorld(), InState.Position, InState.Position + Pitch2D * 1500.f, 50.f, FColor::Red, false, 500.f);

	FPlane VertPlane, HoriPlane;
	if (FVector::DotProduct(CurForwardDir, DestUpDir) > 0.f)
	{
		//VertPlane = 
	}

	float CosValue = FVector::DotProduct(Pitch2D, DestForwardDir);
	float Angle = FMath::RadiansToDegrees(FMath::Acos(CosValue));

	FQuat NewQuat(CrossVec.GetSafeNormal(), FMath::Acos(FVector::DotProduct(CurForwardDir, DestForwardDir)));
	RMesh->SetWorldRotation(NewQuat * CurState.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void USVehicleSmoothSyncComponent::SmoothVehicleMovement(float DeltaTime)
{
	auto TargetState = StateSnapshotList.GetHead();

	if (!TargetState || StateSnapshotList.Num() < MaxSnapshots)return;

	FRigidBodyState BodyState;
	UpdatedComponent->GetRigidBodyState(BodyState);
	FVehicleState TempState;
	TempState.CopyFromRigidState(BodyState);
	float AdaptTime = 0.f;
	//FVehicleState* NearestState = GetNearestStateSnapshot(TempState, AdaptTime);
	CurSimulationTime = FMath::Max(CurSimulationTime, AdaptTime);

	if (bShouldAccelerate)
	{
		CurSimulationTime = FMath::Min(TargetState->GetValue().OwnerTime - 0.01f, CurSimulationTime + DeltaTime * AccTimeCoefficient);
		//PrintDebug(TEXT("In acceleration"));
	}
	else
	{
		CurSimulationTime += DeltaTime;
	}


	if (GetOwnerRole() == ROLE_SimulatedProxy)
	{
		//UKismetSystemLibrary::PrintString(this, TEXT("Time Diff") + FString::SanitizeFloat(CurSimulationTime - TargetState->GetValue().OwnerTime));
	}

	if (TargetState->GetValue().OwnerTime < CurSimulationTime)
	{
		Extrapolate(CurSimulationTime, &TargetState->GetValue());
		//PrintDebug(TEXT("Extrapolate"));
	}
	else
	{
		//PrintDebug(TEXT("Interpolate"));
		int32 Index = 0;
		while (TargetState && TargetState->GetValue().OwnerTime > CurSimulationTime)
		{
			TargetState = TargetState->GetNextNode();
			Index++;
		}
		bShouldAccelerate = Index >= 2;
			
		if (TargetState && TargetState->GetPrevNode())
		{
			Interpolate(CurSimulationTime, &TargetState->GetValue(), &TargetState->GetPrevNode()->GetValue());
		}
		else if(TargetState == StateSnapshotList.GetTail())
		{
			FVehicleState& EndState = StateSnapshotList.GetTail()->GetValue();

			TargetVehicleState.Position = EndState.Position;
			TargetVehicleState.Rotation = EndState.Rotation;
			TargetVehicleState.AngularVelocity = EndState.AngularVelocity;
			TargetVehicleState.LinearVelocity = EndState.LinearVelocity;

			if (UpdatedComponent)
			{
				FVector DiffVec = TargetState->GetValue().Position - BodyState.Position;
				UpdatedComponent->SetPhysicsLinearVelocity(BodyState.LinVel + DiffVec * GetWorld()->GetDeltaSeconds() * InterVelCoefficient);
				FQuat DeltaQuat = BodyState.Quaternion.Inverse() * EndState.Rotation;
				FVector AngDiffAxis;
				float AngDiff;

				DeltaQuat.ToAxisAndAngle(AngDiffAxis, AngDiff);
				AngDiff = FMath::RadiansToDegrees(FMath::UnwindRadians(AngDiff));

				if (GetOwnerRole() == ROLE_SimulatedProxy)
				{
					//UKismetSystemLibrary::PrintString(this, TEXT("First Angle Diff:") + FString::SanitizeFloat(AngDiff));
					//UKismetSystemLibrary::PrintString(this, TEXT("Extrapolate:") + FString::SanitizeFloat((LastState->LinearVelocity + DiffVec).Size()));
				}

				FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
				if (FMath::Abs(AngDiff) > MinAngleTolerance && FMath::Abs(AngDiff) < MaxAngleTolerance)
					UpdatedComponent->SetPhysicsAngularVelocityInDegrees(AngDiffAxis * AngDiff * GetWorld()->GetDeltaSeconds() * InterAngCoefficient);
				//else if (FMath::Abs(AngDiff) >= MaxAngleTolerance)
				//{
				//	UpdatedComponent->SetWorldRotation(EndState.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
				//	//BodyInstance->SetBodyTransform(FTransform(EndState.Rotation, BodyState.Position), ETeleportType::TeleportPhysics);
				//	PrintDebug(TEXT("Rotate"));
				//}
					//UpdatedComponent->SetWorldRotation(DeltaQuat * FQuat(AngDiffAxis, FMath::DegreesToRadians(AngDiff / FMath::Abs(AngDiff) * 60.f)), false, nullptr, ETeleportType::TeleportPhysics);

				FVector NewPosition = FMath::Lerp(FVector(BodyState.Position), TargetVehicleState.Position, PositionLerp);
				DrawDebugPoint(GetWorld(), TargetVehicleState.Position + FVector::UpVector * 50.f, 20.f, FColor::Blue);
				FQuat NewRotation = FQuat::Slerp(BodyState.Quaternion, TargetVehicleState.Rotation, AngleLerp);
			}
		}
	}	
}

void USVehicleSmoothSyncComponent::Interpolate(float DestTime, FVehicleState* StartState, FVehicleState* EndState)
{
	if (StartState && (EndState->Position - StartState->Position).Size() > 1.f)
	{
		FRigidBodyState BodyState;
		UpdatedComponent->GetRigidBodyState(BodyState);
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();

		float LerpAlpha = (DestTime - StartState->OwnerTime) / (EndState->OwnerTime - StartState->OwnerTime);
		TargetVehicleState.Position = FMath::Lerp(FVector(BodyState.Position), EndState->Position, LerpAlpha);
		TargetVehicleState.Rotation = FQuat::Slerp(BodyState.Quaternion, EndState->Rotation, LerpAlpha);
		FVector DiffVec = TargetVehicleState.Position - BodyState.Position;
		
		TargetVehicleState.LinearVelocity = FMath::Lerp(StartState->LinearVelocity, EndState->LinearVelocity, LerpAlpha) + DiffVec * GetWorld()->GetDeltaSeconds() * InterVelCoefficient;
		TargetVehicleState.AngularVelocity = FMath::Lerp(StartState->AngularVelocity, EndState->AngularVelocity, LerpAlpha);
		
		

		FVector NewPosition = FMath::Lerp(FVector(BodyState.Position), TargetVehicleState.Position, PositionLerp);
		FQuat NewRotation = FQuat::Slerp(BodyState.Quaternion, TargetVehicleState.Rotation, AngleLerp);
		//BodyInstance->SetBodyTransform(FTransform(NewRotation, NewPosition), ETeleportType::TeleportPhysics);
		//PrintDebug(FString::SanitizeFloat((NewPosition - BodyState.Position).Size()));
		//UpdatedComponent->SetWorldRotation(NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
		//AdjustVehicleOrientation(TargetVehicleState);

		if (UpdatedComponent)
		{
			if ((TargetVehicleState.Position  - BodyState.Position).Size() > 5.f)
				BodyInstance->SetLinearVelocity(TargetVehicleState.LinearVelocity, false, false);
			FQuat DeltaQuat = BodyState.Quaternion.Inverse() * TargetVehicleState.Rotation;
			FVector AngDiffAxis;
			float AngDiff;
			DeltaQuat.ToAxisAndAngle(AngDiffAxis, AngDiff);
			AngDiff = FMath::RadiansToDegrees(FMath::UnwindRadians(AngDiff));
			DrawDebugDirectionalArrow(GetWorld(), BodyState.Position, BodyState.Position + AngDiffAxis * AngDiffAxis * 100.f, 100.f, FColor::Green, false, -1, 0, 3.f);
			FQuat OutDiff;
			bool bTeleportRotation = AdjustVehicleOrientation(TargetVehicleState, OutDiff);
			if (bTeleportRotation)
			{
				UpdatedComponent->SetWorldRotation(OutDiff * BodyState.Quaternion, false, nullptr, ETeleportType::TeleportPhysics);
			}
			else
			{
				FVector NewAngVel = TargetVehicleState.AngularVelocity + AngDiffAxis * AngDiff * GetWorld()->GetDeltaSeconds() * InterAngCoefficient;
				BodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngVel), false, false);
			}
			//if (FMath::Abs(AngDiff) > MinAngleTolerance && FMath::Abs(AngDiff) < MaxAngleTolerance)
			//{
			//	FVector NewAngVel = TargetVehicleState.AngularVelocity + AngDiffAxis * AngDiff * GetWorld()->GetDeltaSeconds() * InterAngCoefficient;
			//	BodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngVel), false, false);
			//	PrintDebug(FString::SanitizeFloat(AngDiff));
			//}
			//else if (FMath::Abs(AngDiff) >= MaxAngleTolerance)
			//{
			//	//UpdatedComponent->SetWorldRotation(FQuat::Slerp(BodyState.Quaternion, TargetVehicleState.Rotation, 0.05f), false, nullptr, ETeleportType::TeleportPhysics);
			//	//PrintDebug(TEXT("Rotate"));
			//	//PrintDebug( TEXT("Interpolate Angle Diff:") + FString::SanitizeFloat(AngDiff));	
			//}
				//BodyInstance->SetBodyTransform(FTransform(FQuat::Slerp(BodyState.Quaternion, TargetVehicleState.Rotation, 0.2f), FMath::Lerp(FVector(BodyState.Position), TargetVehicleState.Position, 0.3f)), ETeleportType::TeleportPhysics);
			
			DrawDebugPoint(GetWorld(), TargetVehicleState.Position, 20.f, FColor::Blue);
		}
	}
}

void USVehicleSmoothSyncComponent::Extrapolate(float DestTime, FVehicleState* LastState)
{
	if (UpdatedComponent)
	{
		FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		FVehicleState& LastState = StateSnapshotList.GetHead()->GetValue();
		FVehicleState& PreLastState = StateSnapshotList.GetHead()->GetNextNode()->GetValue();
		FVector LinearAcceleration = (LastState.LinearVelocity - PreLastState.LinearVelocity) / (LastState.OwnerTime - PreLastState.OwnerTime);
		FVector AngularAcceleration = (LastState.AngularVelocity - PreLastState.AngularVelocity) / (LastState.OwnerTime - PreLastState.OwnerTime);
		FVector NewLinearVel = LastState.LinearVelocity + LinearAcceleration * GetWorld()->GetDeltaSeconds();
		FVector NewAngularVel = LastState.AngularVelocity + AngularAcceleration * GetWorld()->GetDeltaSeconds();

		BodyInstance->SetLinearVelocity(NewLinearVel, false);
		BodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngularVel), false);
		//if (LastState)
		//{

		//	FRigidBodyState BodyState;
		//	UpdatedComponent->GetRigidBodyState(BodyState);
		//	FBodyInstance* BodyInstance = UpdatedComponent->GetBodyInstance();
		//	FVector DiffVec = LastState->Position - BodyState.Position;
		//	FVector AngularVelocity = FMath::Lerp(FVector(BodyState.AngVel), LastState->AngularVelocity, AngleVelocityLerp);
		//	FQuat DeltaQuat = BodyState.Quaternion.Inverse() * FQuat::Slerp(BodyState.Quaternion, LastState->Rotation, 0.7f);
		//	FVector AngDiffAxis;
		//	float AngDiff;
		//	DeltaQuat.ToAxisAndAngle(AngDiffAxis, AngDiff);
		//	AngDiff = FMath::RadiansToDegrees(FMath::UnwindRadians(AngDiff));
		//	
		//	FVector NewPosition = FMath::Lerp(FVector(BodyState.Position), LastState->Position, PositionLerp);
		//	FQuat NewRotation = FQuat::Slerp(BodyState.Quaternion, LastState->Rotation, AngleLerp);
		//	//BodyInstance->SetBodyTransform(FTransform(NewRotation, NewPosition), ETeleportType::TeleportPhysics);
		//	//UpdatedComponent->SetWorldRotation(NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
		//	//AdjustVehicleOrientation(*LastState);

		//	FVehicleState TempState;
		//	TempState.CopyFromRigidState(BodyState);
		//	float Index = 0.f;
		//	FVehicleState* CurState = GetNearestStateSnapshot(TempState, Index);
		//	
		//	if ((LastState->Position - BodyState.Position).Size() > 5.f)
		//	{
		//		FVector NewLinVel = FMath::Lerp(CurState->LinearVelocity, LastState->LinearVelocity, LinearVelocityLerp) + DiffVec * GetWorld()->GetDeltaSeconds() * ExtrapolationCoefficient;
		//		BodyInstance->SetLinearVelocity(NewLinVel, false, false);
		//		//PrintDebug(TEXT("Velocity length:") + FString::SanitizeFloat(NewLinVel.Size()));
		//	}

		//	if (FMath::Abs(AngDiff) > MinAngleTolerance && FMath::Abs(AngDiff) < MaxAngleTolerance)
		//	{
		//		FVector NewAngVel = AngularVelocity + AngDiffAxis * AngDiff * GetWorld()->GetDeltaSeconds() * InterAngCoefficient;
		//		BodyInstance->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngVel), false, false);
		//		//PrintDebug(TEXT("Angula Vel length:") + FString::SanitizeFloat(NewAngVel.Size()));
		//	}
		//	//else if (FMath::Abs(AngDiff) >= MaxAngleTolerance)
		//	//{
		//	//	UpdatedComponent->SetWorldRotation(FQuat::Slerp(BodyState.Quaternion, LastState->Rotation, 0.5f), false, nullptr, ETeleportType::TeleportPhysics);
		//	//	//PrintDebug(TEXT("Extraplote Angle Diff:") + FString::SanitizeFloat(AngDiff));
		//	//}
		//		//BodyInstance->SetBodyTransform(FTransform(FQuat::Slerp(BodyState.Quaternion, LastState->Rotation, 0.2f),  FMath::Lerp(FVector(BodyState.Position), LastState->Position, 0.3f)), ETeleportType::TeleportPhysics);
		//			
		//}
	}
}

FVehicleState* USVehicleSmoothSyncComponent::GetNearestStateSnapshot(const FVehicleState& InState, float& OutTime)
{
	auto HeadState = StateSnapshotList.GetHead();
	if (!HeadState)return nullptr;

	int32 i = 0;
	while (HeadState && HeadState->GetNextNode())
	{
		i++;
		const FVector FromForward = HeadState->GetValue().Position - InState.Position;
		const FVector FromBackward = HeadState->GetNextNode()->GetValue().Position - InState.Position;
		if (FVector::DotProduct(FromForward, FromBackward) < 0.f && FVector::DotProduct(InState.LinearVelocity, HeadState->GetValue().LinearVelocity) >= 0.f)
		{
			const FVector SnapshotInterval = HeadState->GetValue().Position - HeadState->GetNextNode()->GetValue().Position;
			const float Mid = FVector::DotProduct(InState.Position - HeadState->GetNextNode()->GetValue().Position, SnapshotInterval) / (FMath::Pow(SnapshotInterval.Size(), 2));
			OutTime = FMath::Lerp(HeadState->GetNextNode()->GetValue().OwnerTime, HeadState->GetValue().OwnerTime, Mid);
			
			return &HeadState->GetNextNode()->GetValue();
		}
		HeadState = HeadState->GetNextNode();
	}
	
	if ((InState.Position - StateSnapshotList.GetHead()->GetValue().Position).Size() > (InState.Position - StateSnapshotList.GetTail()->GetValue().Position).Size())
	{
		OutTime = StateSnapshotList.GetTail()->GetValue().OwnerTime - 0.01f;
		return &StateSnapshotList.GetTail()->GetValue();
	}
	else
	{
		OutTime = StateSnapshotList.GetHead()->GetValue().OwnerTime - 0.01f;
		return &StateSnapshotList.GetHead()->GetValue();
	}
}

void USVehicleSmoothSyncComponent::SmoothPhysics(float DeltaTime, FBodyInstance* BodyInstance)
{
	SmoothVehicleMovement(DeltaTime);
}

bool USVehicleSmoothSyncComponent::AdjustVehicleOrientation(const FVehicleState& InState, FQuat& OutDiffQuat)
{
	FRigidBodyState BodyState;
	UpdatedComponent->GetRigidBodyState(BodyState);
	FVehicleState CurState;
	CurState.CopyFromRigidState(BodyState);
	const FVector DestRightDir = InState.Rotation.GetRightVector();
	const FVector DestForwardDir = InState.Rotation.GetForwardVector();
	const FVector DestUpDir = InState.Rotation.GetUpVector();

	const FVector CurRightDir = CurState.Rotation.GetRightVector();
	const FVector CurForwardDir = CurState.Rotation.GetForwardVector();
	const FVector CurUpDir = CurState.Rotation.GetUpVector();
	
	FPlane PlaneUpForward(FVector(InState.Position), FVector(InState.Position) + DestForwardDir, FVector(InState.Position) + FVector::UpVector);
	FVector Pitch2D = (FVector::PointPlaneProject(CurState.Position + CurForwardDir, PlaneUpForward) - FVector::PointPlaneProject(CurState.Position, PlaneUpForward)).GetSafeNormal();
	float CosValue_Up = FVector::DotProduct(Pitch2D, DestUpDir);
	
	const FVector RightUp = FVector(FMath::Cos(FMath::DegreesToRadians(YawDiffTolerance)), FMath::Sin(FMath::DegreesToRadians(YawDiffTolerance)), FMath::Sin(FMath::DegreesToRadians(PitchDiffTolerance)));
	const FVector RightDown = FVector(FMath::Cos(FMath::DegreesToRadians(YawDiffTolerance)), FMath::Sin(FMath::DegreesToRadians(YawDiffTolerance)), -FMath::Sin(FMath::DegreesToRadians(PitchDiffTolerance)));
	const FVector LeftUp = FVector(FMath::Cos(FMath::DegreesToRadians(YawDiffTolerance)), -FMath::Sin(FMath::DegreesToRadians(YawDiffTolerance)), FMath::Sin(FMath::DegreesToRadians(PitchDiffTolerance)));
	const FVector LeftDown = FVector(FMath::Cos(FMath::DegreesToRadians(YawDiffTolerance)), -FMath::Sin(FMath::DegreesToRadians(YawDiffTolerance)), -FMath::Sin(FMath::DegreesToRadians(PitchDiffTolerance)));

	///For pitch,yaw
	FPlane PlaneYZ(InState.Position, InState.Position + DestForwardDir, InState.Position + CurForwardDir);
	FPlane PlaneVert, PlaneHoriz;
	bool bYawInDiff = false, bPitchInDiff = false;
	
	if (FVector::DotProduct(CurForwardDir, DestUpDir) > 0.f)
	{
		PlaneVert = FPlane(InState.Position, InState.Position + RightUp, InState.Position + LeftUp);
	}
	else
	{
		PlaneVert = FPlane(InState.Position, InState.Position + RightDown, InState.Position + LeftDown);
	}

	if (FVector::DotProduct(CurForwardDir, DestRightDir) > 0.f)
	{
		PlaneHoriz = FPlane(InState.Position, InState.Position + RightUp, InState.Position + RightDown);
	}
	else
	{
		PlaneHoriz = FPlane(InState.Position, InState.Position + LeftUp, InState.Position + LeftDown);
	}
	bPitchInDiff = IsPointInCone(PlaneVert, InState.Position + CurForwardDir, InState.Position + DestForwardDir);
	bYawInDiff = IsPointInCone(PlaneHoriz, InState.Position + CurForwardDir, InState.Position + DestForwardDir);

	FVector CrossVec = FVector::CrossProduct(CurForwardDir, DestForwardDir);

	if (bPitchInDiff && bYawInDiff)
	{
		OutDiffQuat = CurState.Rotation.Inverse()*InState.Rotation;
	}
	else
	{
		FVector VertInteractDir = PlaneVert ^ PlaneYZ;
		FVector HorizInteractDir = PlaneHoriz ^ PlaneYZ;
		float RotateAngleRadian = 0.f;
		if (!VertInteractDir.IsNearlyZero())
		{
			if(IsPointInCone(PlaneYZ, InState.Position + VertInteractDir, InState.Position + DestForwardDir))
			{
				RotateAngleRadian = FMath::Acos(FVector::DotProduct(CurForwardDir, VertInteractDir));
			}
			else
			{
				RotateAngleRadian = FMath::Acos(FVector::DotProduct(CurForwardDir, HorizInteractDir));
			}
		}
		OutDiffQuat = FQuat(CrossVec.GetSafeNormal(), RotateAngleRadian);
	}

	///For roll
	bool bRollInDiff = false;
	FQuat CurQuat;
	if (bPitchInDiff && bYawInDiff)
		CurQuat = CurState.Rotation;
	else
		CurQuat = OutDiffQuat * CurState.Rotation;
	//FQuat CurQuat = OutDiffQuat * CurState.Rotation;
	FPlane CurPlaneXY(CurState.Position, CurState.Position + CurQuat.GetRightVector(), CurState.Position + CurQuat.GetUpVector());
	FVector ProjectedUpDir = FPlane::VectorPlaneProject(DestUpDir, FVector(CurPlaneXY.X, CurPlaneXY.Y, CurPlaneXY.Z)).GetSafeNormal();
	float YawDiffAngleRadian = FMath::Acos(FVector::DotProduct(ProjectedUpDir, CurQuat.GetUpVector()));
	bRollInDiff = FMath::RadiansToDegrees(YawDiffAngleRadian) <= YawDiffTolerance;
	if (!bRollInDiff)
	{
		FQuat YawQuat(FVector::CrossProduct(CurQuat.GetUpVector(), ProjectedUpDir).GetSafeNormal(), YawDiffAngleRadian - FMath::DegreesToRadians(YawDiffAngleRadian - 0.1f));
		if (bPitchInDiff && bYawInDiff)
			OutDiffQuat = YawQuat;
		else
			OutDiffQuat = YawQuat * OutDiffQuat;
	}

	return bPitchInDiff || bYawInDiff || bRollInDiff;
	//DrawDebugDirectionalArrow(GetWorld(), InState.Position, InState.Position + DestForwardDir * 1500.f, 50.f, FColor::Blue, false, 5.f);
	//DrawDebugDirectionalArrow(GetWorld(), InState.Position, InState.Position + Pitch2D * 1500.f, 50.f, FColor::Red, false, 5.f);
	//DrawDirectionalArrow()

	//float CosValue = FVector::DotProduct(Pitch2D, DestForwardDir);
	//PrintDebug(TEXT("Adjust Angle:") + FString::SanitizeFloat(FMath::RadiansToDegrees(FMath::Acos(CosValue))));
	//if (FMath::RadiansToDegrees(FMath::Acos(CosValue)) > 20.f)
	//{
	//	
	//	float NewRotAngle = FMath::Lerp(CosValue_Up / FMath::Abs(CosValue_Up) * FMath::RadiansToDegrees(FMath::Acos(CosValue)), 0.f, AngleLerp);
	//	FQuat NewQuat(DestRightDir, FMath::DegreesToRadians(NewRotAngle));
	//	UpdatedComponent->SetWorldRotation(BodyState.Quaternion * NewQuat, false, nullptr, ETeleportType::TeleportPhysics);
	//}
}

bool USVehicleSmoothSyncComponent::IsPointInCone(const FPlane& ComparedPlane, const FVector& StartPoint, const FVector& EndPoint)
{
	const FVector PlaneNormal = FVector( ComparedPlane.X, ComparedPlane.Y, ComparedPlane.Z );
	const FVector PlaneOrigin = PlaneNormal * ComparedPlane.W;
	FVector Direction = (EndPoint - StartPoint).GetSafeNormal();

	const float Distance = FVector::DotProduct( ( PlaneOrigin - (StartPoint + Direction * 0.001f)), PlaneNormal ) / FVector::DotProduct( Direction, PlaneNormal );
	return Distance <= 0.f;
}

FVehicleState USVehicleSmoothSyncComponent::CurveMovement(float DestTime, FVehicleState* PreState, FVehicleState* StartState, FVehicleState* EndState /*= nullptr*/)
{
	FVehicleState VehicleState;
	if (StartState) return VehicleState;
	FRigidBodyState BodyState;
	UpdatedComponent->GetRigidBodyState(BodyState);

	FVector Acceleration = (StartState->LinearVelocity - PreState->LinearVelocity) / (StartState->OwnerTime - PreState->OwnerTime);
	
	return VehicleState;
}

void USVehicleSmoothSyncComponent::PrintDebug(FString&& InString)
{
	if (GetOwnerRole() == ROLE_SimulatedProxy)
	{
		UKismetSystemLibrary::PrintString(this, InString);
	}
}

void USVehicleSmoothSyncComponent::MultiCast_SendStateToAllClient_Implementation(FVehicleState NewState)
{
	if (GetOwnerRole() != ROLE_AutonomousProxy)
		AddState(NewState);
}

void USVehicleSmoothSyncComponent::Server_SendStateToServer_Implementation(FVehicleState NewState)
{
	MultiCast_SendStateToAllClient(NewState);
}

bool USVehicleSmoothSyncComponent::Server_SendStateToServer_Validate(FVehicleState NewState)
{
	return true;
}

