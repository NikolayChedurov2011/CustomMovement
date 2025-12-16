#include "CustomMovementComponent.h"

#if !UE_BUILD_SHIPPING
#include "Engine/Engine.h"
#endif

#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomMovementComponent)

DEFINE_LOG_CATEGORY_STATIC(LogPredictedMovement, Log, All);

namespace PredMovementCVars
{
#if UE_ENABLE_DEBUG_DRAWING
	int32 DrawStaminaValues = 0;
	FAutoConsoleVariableRef CVarDrawStaminaValues(
		TEXT("p.DrawStaminaValues"),
		DrawStaminaValues,
		TEXT("Whether to draw stamina values to screen.\n")
		TEXT("0: Disable, 1: Enable, 2: Enable Local Client Only, 3: Enable Authority Only"),
		ECVF_Default);
#endif
}

UCustomMovementComponent::UCustomMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SetIsReplicatedByDefault(true);

	// Defaults
	GroundFriction = 12.f;  // More grounded, less sliding
	RotationRate.Yaw = 500.f;
	BrakingFrictionFactor = 1.f;
	bUseSeparateBrakingFriction = true;
	PerchRadiusThreshold = 15.f;

	// Walking
	MaxAcceleration = 1300.f;
	MaxWalkSpeed = 260.f;
	BrakingDecelerationWalking = 512.f;
	VelocityCheckMitigatorWalking = 0.98f;

	bWantsToWalk = false;

	// Running
	MaxAccelerationRunning = 1600.f;
	MaxWalkSpeedRunning = 500.f;
	BrakingDecelerationRunning = 1680.f;
	GroundFrictionRunning = 12.f;
	BrakingFrictionRunning = 4.f;
	VelocityCheckMitigatorRunning = 0.98f;
	
	// Sprinting
	bUseMaxAccelerationSprintingOnlyAtSpeed = true;
	MaxAccelerationSprinting = 2400.f;
	MaxWalkSpeedSprinting = 860.f;
	BrakingDecelerationSprinting = 2048.f;
	GroundFrictionSprinting = 12.f;
	BrakingFrictionSprinting = 4.f;

	VelocityCheckMitigatorSprinting = 0.98f;
	bRestrictSprintInputAngle = true;
	SetMaxInputAngleSprint(50.f);

	bWantsToSprint = false;

	// Enable crouch
	NavAgentProps.bCanCrouch = true;

	// Stamina drained scalars
	MaxWalkSpeedScalarStaminaDrained = 0.25f;
	MaxAccelerationScalarStaminaDrained = 0.5f;
	MaxBrakingDecelerationScalarStaminaDrained = 0.5f;

	// Stamina
	BaseMaxStamina = 100.f;
	SetMaxStamina(BaseMaxStamina);
	SprintStaminaDrainRate = 34.f;
	StaminaRegenRate = 20.f;
	StaminaDrainedRegenRate = 10.f;
	bStaminaRecoveryFromPct = true;
	StaminaRecoveryAmount = 20.f;
	StaminaRecoveryPct = 0.2f;
	StartSprintStaminaPct = 0.05f;  // 5% stamina to start sprinting
	
	NetworkStaminaCorrectionThreshold = 2.f;

	// Crouch
	SetCrouchedHalfHeight(54.f);
	
	// Init Modifier Levels
	if (Haste.Num()==0) { Haste.Add(CustomMovementGameplayTags::CustomMovement_Modifier_Haste, { 1.50f }); }		// 50% Speed Haste (Sprinting)
	if (Slow.Num()==0) { Slow.Add(CustomMovementGameplayTags::CustomMovement_Modifier_Slowdown, { 0.50f });	}	 // 50% Speed Slow
	if (SlowFall.Num()==0) { SlowFall.Add(CustomMovementGameplayTags::CustomMovement_Modifier_SlowFall, { 0.1f, EModifierFallZ::Enabled }); }  // 90% Gravity Reduction
}

void UCustomMovementComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, bIsSprinting);
	DOREPLIFETIME(ThisClass, bIsWalking);
	DOREPLIFETIME(ThisClass, HasteLevel);
	DOREPLIFETIME(ThisClass, SlowLevel);
	DOREPLIFETIME(ThisClass, SlowFallLevel);
	DOREPLIFETIME_CONDITION(ThisClass, Stamina, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION(ThisClass, MaxStamina, COND_OwnerOnly);
}

void FCM_NetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	const FPredictedSavedMove& CM = static_cast<const FPredictedSavedMove&>(ClientMove);
	SavedHasteLevel    = CM.HasteLevel;
	SavedSlowLevel     = CM.SlowLevel;
	SavedSlowFallLevel = CM.SlowFallLevel;
}

bool FCM_NetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	bool bSuccess = Super::Serialize(Movement, Ar, PackageMap, MoveType);
	Ar << SavedHasteLevel;
	Ar << SavedSlowLevel;
	Ar << SavedSlowFallLevel;
	return bSuccess && !Ar.IsError();
}

/*-- Haste --*/
void UCustomMovementComponent::SetHasteByTag(const FGameplayTag Tag)
{
	HasteLevel = GetHasteLevelIndex(Tag);

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt((int32)HasteLevel));
}

void UCustomMovementComponent::ClearHaste()
{
	if (HasteLevel == NO_MODIFIER)
		return;

	HasteLevel = NO_MODIFIER;
}

/*void UCustomMovementComponent::ServerSetHasteLevel_Implementation(const uint8 NewLevel)
{
	HasteLevel = NewLevel;
}*/

/*void UCustomMovementComponent::ServerClearHaste_Implementation()
{
	HasteLevel = NO_MODIFIER;
}*/
/*-- End Haste --*/

/*-- Slow --*/
void UCustomMovementComponent::SetSlowByTag(const FGameplayTag Tag)
{
	SlowLevel = GetSlowLevelIndex(Tag);

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt((int32)SlowLevel));
}

void UCustomMovementComponent::ClearSlow()
{
	if (SlowLevel == NO_MODIFIER)
		return;
	
	SlowLevel = NO_MODIFIER;
}

/*void UCustomMovementComponent::ServerSetSlowLevel_Implementation(const uint8 NewLevel)
{
	SlowLevel = NewLevel;
}*/

/*void UCustomMovementComponent::ServerClearSlow_Implementation()
{
	SlowLevel = NO_MODIFIER;
}*/
/*-- End Slow --*/

/*-- Slow falling --*/
void UCustomMovementComponent::SetSlowFallByTag(const FGameplayTag Tag)
{
	SlowFallLevel = GetSlowFallLevelIndex(Tag);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::FromInt((int32)SlowFallLevel));
}

void UCustomMovementComponent::ClearSlowFalling()
{
	if (SlowFallLevel == NO_MODIFIER)
		return;

	SlowFallLevel = NO_MODIFIER;
}

/*void UCustomMovementComponent::ServerSetSlowFallLevel_Implementation(const uint8 NewLevel)
{
	SlowFallLevel = NewLevel;
}*/

/*void UCustomMovementComponent::ServerClearSlowFalling_Implementation()
{
	SlowFallLevel = NO_MODIFIER;
}*/
/*-- End Slow falling --*/


void UCustomMovementComponent::StartSprint()
{
	bWantsToSprint = true;
}

void UCustomMovementComponent::EndSprint()
{
	bWantsToSprint = false;
}

void UCustomMovementComponent::StartWalk()
{
	bWantsToWalk = true;
}

void UCustomMovementComponent::EndWalk()
{
	bWantsToWalk = false;
}

void UCustomMovementComponent::OnRep_IsSprinting()
{
	if (bIsSprinting)
	{
		// Local effects, anim, etc.
	}
	else
	{
		// Disable
	}
}

void UCustomMovementComponent::OnRep_IsWalking()
{
	if (bIsWalking)
	{
		// Local effects, anim, etc.
	}
	else
	{
		// Disable
	}
}

void UCustomMovementComponent::OnRep_HasteLevel()
{
	
}

void UCustomMovementComponent::OnRep_SlowLevel()
{
	
}

void UCustomMovementComponent::OnRep_SlowFallLevel()
{
	
}

void UCustomMovementComponent::OnRep_Stamina()
{
	
}

void UCustomMovementComponent::OnRep_MaxStamina()
{
	
}

#if WITH_EDITOR
void UCustomMovementComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, MaxInputAngleSprint))
	{
		// Compute MaxInputAngleSprint from the Angle.
		SetMaxInputAngleSprint(MaxInputAngleSprint);
	}
	else if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, BaseMaxStamina))
	{
		// Update max stamina
		SetMaxStamina(BaseMaxStamina);
	}
}
#endif

void UCustomMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// Broadcast events to initialize UI, etc.
	OnMaxStaminaChanged(GetMaxStamina(), GetMaxStamina());

	// Set stamina to max
	SetStamina(GetMaxStamina());
}

ECustomMovementGaitMode UCustomMovementComponent::GetGaitMode() const
{
	if (IsSprinting())
	{
		return ECustomMovementGaitMode::Sprint;
	}
	if (IsWalk())
	{
		return ECustomMovementGaitMode::Walk;
	}
	return ECustomMovementGaitMode::Run;
}

/*ECustomMovementGaitMode UCustomMovementComponent::GetGaitSpeed() const
{
	if (IsSprintingInEffect())
	{
		return ECustomMovementGaitMode::Sprint;
	}
	if (IsRunningAtSpeed())
	{
		return ECustomMovementGaitMode::Run;
	}
	if (IsWalkingAtSpeed())
	{
		return ECustomMovementGaitMode::Walk;
	}
	return ECustomMovementGaitMode::Stroll;
}*/

bool UCustomMovementComponent::IsGaitAtSpeed(float Mitigator) const
{
	// When moving on ground we want to factor moving uphill or downhill so variations in terrain
	// aren't culled from the check. When falling, we don't want to factor fall velocity, only lateral
	const float Vel = IsMovingOnGround() ? Velocity.SizeSquared() : Velocity.SizeSquared2D();

	// When struggling to surpass walk speed, which can occur with heavy rotation and low acceleration, we
	// mitigate the check so there isn't a constant re-entry that can occur as an edge case
	return Vel >= FMath::Square(GetBaseMaxSpeed() * GetGaitSpeedFactor()) * Mitigator;
}

bool UCustomMovementComponent::IsWalk() const
{
	return bIsWalking;
}

bool UCustomMovementComponent::IsRunning() const
{
	// We're running if we're not walking, sprinting, etc.
	return !GetOwner() || (!IsWalk() && !IsSprinting());
}

float UCustomMovementComponent::GetGaitSpeedFactor() const
{
	/*
	 * Infinite recursion protection to avoid stack overflow -- we must exclude Haste from speed checks
	 * e.g. IsSprintWithinAllowableInputAngle() ➜ IsSprintingAtSpeed() ➜ GetMaxSpeed() ➜ GetMaxSpeedScalar() ➜ IsSprintingInEffect() ➜ IsSprintWithinAllowableInputAngle()
	 */
	
	const float StaminaDrained = IsStaminaDrained() ? MaxWalkSpeedScalarStaminaDrained : 1.f;
	const float SlowScalar = GetSlowSpeedScalar();
	return StaminaDrained * SlowScalar;
}

float UCustomMovementComponent::GetMaxAccelerationScalar() const
{
	const float StaminaDrained = IsStaminaDrained() ? MaxAccelerationScalarStaminaDrained : 1.f;
	const float SlowScalar = GetSlowAccelScalar();
	const float HasteScalar = GetHasteAccelScalar(); //IsSprintingInEffect() ? GetHasteAccelScalar() : 1.f;
	return StaminaDrained * SlowScalar * HasteScalar;
}

float UCustomMovementComponent::GetMaxSpeedScalar() const
{
	const float StaminaDrained = IsStaminaDrained() ? MaxWalkSpeedScalarStaminaDrained : 1.f;
	const float SlowScalar = GetSlowSpeedScalar();
	const float HasteScalar = GetHasteSpeedScalar(); //IsSprintingInEffect() ? GetHasteSpeedScalar() : 1.f;
	return StaminaDrained * SlowScalar * HasteScalar;
}

float UCustomMovementComponent::GetMaxBrakingDecelerationScalar() const
{
	const float StaminaDrained = IsStaminaDrained() ? MaxBrakingDecelerationScalarStaminaDrained : 1.f;
	const float SlowScalar = GetSlowBrakingScalar();
	const float HasteScalar = GetHasteBrakingScalar(); //IsSprintingInEffect() ? GetHasteBrakingScalar() : 1.f;
	return StaminaDrained * SlowScalar * HasteScalar;
}

float UCustomMovementComponent::GetGroundFrictionScalar() const
{
	const float SlowScalar = GetSlowGroundFrictionScalar();
	const float HasteScalar = GetHasteGroundFrictionScalar(); //IsSprintingInEffect() ? GetHasteGroundFrictionScalar() : 1.f;
	return SlowScalar * HasteScalar;
}

float UCustomMovementComponent::GetBrakingFrictionScalar() const
{
	const float SlowScalar = GetSlowBrakingFrictionScalar();
	const float HasteScalar = GetHasteBrakingFrictionScalar(); //IsSprintingInEffect() ? GetHasteBrakingFrictionScalar() : 1.f;
	return SlowScalar * HasteScalar;
}

float UCustomMovementComponent::GetGravityZScalar() const
{
	const float SlowFallScalar = GetSlowFallGravityZScalar();
	return SlowFallScalar;
}

float UCustomMovementComponent::GetRootMotionTranslationScalar() const
{
	// Allowing boost to affect root motion will increase attack range, dodge range, etc., it is disabled by default
	const float SlowScalar = SlowAffectsRootMotion() ? GetSlowSpeedScalar() : 1.f;
	return SlowScalar;
}

float UCustomMovementComponent::GetBaseMaxAcceleration() const
{
	if (IsFlying())		{ return MaxAccelerationRunning; }
	if (IsSwimming())	{ return MaxAccelerationRunning; }

	if (IsSprintingInEffect())
	{
		return MaxAccelerationSprinting;
	}

	if (!bUseMaxAccelerationSprintingOnlyAtSpeed && IsSprinting() && IsSprintWithinAllowableInputAngle())
	{
		return MaxAccelerationSprinting;
	}

	const ECustomMovementGaitMode GaitMode = GetGaitMode();
	switch (GaitMode)
	{
		case ECustomMovementGaitMode::Walk:
			return MaxAcceleration;
		case ECustomMovementGaitMode::Run:
		case ECustomMovementGaitMode::Sprint:
			return MaxAccelerationRunning;
	}
	return 0.f;
}

float UCustomMovementComponent::GetBaseMaxSpeed() const
{
	if (IsFlying())		{ return MaxFlySpeed; }
	if (IsSwimming())	{ return MaxSwimSpeed; }
	if (IsCrouching())	{ return MaxWalkSpeedCrouched; }
	if (MovementMode == MOVE_Custom) { return MaxCustomMovementSpeed; }

	const ECustomMovementGaitMode GaitMode = GetGaitMode();
	switch (GaitMode)
	{
		case ECustomMovementGaitMode::Walk: return MaxWalkSpeed;
		case ECustomMovementGaitMode::Run: return MaxWalkSpeedRunning;
		case ECustomMovementGaitMode::Sprint: return MaxWalkSpeedSprinting;
	}
	return 0.f;
}

float UCustomMovementComponent::GetBaseMaxBrakingDeceleration() const
{
	if (IsFlying()) { return BrakingDecelerationFlying; }
	if (IsFalling()) { return BrakingDecelerationFalling; }
	if (IsSwimming()) { return BrakingDecelerationSwimming; }

	const ECustomMovementGaitMode GaitMode = GetGaitMode();
	switch (GaitMode)
	{
		case ECustomMovementGaitMode::Walk: return BrakingDecelerationWalking;
		case ECustomMovementGaitMode::Run: return BrakingDecelerationRunning;
		case ECustomMovementGaitMode::Sprint: return BrakingDecelerationSprinting;
	}
	return 0.f;
}

float UCustomMovementComponent::GetBaseGroundFriction(float DefaultGroundFriction) const
{
	// This function is already gated by IsMovingOnGround() when called

	const ECustomMovementGaitMode GaitMode = GetGaitMode();
	switch (GaitMode)
	{
		case ECustomMovementGaitMode::Walk: return DefaultGroundFriction;
		case ECustomMovementGaitMode::Run: return GroundFrictionRunning;
		case ECustomMovementGaitMode::Sprint: return GroundFrictionSprinting;
	}

	return DefaultGroundFriction;
}

float UCustomMovementComponent::GetBaseBrakingFriction() const
{
	// This function is already gated by IsMovingOnGround() when called

	const ECustomMovementGaitMode GaitMode = GetGaitMode();
	switch (GaitMode)
	{
		case ECustomMovementGaitMode::Walk: return BrakingFriction;
		case ECustomMovementGaitMode::Run: return BrakingFrictionRunning;
		case ECustomMovementGaitMode::Sprint: return BrakingFrictionSprinting;
	}
	
	return BrakingFriction;
}

float UCustomMovementComponent::GetGravityZ() const
{
	return Super::GetGravityZ() * GetGravityZScalar();
}

FVector UCustomMovementComponent::GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	// Slow fall air control
	if (const FFallingModifierParams* Params = GetSlowFallParams())
	{
		TickAirControl = Params->GetAirControl(TickAirControl);
	}
	
	return Super::GetAirControl(DeltaTime, TickAirControl, FallAcceleration);
}

void UCustomMovementComponent::CalcStamina(float DeltaTime)
{
	// Do not update velocity when using root motion or when SimulatedProxy and not simulating root motion - SimulatedProxy are repped their Velocity
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME || (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && !bWasSimulatingRootMotion))
	{
		return;
	}
	
	if (IsSprintingInEffect())
	{
		SetStamina(GetStamina() - SprintStaminaDrainRate * DeltaTime);
	}
	else
	{
		const float RegenRate = IsStaminaDrained() ? StaminaDrainedRegenRate : StaminaRegenRate;
		SetStamina(GetStamina() + RegenRate * DeltaTime);
	}
}

void UCustomMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (IsMovingOnGround())
	{
		Friction = GetGroundFriction(Friction);
	}
	
	CalcStamina(DeltaTime);
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UCustomMovementComponent::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
{
	if (IsMovingOnGround())
	{
		Friction = bUseSeparateBrakingFriction ? GetBrakingFriction() : GetGroundFriction(Friction);
	}
	Super::ApplyVelocityBraking(DeltaTime, Friction, BrakingDeceleration);
}

bool UCustomMovementComponent::CanWalkOffLedges() const
{
	return Super::CanWalkOffLedges();
}

bool UCustomMovementComponent::CanAttemptJump() const
{
	// Cannot jump if not allowed
	if (!IsJumpAllowed())
	{
		return false;
	}

	// Can only jump from valid movement modes
	if (!IsMovingOnGround() && !IsFalling())
	{
		return false;
	}

	return true;
}

void UCustomMovementComponent::Walk()
{
	if (!HasValidData())
	{
		return;
	}
	
	if (!CanWalkInCurrentState())
	{
		return;
	}

	if (IsSprinting())
	{
		UnSprint();
	}
	
	/*if (IsWalk())
	{
		UnWalk();
	}*/

	// Client side
	bIsWalking = true;
}

void UCustomMovementComponent::UnWalk()
{
	if (!HasValidData())
	{
		return;
	}

	bIsWalking = false;
}

bool UCustomMovementComponent::CanWalkInCurrentState() const
{
	if (!UpdatedComponent || UpdatedComponent->IsSimulatingPhysics())
	{
		return false;
	}

	if (!IsFalling() && !IsMovingOnGround())
	{
		// Can only enter walk in either MOVE_Falling or MOVE_Walking or MOVE_NavWalking
		return false;
	}

	return true;
}

void UCustomMovementComponent::SetMaxInputAngleSprint(float InMaxAngleSprint)
{
	MaxInputAngleSprint = FMath::Clamp(InMaxAngleSprint, 0.f, 180.0f);
	MaxInputNormalSprint = FMath::Cos(FMath::DegreesToRadians(MaxInputAngleSprint));
}

bool UCustomMovementComponent::IsSprinting() const
{
	return bIsSprinting;
}

void UCustomMovementComponent::Sprint()
{
	if (!HasValidData())
	{
		return;
	}
	
	if (!CanSprintInCurrentState())
	{
		return;
	}

	if (IsCrouching())
	{
		UnCrouch();
	}

	if (IsWalk())
	{
		UnWalk();
	}

	// Client side
	bIsSprinting = true;
}

void UCustomMovementComponent::UnSprint()
{
	if (!HasValidData())
	{
		return;
	}

	bIsSprinting = false;
}

bool UCustomMovementComponent::CanSprintInCurrentState() const
{
	if (!UpdatedComponent || UpdatedComponent->IsSimulatingPhysics())
	{
		return false;
	}

	// Cannot sprint if stamina is drained
	if (IsStaminaDrained())
	{
		return false;
	}

	// Cannot sprint if stamina is at 0
	if (GetStaminaPct() <= 0.f)
	{
		return false;
	}

	// Cannot start to sprint if stamina is below threshold
	if (!IsSprinting() && GetStaminaPct() < StartSprintStaminaPct)
	{
		return false;
	}

	// Cannot sprint if in an invalid movement mode
	if (!IsFalling() && !IsMovingOnGround())
	{
		return false;
	}

	if (IsCrouching())
	{
		return false;
	}

	return true;
}

bool UCustomMovementComponent::IsSprintWithinAllowableInputAngle() const
{
	if (!UpdatedComponent)
	{
		return false;
	}
	
	if (!bRestrictSprintInputAngle || MaxInputAngleSprint <= 0.f)
	{
		return true;
	}
	
	// This check ensures that we are not sprinting backward or sideways, while allowing leeway 
	// This angle allows sprinting when holding forward, forward left, forward right
	// but not left or right or backward
	const float Dot = GetCurrentAcceleration().GetSafeNormal2D() | UpdatedComponent->GetForwardVector();
	return Dot >= MaxInputNormalSprint;
}

void UCustomMovementComponent::SetStamina(float NewStamina)
{
	const float PrevStamina = Stamina;
	Stamina = FMath::Clamp(NewStamina, 0.f, MaxStamina);
	if (CharacterOwner != nullptr)
	{
		if (!FMath::IsNearlyEqual(PrevStamina, Stamina))
		{
			OnStaminaChanged(PrevStamina, Stamina);
		}
	}
}

void UCustomMovementComponent::SetMaxStamina(float NewMaxStamina)
{
	const float PrevMaxStamina = MaxStamina;
	MaxStamina = FMath::Max(0.f, NewMaxStamina);
	if (CharacterOwner != nullptr)
	{
		if (!FMath::IsNearlyEqual(PrevMaxStamina, MaxStamina))
		{
			OnMaxStaminaChanged(PrevMaxStamina, MaxStamina);
		}
	}
}

void UCustomMovementComponent::SetStaminaDrained(bool bNewValue)
{
	const bool bWasStaminaDrained = bStaminaDrained;
	bStaminaDrained = bNewValue;
	if (CharacterOwner != nullptr)
	{
		if (bWasStaminaDrained != bStaminaDrained)
		{
			if (bStaminaDrained)
			{
				OnStaminaDrained();
			}
			else
			{
				OnStaminaDrainRecovered();
			}
		}
	}
}

void UCustomMovementComponent::OnStaminaChanged(float PrevValue, float NewValue)
{
	if (IsValid(GetOwner()))
	{
		// TODO:
		//GetOwner()->OnStaminaChanged(NewValue, PrevValue);
	}
	
	if (FMath::IsNearlyZero(Stamina))
	{
		Stamina = 0.f;
		if (!bStaminaDrained)
		{
			SetStaminaDrained(true);
		}
	}
	else if (bStaminaDrained && IsStaminaRecovered())
	{
		SetStaminaDrained(false);
	}
	else if (FMath::IsNearlyEqual(Stamina, MaxStamina))
	{
		Stamina = MaxStamina;
		if (bStaminaDrained)
		{
			SetStaminaDrained(false);
		}
	}
}

void UCustomMovementComponent::OnMaxStaminaChanged(float PrevValue, float NewValue)
{
	if (IsValid(GetOwner()))
	{
		// TODO:
		//GetOwner()->OnMaxStaminaChanged(NewValue, PrevValue);
	}

	// Ensure that Stamina is within the new MaxStamina
	SetStamina(GetStamina());
}

void UCustomMovementComponent::OnStaminaDrained()
{
	if (IsValid(GetOwner()))
	{
		//GetOwner()->OnStaminaDrained();
	}
}

void UCustomMovementComponent::OnStaminaDrainRecovered()
{
	if (IsValid(GetOwner()))
	{
		//GetOwner()->OnStaminaDrainRecovered();
	}
}

bool UCustomMovementComponent::CanHasteInCurrentState() const
{
	return UpdatedComponent && !UpdatedComponent->IsSimulatingPhysics() && (IsFalling() || IsMovingOnGround());
}

bool UCustomMovementComponent::CanSlowInCurrentState() const
{
	return UpdatedComponent && !UpdatedComponent->IsSimulatingPhysics() && (IsFalling() || IsMovingOnGround());
}

bool UCustomMovementComponent::CanSlowFallInCurrentState() const
{
	return UpdatedComponent && !UpdatedComponent->IsSimulatingPhysics() && (IsFalling() || IsMovingOnGround());
}

bool UCustomMovementComponent::RemoveVelocityZOnSlowFallStart() const
{
	if (IsMovingOnGround())
	{
		return false;
	}
	
	// Optionally clear Z velocity if slow fall is active
	const EModifierFallZ RemoveVelocityZ = GetSlowFallParams() ?
		GetSlowFallParams()->RemoveVelocityZOnStart : EModifierFallZ::Disabled;
		
	switch (RemoveVelocityZ)
	{
	case EModifierFallZ::Disabled:
		return false;
	case EModifierFallZ::Enabled:
		return true;
	case EModifierFallZ::Falling:
		return Velocity.Z < 0.f;
	case EModifierFallZ::Rising:
		return Velocity.Z > 0.f;
	}

	return false;
}

void UCustomMovementComponent::UpdateModifierMovementState()
{
	if (!HasValidData())
	{
		return;
	}
	
	// Initialize Modifier levels if empty
	if (HasteLevels.Num() == 0)	{ for (const auto& Level : Haste) { HasteLevels.Add(Level.Key); } }
	if (SlowLevels.Num() == 0)	{ for (const auto& Level : Slow) { SlowLevels.Add(Level.Key); } }
	if (SlowFallLevels.Num() == 0) { for (const auto& Level : SlowFall) { SlowFallLevels.Add(Level.Key); } }
}

void UCustomMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	
	if (!HasValidData())
	{
		return;
	}

	// Detect when slow fall starts
	const bool bWasSlowFalling = IsSlowFallActive();

	// Update movement modifiers
	UpdateModifierMovementState();
	
	if (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		// Optionally clear Z velocity if slow fall just started
		if (!bWasSlowFalling && IsSlowFallActive() && RemoveVelocityZOnSlowFallStart())
		{
			Velocity.Z = 0.f;
		}
		
		/* We can't sprint if we're prone, we must clear input in the character */
		
		// Check for a change in Sprint state. Players toggle Sprint by changing bWantsToSprint.
		const bool bCheckIsSprinting = IsSprinting();
		if (bCheckIsSprinting && (!bWantsToSprint || !CanSprintInCurrentState()))
		{
			UnSprint();
		}
		else if (!bCheckIsSprinting && bWantsToSprint && CanSprintInCurrentState())
		{
			Sprint();
		}
		
		// Check for a change in Walk state. Players toggle Walk by changing bWantsToWalk.
		const bool bCheckIsWalking = IsWalk();
		if (bCheckIsWalking && (!bWantsToWalk || !CanWalkInCurrentState()))
		{
			UnWalk();
		}
		else if (!bCheckIsWalking && bWantsToWalk && CanWalkInCurrentState())
		{
			Walk();
		}
		
		// Check for a change in crouch state. Players toggle crouch by changing bWantsToCrouch.
		const bool bCheckIsCrouching = IsCrouching();
		if (bCheckIsCrouching && (!bWantsToCrouch || !CanCrouchInCurrentState()))
		{
			UnCrouch(false);
		}
		else if (!bCheckIsCrouching && bWantsToCrouch && CanCrouchInCurrentState())
		{
			// Potential prone lock
			Crouch(false);
		}
	}
}

void UCustomMovementComponent::UpdateCharacterStateAfterMovement(float DeltaSeconds)
{
	//UpdateModifierMovementState();
	
	if (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		// UnSprint if no longer allowed to be Sprinting
		if (IsSprinting() && !CanSprintInCurrentState())
		{
			UnSprint();
		}
	}

	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	
#if UE_ENABLE_DEBUG_DRAWING
	// Draw Stamina values to Screen
	if (GEngine && PredMovementCVars::DrawStaminaValues > 0)
	{
		const uint32 DebugKey = (CharacterOwner->GetUniqueID() + 74290) % UINT32_MAX;
		if (CharacterOwner->HasAuthority() && (PredMovementCVars::DrawStaminaValues == 1 || PredMovementCVars::DrawStaminaValues == 3))
		{
			const uint64 AuthDebugKey = DebugKey + 1;
			GEngine->AddOnScreenDebugMessage(AuthDebugKey, 1.f, FColor::Orange, FString::Printf(TEXT("[Authority] Stamina %f    Drained %d"), GetStamina(), IsStaminaDrained()));
		}
		else if (CharacterOwner->IsLocallyControlled() && (PredMovementCVars::DrawStaminaValues == 1 || PredMovementCVars::DrawStaminaValues == 2))
		{
			const uint64 LocalDebugKey = DebugKey + 2;
			GEngine->AddOnScreenDebugMessage(LocalDebugKey, 1.f, FColor::Yellow, FString::Printf(TEXT("[Local] Stamina %f    Drained %d"), GetStamina(), IsStaminaDrained()));
		}
	}
#endif
}

bool UCustomMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& ClientWorldLocation, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	// ServerMovePacked_ServerReceive ➜ ServerMove_HandleMoveData ➜ ServerMove_PerformMovement
	// ➜ ServerMoveHandleClientError ➜ ServerCheckClientError
	
	if (Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))
	{
		return true;
	}

	return false;
}

void UCustomMovementComponent::OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData,
	float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName,
	bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	// This occurs on AutonomousProxy, when the server sends the move response
	// This is where we receive the snare, and can override the server's location, assuming it has given us authority

	// Server >> SendClientAdjustment() ➜ ServerSendMoveResponse() ➜ ServerFillResponseData() + MoveResponsePacked_ServerSend() >> Client
	// >> ClientMoveResponsePacked() ➜ ClientHandleMoveResponse() ➜ ClientAdjustPosition_Implementation() ➜ OnClientCorrectionReceived()
	
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName,
		bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

void UCustomMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	/*const bool bShouldPullFromMoveData = (CharacterOwner != nullptr) && (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy);

	if (bShouldPullFromMoveData)
	{
		if (FCharacterNetworkMoveData* CurrentMoveData = GetCurrentNetworkMoveData())
		{
			if (FCM_NetworkMoveData* CM = static_cast<FCM_NetworkMoveData*>(CurrentMoveData))
			{
				HasteLevel    = CM->SavedHasteLevel;
				SlowLevel     = CM->SavedSlowLevel;
				SlowFallLevel = CM->SavedSlowFallLevel;
			}
		}
	}*/

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UCustomMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsToSprint = (Flags & FPredictedSavedMove::FLAG_Sprint) != 0;
	bWantsToWalk = (Flags & FPredictedSavedMove::FLAG_Walk) != 0;
}

uint8 FPredictedSavedMove::GetCompressedFlags() const
{
	uint8 Result = FSavedMove_Character::GetCompressedFlags();

	if (bWantsToSprint) Result |= FLAG_Sprint;
	if (bWantsToWalk) Result |= FLAG_Walk;

	return Result;
}

/*uint8 FPredictedSavedMove::GetCompressedFlagsExtra() const
{
	uint8 Result = 0;

	if (bWantsToWalk)
	{
		Result |= FLAGEX_Walk;
	}
	
	if (bWantsToSprint)
	{
		Result |= FLAGEX_Sprint;
	}

	return Result;
}*/

void FPredictedSavedMove::Clear()
{
	Super::Clear();

	bWantsToWalk = false;
	bWantsToSprint = false;
	
	bStaminaDrained = false;
	StartStamina = 0.f;
	EndStamina = 0.f;

	HasteLevel = NO_MODIFIER;
	SlowLevel = NO_MODIFIER;
	SlowFallLevel = NO_MODIFIER;
}

bool FPredictedSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter,
	float MaxDelta) const
{
	// We combine moves for the purpose of reducing the number of moves sent to the server, especially when exceeding
	// 60 fps (by default, see ClientNetSendMoveDeltaTime).
	// By combining moves, we can send fewer moves, but still have the same outcome.
	
	// If we didn't handle move combining, and then we used OnStartSprint() to modify our Velocity directly, it would
	// de-sync if we exceed 60fps. This is where move combining kicks in and starts using Pending Moves instead.
	
	// When combining moves, the PendingMove is passed into the NewMove. Locally, before sending a Move to the Server,
	// the AutonomousProxy Client will already have processed the current PendingMove (it's only pending for being sent,
	// not processed).

	// Since combining will happen before processing a move, PendingMove might end up being processed twice; once last
	// frame, and once as part of the new combined move.
	
	const TSharedPtr<FPredictedSavedMove>& SavedMove = StaticCastSharedPtr<FPredictedSavedMove>(NewMove);

	if (bStaminaDrained != SavedMove->bStaminaDrained)
	{
		return false;
	}
	
	if (bWantsToWalk != SavedMove->bWantsToWalk) { return false; }
	if (bWantsToSprint != SavedMove->bWantsToSprint) { return false; }

	// Without these, the change/start/stop events will trigger twice causing de-sync, so we don't combine moves if the level changes
	if (HasteLevel != SavedMove->HasteLevel) { return false; }
	if (SlowLevel != SavedMove->SlowLevel) { return false; }
	if (SlowFallLevel != SavedMove->SlowFallLevel) { return false; }
	
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FPredictedSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bWantsToWalk = MoveComp->bWantsToWalk;
		bWantsToSprint = MoveComp->bWantsToSprint;

		HasteLevel = MoveComp->HasteLevel;
		SlowLevel = MoveComp->SlowLevel;
		SlowFallLevel = MoveComp->SlowFallLevel;

		StartStamina = MoveComp->GetStamina();
		bStaminaDrained = MoveComp->IsStaminaDrained();
	}
}

void FPredictedSavedMove::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	
	if (UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		MoveComp->bWantsToWalk = bWantsToWalk;
		MoveComp->bWantsToSprint = bWantsToSprint;

		MoveComp->HasteLevel = HasteLevel;
		MoveComp->SlowLevel = SlowLevel;
		MoveComp->SlowFallLevel = SlowFallLevel;

		MoveComp->SetStamina(StartStamina);
		MoveComp->SetStaminaDrained(bStaminaDrained);
	}
}

void FPredictedSavedMove::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	FSavedMove_Character::PostUpdate(C, PostUpdateMode);

	if (const UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		EndStamina = MoveComp->GetStamina();
		bStaminaDrained = MoveComp->IsStaminaDrained();
	}
}

void FPredictedSavedMove::CombineWith(const FSavedMove_Character* OldMove, ACharacter* C, APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, C, PC, OldStartLocation);

	const FPredictedSavedMove* SavedOldMove = static_cast<const FPredictedSavedMove*>(OldMove);

	if (UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		MoveComp->SetStamina(SavedOldMove->StartStamina);
		MoveComp->SetStaminaDrained(SavedOldMove->bStaminaDrained);
		
		MoveComp->HasteLevel = SavedOldMove->HasteLevel;
		MoveComp->SlowLevel = SavedOldMove->SlowLevel;
		MoveComp->SlowFallLevel = SavedOldMove->SlowFallLevel;
	}
}

FSavedMovePtr FPredictedNetworkPredictionData_Client::AllocateNewMove()
{
	return MakeShared<FPredictedSavedMove>();
}

void UCustomMovementComponent::TickCharacterPose(float DeltaTime)
{
	/*
	 * ACharacter::GetAnimRootMotionTranslationScale() is non-virtual, so we have to duplicate the entire function.
	 * All we do here is scale CharacterOwner->GetAnimRootMotionTranslationScale() by GetRootMotionTranslationScalar()
	 * 
	 * This allows our snares to affect root motion.
	 */
	
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	check(CharacterOwner && CharacterOwner->GetMesh());
	USkeletalMeshComponent* CharacterMesh = CharacterOwner->GetMesh();

	// bAutonomousTickPose is set, we control TickPose from the Character's Movement and Networking updates, and bypass the Component's update.
	// (Or Simulating Root Motion for remote clients)
	CharacterMesh->bIsAutonomousTickPose = true;

	if (CharacterMesh->ShouldTickPose())
	{
		// Keep track of if we're playing root motion, just in case the root motion montage ends this frame.
		const bool bWasPlayingRootMotion = CharacterOwner->IsPlayingRootMotion();

		CharacterMesh->TickPose(DeltaTime, true);

		// Grab root motion now that we have ticked the pose
		if (CharacterOwner->IsPlayingRootMotion() || bWasPlayingRootMotion)
		{
			FRootMotionMovementParams RootMotion = CharacterMesh->ConsumeRootMotion();
			if (RootMotion.bHasRootMotion)
			{
				RootMotion.ScaleRootMotionTranslation(CharacterOwner->GetAnimRootMotionTranslationScale() * GetRootMotionTranslationScalar());
				RootMotionParams.Accumulate(RootMotion);
			}

#if !(UE_BUILD_SHIPPING)
			// Debugging
			{
				const FAnimMontageInstance* RootMotionMontageInstance = CharacterOwner->GetRootMotionAnimMontageInstance();
				UE_LOG(LogRootMotion, Log, TEXT("UCharacterMovementComponent::TickCharacterPose Role: %s, RootMotionMontage: %s, MontagePos: %f, DeltaTime: %f, ExtractedRootMotion: %s, AccumulatedRootMotion: %s")
					, *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), CharacterOwner->GetLocalRole())
					, *GetNameSafe(RootMotionMontageInstance ? RootMotionMontageInstance->Montage : NULL)
					, RootMotionMontageInstance ? RootMotionMontageInstance->GetPosition() : -1.f
					, DeltaTime
					, *RootMotion.GetRootMotionTransform().GetTranslation().ToCompactString()
					, *RootMotionParams.GetRootMotionTransform().GetTranslation().ToCompactString()
					);
			}
#endif // !(UE_BUILD_SHIPPING)
		}
	}

	CharacterMesh->bIsAutonomousTickPose = false;
}

FNetworkPredictionData_Client* UCustomMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UCustomMovementComponent* MutableThis = const_cast<UCustomMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FPredictedNetworkPredictionData_Client(*this);
	}

	return ClientPredictionData;
}
