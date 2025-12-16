#include "CustomMovementComponent.h"

#if !UE_BUILD_SHIPPING
#include "Engine/Engine.h"
#endif

#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "Tags/CM_GameplayTags.h"

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

#if !UE_BUILD_SHIPPING
	static bool bClientAuthDisabled = false;
	FAutoConsoleVariableRef CVarClientAuthDisabled(
		TEXT("p.ClientAuth.Disabled"),
		bClientAuthDisabled,
		TEXT("Override client authority to disabled.\n")
		TEXT("If true, disable client authority"),
		ECVF_Default);
#endif
}

UCustomMovementComponent::UCustomMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetNetworkMoveDataContainer(PredMoveDataContainer);
	SetMoveResponseDataContainer(PredMoveResponseDataContainer);

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

void FPredictedMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	// Server ➜ Client

	// Server >> APlayerController::SendClientAdjustment() ➜ SendClientAdjustment ➜ ServerSendMoveResponse ➜
	// ServerFillResponseData ➜ MoveResponsePacked_ServerSend >> Client
	
	const UCustomMovementComponent* MoveComp = Cast<UCustomMovementComponent>(&CharacterMovement);

	// Stamina
	bStaminaDrained = MoveComp->IsStaminaDrained();
	Stamina = MoveComp->GetStamina();

	// Fill the response data with the current modifier state
	HasteCorrection.ServerFillResponseData(MoveComp->HasteCorrection.Modifiers);
	SlowCorrection.ServerFillResponseData(MoveComp->SlowCorrection.Modifiers);
	SlowFallCorrection.ServerFillResponseData(MoveComp->SlowFallCorrection.Modifiers);

	// Fill ClientAuthAlpha
	ClientAuthAlpha = MoveComp->ClientAuthAlpha;
	bHasClientAuthAlpha = ClientAuthAlpha > 0.f;
}

bool FPredictedMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}

	// Server ➜ Client
	if (IsCorrection())
	{
		// Serialize Stamina
		Ar << Stamina;
		Ar << bStaminaDrained;

		// Serialize Modifiers
		Ar << HasteCorrection.Modifiers;
		Ar << SlowCorrection.Modifiers;
		Ar << SlowFallCorrection.Modifiers;

		// Serialize ClientAuthAlpha
		Ar.SerializeBits(&bHasClientAuthAlpha, 1);
		if (bHasClientAuthAlpha)
		{
			Ar << ClientAuthAlpha;
		}
		else if (!Ar.IsSaving())
		{
			ClientAuthAlpha = 0.f;
		}
	}

	return !Ar.IsError();
}

void FPredictedNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	// Client packs move data to send to the server
	// Use this instead of GetCompressedFlags()
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);
	
	// Client ➜ Server
	
	// CallServerMovePacked ➜ ClientFillNetworkMoveData ➜ ServerMovePacked_ClientSend >> Server
	// >> ServerMovePacked_ServerReceive ➜ ServerMove_HandleMoveData ➜ ServerMove_PerformMovement
	// ➜ MoveAutonomous (UpdateFromCompressedFlags)

	const FPredictedSavedMove& SavedMove = static_cast<const FPredictedSavedMove&>(ClientMove);

	// Compressed flags
	CompressedMoveFlagsExtra = SavedMove.GetCompressedFlagsExtra();
	
	// Stamina
	Stamina = SavedMove.EndStamina;
	
	// Fill the Modifier data from the saved move
	HasteLocal.ClientFillNetworkMoveData(SavedMove.HasteLocal.WantsModifiers);
	HasteCorrection.ClientFillNetworkMoveData(SavedMove.HasteCorrection.WantsModifiers, SavedMove.HasteCorrection.Modifiers);
	SlowLocal.ClientFillNetworkMoveData(SavedMove.SlowLocal.WantsModifiers);
	SlowCorrection.ClientFillNetworkMoveData(SavedMove.SlowCorrection.WantsModifiers, SavedMove.SlowCorrection.Modifiers);
	SlowFallLocal.ClientFillNetworkMoveData(SavedMove.SlowFallLocal.WantsModifiers);
	SlowFallCorrection.ClientFillNetworkMoveData(SavedMove.SlowFallCorrection.WantsModifiers, SavedMove.SlowFallCorrection.Modifiers);
}

bool FPredictedNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	Super::Serialize(Movement, Ar, PackageMap, MoveType);

	// Client ➜ Server

	// Compressed flags
	SerializeOptionalValue<uint8>(Ar.IsSaving(), Ar, CompressedMoveFlagsExtra, 0);

	// Stamina
	SerializeOptionalValue<float>(Ar.IsSaving(), Ar, Stamina, 0.f);
	
	// Serialize Modifier data=
	HasteLocal.Serialize(Ar, TEXT("HasteLocal"));
	HasteCorrection.Serialize(Ar, TEXT("HasteCorrection"));
	SlowLocal.Serialize(Ar, TEXT("SlowLocal"));
	SlowCorrection.Serialize(Ar, TEXT("SlowCorrection"));
	SlowFallLocal.Serialize(Ar, TEXT("SlowFallLocal"));
	SlowFallCorrection.Serialize(Ar, TEXT("SlowFallCorrection"));

	return !Ar.IsError();
}

/*-- Haste --*/
void UCustomMovementComponent::SetHasteByTag(const FGameplayTag Tag)
{
	const uint8 Level = GetHasteLevelIndex(Tag);
	if (Level != NO_MODIFIER)
	{
		HasteCorrection.AddModifier(Level);
	}
}

void UCustomMovementComponent::ClearHaste()
{
	HasteCorrection.ResetModifiers();
}
/*-- End Haste --*/

/*-- Slow --*/
void UCustomMovementComponent::SetSlowByTag(const FGameplayTag Tag)
{
	const uint8 Level = GetSlowLevelIndex(Tag);
	if (Level != NO_MODIFIER)
	{
		SlowCorrection.AddModifier(Level);
	}
}

void UCustomMovementComponent::ClearSlow()
{
	SlowCorrection.ResetModifiers();
}
/*-- End Slow --*/

/*-- Slow falling --*/
void UCustomMovementComponent::SetSlowFallByTag(const FGameplayTag Tag)
{
	const uint8 Level = GetSlowFallLevelIndex(Tag);
	if (Level != NO_MODIFIER)
	{
		SlowFallCorrection.AddModifier(Level);
	}
}

void UCustomMovementComponent::ClearSlowFalling()
{
	SlowFallCorrection.ResetModifiers();
}
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

void UCustomMovementComponent::ProcessModifierMovementState()
{
	// Proxies get replicated Modifier state.
	if (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		// Haste
		{
			TArray<FMovementModifier*> HasteMods = { &HasteCorrection };
			FModifierStatics::ProcessModifiers(HasteLevel, HasteLevelMethod, HasteLevels, bLimitMaxHastes, MaxHastes, NO_MODIFIER, HasteMods,
				[this](){ return CanHasteInCurrentState(); });
		}

		// Slow
		{
			TArray<FMovementModifier*> SlowMods = { &SlowCorrection };
			FModifierStatics::ProcessModifiers(SlowLevel, SlowLevelMethod, SlowLevels, bLimitMaxSlows, MaxSlows, NO_MODIFIER, SlowMods,
				[this](){ return CanSlowInCurrentState(); });
		}

		// SlowFall
		{
			TArray<FMovementModifier*> SlowFallMods = { &SlowFallCorrection };
			FModifierStatics::ProcessModifiers(SlowFallLevel, SlowFallLevelMethod, SlowFallLevels, bLimitMaxSlowFalls, MaxSlowFalls, NO_MODIFIER, SlowFallMods,
				[this](){ return CanSlowFallInCurrentState(); });
		}
	}
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

	// Update the modifiers
	ProcessModifierMovementState();
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

void UCustomMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	// Server updates from the client's move data
	// Use this instead of UpdateFromCompressedFlags()

	// Client >> CallServerMovePacked ➜ ClientFillNetworkMoveData ➜ ServerMovePacked_ClientSend >> Server
	// >> ServerMovePacked_ServerReceive ➜ ServerMove_HandleMoveData ➜ ServerMove_PerformMovement
	
	const FPredictedNetworkMoveData& PredMoveData = static_cast<const FPredictedNetworkMoveData&>(MoveData);

	HasteCorrection.ServerMove_PerformMovement(PredMoveData.HasteCorrection.WantsModifiers);
	SlowCorrection.ServerMove_PerformMovement(PredMoveData.SlowCorrection.WantsModifiers);
	SlowFallCorrection.ServerMove_PerformMovement(PredMoveData.SlowFallCorrection.WantsModifiers);

	Super::ServerMove_PerformMovement(MoveData);
}

FClientAuthData* UCustomMovementComponent::ProcessClientAuthData()
{
	ClientAuthStack.SortByPriority();
	return ClientAuthStack.GetFirst();
}

FClientAuthParams UCustomMovementComponent::GetClientAuthParams(const FClientAuthData* ClientAuthData)
{
	if (!ClientAuthData)
	{
		return {};
	}
	
	FClientAuthParams Params = { false, 0.f, 0.f, 0.f, ClientAuthData->Priority };

	// Get all active client auth data that matches the priority
	TArray<FClientAuthData> Priority = ClientAuthStack.FilterPriority(ClientAuthData->Priority);

	// Combine the parameters
	int32 Num = 0;
	for (const FClientAuthData& Data : Priority)
	{
		if (const FClientAuthParams* DataParams = GetClientAuthParamsForSource(Data.Source))
		{
			Params.ClientAuthTime += DataParams->ClientAuthTime;
			Params.MaxClientAuthDistance += DataParams->MaxClientAuthDistance;
			Params.RejectClientAuthDistance += DataParams->RejectClientAuthDistance;
			Num++;
		}
	}

	// Average the parameters
	Params.bEnableClientAuth = Num > 0;
	if (Num > 1)
	{
		Params.ClientAuthTime /= Num;
		Params.MaxClientAuthDistance /= Num;
		Params.RejectClientAuthDistance /= Num;
	}

	return Params;
}

void UCustomMovementComponent::GrantClientAuthority(FGameplayTag ClientAuthSource, float OverrideDuration)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
	{
		return;
	}
	
	if (const FClientAuthParams* Params = GetClientAuthParamsForSource(ClientAuthSource))
	{
		if (Params->bEnableClientAuth)
		{
			const float Duration = OverrideDuration > 0.f ? OverrideDuration : Params->ClientAuthTime;
			ClientAuthStack.Stack.Add(FClientAuthData(ClientAuthSource, Duration, Params->Priority, ++ClientAuthIdCounter));

			// Limit the number of auth data entries
			// IMPORTANT: We do not allow serializing more than 8, if this changes, update the serialization code too
			if (ClientAuthStack.Stack.Num() > 8)
			{
				ClientAuthStack.Stack.RemoveAt(0);
			}
		}
	}
	else
	{
#if WITH_EDITOR
		FMessageLog("PIE").Error(FText::FromString(FString::Printf(TEXT("ClientAuthSource '%s' not found in ClientAuthParams"), *ClientAuthSource.ToString())));
#else
		UE_LOG(LogPredictedMovement, Error, TEXT("ClientAuthSource '%s' not found"), *ClientAuthSource.ToString());
#endif
	}
}

bool UCustomMovementComponent::ServerShouldGrantClientPositionAuthority(FVector& ClientLoc, FClientAuthData*& AuthData)
{
	AuthData = nullptr;
	
	// Already ignoring client movement error checks and correction
	if (bIgnoreClientMovementErrorChecksAndCorrection)
	{
		return false;
	}

	// Abort if client authority is not enabled
#if !UE_BUILD_SHIPPING
	if (PredMovementCVars::bClientAuthDisabled)
	{
		return false;
	}
#endif

	// Get auth data
	AuthData = ProcessClientAuthData();
	if (!AuthData || !AuthData->IsValid())
	{
		// No auth data, can't do anything
		return false;
	}

	// Get auth params
	const FClientAuthParams Params = GetClientAuthParams(AuthData);

	// Disabled
	if (!Params.bEnableClientAuth)
	{
		return false;
	}

	// Validate auth data
#if !UE_BUILD_SHIPPING
	if (UNLIKELY(AuthData->TimeRemaining <= 0.f))
	{
		// ServerMoveHandleClientError() should have removed the auth data already
		return ensure(false);
	}
#endif
	
	// Reset alpha, we're going to calculate it now
	AuthData->Alpha = 0.f;

	// How far the client is from the server
	const FVector ServerLoc = UpdatedComponent->GetComponentLocation();
	FVector LocDiff = ServerLoc - ClientLoc;

	// No change or almost no change occurred
	if (LocDiff.IsNearlyZero())
	{
		// Grant full authority
		AuthData->Alpha = 1.f;
		return true;
	}

	// If the client is too far away from the server, reject the client position entirely, potential cheater
	if (LocDiff.SizeSquared() >= FMath::Square(Params.RejectClientAuthDistance))
	{
		OnClientAuthRejected(ClientLoc, ServerLoc, LocDiff);
		return false;
	}

	// If the client is not within the maximum allowable distance, accept the client position, but only partially
	if (LocDiff.Size() >= Params.MaxClientAuthDistance)
	{
		// Accept only a portion of the client's location
		AuthData->Alpha = Params.MaxClientAuthDistance / LocDiff.Size();
		ClientLoc = FMath::Lerp<FVector>(ServerLoc, ClientLoc, AuthData->Alpha);
		LocDiff = ServerLoc - ClientLoc;
	}
	else
	{
		// Accept full client location
		AuthData->Alpha = 1.f;
	}

	return true;
}

bool UCustomMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
	const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
         FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	// ServerMovePacked_ServerReceive ➜ ServerMove_HandleMoveData ➜ ServerMove_PerformMovement
	// ➜ ServerMoveHandleClientError ➜ ServerCheckClientError
	
	if (Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))
	{
		return true;
	}

	// Trigger a client correction if the value in the Client differs
	const FPredictedNetworkMoveData* CurrentMoveData = static_cast<const FPredictedNetworkMoveData*>(GetCurrentNetworkMoveData());
    
	/*
	 * This will trigger a client correction if the Stamina value in the Client differs
	 * NetworkStaminaCorrectionThreshold (2.f default) units from the one in the server
	 * De-syncs can happen if we set the Stamina directly in Gameplay code (ie: GAS)
	 */
	if (!FMath::IsNearlyEqual(CurrentMoveData->Stamina, Stamina, NetworkStaminaCorrectionThreshold))
	{
		return true;
	}

	if (HasteCorrection.ServerCheckClientError(CurrentMoveData->HasteCorrection.Modifiers))	{ return true; }
	if (SlowCorrection.ServerCheckClientError(CurrentMoveData->SlowCorrection.Modifiers))	{ return true; }
	if (SlowFallCorrection.ServerCheckClientError(CurrentMoveData->SlowFallCorrection.Modifiers)) { return true; }
	
	return false;
}

void UCustomMovementComponent::ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	// This is the entry-point for determining how to handle client corrections; we can determine they are out of sync
	// and make any changes that suit our needs
	
	// Client >> TickComponent ➜ ControlledCharacterMove ➜ CallServerMovePacked ➜ ReplicateMoveToServer >> Server
	// >> ServerMove_PerformMovement ➜ ServerMoveHandleClientError

	// Process and grant client authority
#if !UE_BUILD_SHIPPING
	if (!PredMovementCVars::bClientAuthDisabled)
#endif
	{
		// Update client authority time remaining
		ClientAuthStack.Update(DeltaTime);

		// Test for client authority
		FVector ClientLoc = FRepMovement::RebaseOntoZeroOrigin(RelativeClientLocation, this);
		FClientAuthData* AuthData = nullptr;
		if (ServerShouldGrantClientPositionAuthority(ClientLoc, AuthData))
		{
			// Apply client authoritative position directly -- Subsequent moves will resolve overlapping conditions
			UpdatedComponent->SetWorldLocation(ClientLoc, false);
		}

		// Cached to be sent to the client later with FMoveResponseDataContainer
		ClientAuthAlpha = AuthData ? AuthData->Alpha : 0.f;
	}

	// The move prepared here will finally be sent in the next ReplicateMoveToServer()

	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBase,
		ClientBaseBoneName, ClientMovementMode);
}

void UCustomMovementComponent::ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition,
	uint8 ServerMovementMode, TOptional<FRotator> OptionalRotation)
{
	if (!HasValidData() || !IsActive())
	{
		return;
	}

	const FVector ClientLoc = UpdatedComponent->GetComponentLocation();
	
	Super::ClientAdjustPosition_Implementation(TimeStamp, NewLoc, NewVel, NewBase, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode,OptionalRotation);

	const FPredictedMoveResponseDataContainer& MoveResponse = static_cast<const FPredictedMoveResponseDataContainer&>(GetMoveResponseDataContainer());
	ClientAuthAlpha = MoveResponse.bHasClientAuthAlpha ? MoveResponse.ClientAuthAlpha : 0.f;

	// Preserve client location relative to the partial client authority we have
	const FVector AuthLocation = FMath::Lerp<FVector>(UpdatedComponent->GetComponentLocation(), ClientLoc, ClientAuthAlpha);
	UpdatedComponent->SetWorldLocation(AuthLocation, false);
}

void UCustomMovementComponent::OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData,
                                                          float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName,
                                                          bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	// This occurs on AutonomousProxy, when the server sends the move response
	// This is where we receive the snare, and can override the server's location, assuming it has given us authority

	// Server >> SendClientAdjustment() ➜ ServerSendMoveResponse() ➜ ServerFillResponseData() + MoveResponsePacked_ServerSend() >> Client
	// >> ClientMoveResponsePacked() ➜ ClientHandleMoveResponse() ➜ ClientAdjustPosition_Implementation() ➜ OnClientCorrectionReceived()
	
	const FPredictedMoveResponseDataContainer& MoveResponse = static_cast<const FPredictedMoveResponseDataContainer&>(GetMoveResponseDataContainer());

	// Stamina
	SetStamina(MoveResponse.Stamina);
	SetStaminaDrained(MoveResponse.bStaminaDrained);

	// Modifiers
	HasteCorrection.OnClientCorrectionReceived(MoveResponse.HasteCorrection.Modifiers);
	SlowCorrection.OnClientCorrectionReceived(MoveResponse.SlowCorrection.Modifiers);
	SlowFallCorrection.OnClientCorrectionReceived(MoveResponse.SlowFallCorrection.Modifiers);
	
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName,
		bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

void UCustomMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	if (!HasValidData())
	{
		return;
	}
	
	if (const FPredictedNetworkMoveData* MoveData = static_cast<FPredictedNetworkMoveData*>(GetCurrentNetworkMoveData()))
	{
		// Extra set of compression flags
		UpdateFromCompressedFlagsExtra(MoveData->CompressedMoveFlagsExtra);
	}
	
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UCustomMovementComponent::UpdateFromCompressedFlagsExtra(uint8 Flags)
{
	bWantsToWalk = (Flags & FPredictedSavedMove::FLAGEX_Walk) != 0;
	bWantsToSprint = (Flags & FPredictedSavedMove::FLAGEX_Sprint) != 0;
}

uint8 FPredictedSavedMove::GetCompressedFlagsExtra() const
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
}

void FPredictedSavedMove::Clear()
{
	Super::Clear();

	bWantsToWalk = false;
	bWantsToSprint = false;
	
	bStaminaDrained = false;
	StartStamina = 0.f;
	EndStamina = 0.f;
	
	// Modifiers
	HasteLocal.Clear();
	HasteCorrection.Clear();
	SlowLocal.Clear();
	SlowCorrection.Clear();
	SlowFallLocal.Clear();
	SlowFallCorrection.Clear();
	
	HasteLevel = NO_MODIFIER;
	SlowLevel = NO_MODIFIER;
	SlowFallLevel = NO_MODIFIER;
}

bool FPredictedSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter,	float MaxDelta) const
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

	// We can only combine moves if they will result in the same state as if both moves were processed individually,
	// because the AutonomousProxy Client processes them individually prior to sending them to the server.
	
	if (!HasteLocal.CanCombineWith(SavedMove->HasteLocal.WantsModifiers)) { return false; }
	if (!HasteCorrection.CanCombineWith(SavedMove->HasteCorrection.WantsModifiers)) { return false; }
	if (!SlowLocal.CanCombineWith(SavedMove->SlowLocal.WantsModifiers)) { return false; }
	if (!SlowCorrection.CanCombineWith(SavedMove->SlowCorrection.WantsModifiers)) { return false; }
	if (!SlowFallLocal.CanCombineWith(SavedMove->SlowFallLocal.WantsModifiers)) { return false; }
	if (!SlowFallCorrection.CanCombineWith(SavedMove->SlowFallCorrection.WantsModifiers)) { return false; }

	// Without these, the change/start/stop events will trigger twice causing de-sync, so we don't combine moves if the level changes
	if (HasteLevel != SavedMove->HasteLevel) { return false; }
	if (SlowLevel != SavedMove->SlowLevel) { return false; }
	if (SlowFallLevel != SavedMove->SlowFallLevel) { return false; }
	
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FPredictedSavedMove::SetInitialPosition(ACharacter* C)
{
	// To counter the PendingMove potentially being processed twice, we need to make sure to reset the state of the CMC
	// back to the "InitialPosition" (state) it had before the PendingMove got processed.
	
	Super::SetInitialPosition(C);

	if (const UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		// Retrieve the value from our CMC to revert the saved move value back to this.
		bStaminaDrained = MoveComp->IsStaminaDrained();
		StartStamina = MoveComp->GetStamina();

		// Modifiers
		HasteLocal.SetInitialPosition(MoveComp->HasteLocal.WantsModifiers);
		HasteCorrection.SetInitialPosition(MoveComp->HasteCorrection.WantsModifiers);
		SlowLocal.SetInitialPosition(MoveComp->SlowLocal.WantsModifiers);
		SlowCorrection.SetInitialPosition(MoveComp->SlowCorrection.WantsModifiers);
		SlowFallLocal.SetInitialPosition(MoveComp->SlowFallLocal.WantsModifiers);
		SlowFallCorrection.SetInitialPosition(MoveComp->SlowFallCorrection.WantsModifiers);

		HasteLevel = MoveComp->HasteLevel;
		SlowLevel = MoveComp->SlowLevel;
		SlowFallLevel = MoveComp->SlowFallLevel;
	}
}

void FPredictedSavedMove::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

	if (const UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		bWantsToWalk = MoveComp->bWantsToWalk;
		bWantsToSprint = MoveComp->bWantsToSprint;

		// Modifiers
		HasteLocal.SetMoveFor(MoveComp->HasteLocal.WantsModifiers);
		HasteCorrection.SetMoveFor(MoveComp->HasteCorrection.WantsModifiers);
		SlowLocal.SetMoveFor(MoveComp->SlowLocal.WantsModifiers);
		SlowCorrection.SetMoveFor(MoveComp->SlowCorrection.WantsModifiers);
		SlowFallLocal.SetMoveFor(MoveComp->SlowFallLocal.WantsModifiers);
		SlowFallCorrection.SetMoveFor(MoveComp->SlowFallCorrection.WantsModifiers);
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
	// When considering whether to delay or combine moves, we need to compare the move at the start and the end
	if (const UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		EndStamina = MoveComp->GetStamina();

		// Modifiers
		HasteCorrection.PostUpdate(MoveComp->HasteCorrection.Modifiers);
		SlowCorrection.PostUpdate(MoveComp->SlowCorrection.Modifiers);
		SlowFallCorrection.PostUpdate(MoveComp->SlowFallCorrection.Modifiers);

		if (PostUpdateMode == PostUpdate_Record)
		{
			// Don't combine moves if the properties changed over the course of the move
			if (bStaminaDrained != MoveComp->IsStaminaDrained())
			{
				bForceNoCombine = true;
			}
		}
	}

	Super::PostUpdate(C, PostUpdateMode);
}

void FPredictedSavedMove::CombineWith(const FSavedMove_Character* OldMove, ACharacter* C, APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, C, PC, OldStartLocation);

	const FPredictedSavedMove* SavedOldMove = static_cast<const FPredictedSavedMove*>(OldMove);

	if (UCustomMovementComponent* MoveComp = C ? Cast<UCustomMovementComponent>(C->GetCharacterMovement()) : nullptr)
	{
		MoveComp->SetStamina(SavedOldMove->StartStamina);
		MoveComp->SetStaminaDrained(SavedOldMove->bStaminaDrained);

		// Modifiers
		MoveComp->HasteLocal.CombineWith(SavedOldMove->HasteLocal.WantsModifiers);
		MoveComp->HasteCorrection.CombineWith(SavedOldMove->HasteCorrection.WantsModifiers);
		MoveComp->SlowLocal.CombineWith(SavedOldMove->SlowLocal.WantsModifiers);
		MoveComp->SlowCorrection.CombineWith(SavedOldMove->SlowCorrection.WantsModifiers);
		MoveComp->SlowFallLocal.CombineWith(SavedOldMove->SlowFallLocal.WantsModifiers);
		MoveComp->SlowFallCorrection.CombineWith(SavedOldMove->SlowFallCorrection.WantsModifiers);

		MoveComp->HasteLevel = SavedOldMove->HasteLevel;
		MoveComp->SlowLevel = SavedOldMove->SlowLevel;
		MoveComp->SlowFallLevel = SavedOldMove->SlowFallLevel;
	}
}

bool FPredictedSavedMove::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	// Important moves get sent again if not acked by the server
	
	const TSharedPtr<FPredictedSavedMove>& SavedMove = StaticCastSharedPtr<FPredictedSavedMove>(LastAckedMove);

	if (HasteLocal.IsImportantMove(SavedMove->HasteLocal.WantsModifiers)) { return true; }
	if (HasteCorrection.IsImportantMove(SavedMove->HasteCorrection.WantsModifiers)) { return true; }
	if (SlowLocal.IsImportantMove(SavedMove->SlowLocal.WantsModifiers)) { return true; }
	if (SlowCorrection.IsImportantMove(SavedMove->SlowCorrection.WantsModifiers)) { return true; }
	if (SlowFallLocal.IsImportantMove(SavedMove->SlowFallLocal.WantsModifiers)) { return true; }
	if (SlowFallCorrection.IsImportantMove(SavedMove->SlowFallCorrection.WantsModifiers)) { return true; }
	
	return Super::IsImportantMove(LastAckedMove);
}

FSavedMovePtr FPredictedNetworkPredictionData_Client::AllocateNewMove()
{
	return MakeShared<FPredictedSavedMove>();
}

bool UCustomMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bRealWalk = bWantsToWalk;
	const bool bRealSprint = bWantsToSprint;

	// Modifiers
	const TModifierStack RealHasteLocal = HasteLocal.WantsModifiers;
	const TModifierStack RealHasteCorrection = HasteCorrection.WantsModifiers;
	const TModifierStack RealSlowLocal = SlowLocal.WantsModifiers;
	const TModifierStack RealSlowCorrection = SlowCorrection.WantsModifiers;
	const TModifierStack RealSlowFallLocal = SlowFallLocal.WantsModifiers;
	const TModifierStack RealSlowFallCorrection = SlowFallCorrection.WantsModifiers;

	// Client location authority
	const FVector ClientLoc = UpdatedComponent->GetComponentLocation();
	
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	
	bWantsToWalk = bRealWalk;
	bWantsToSprint = bRealSprint;

	// Modifiers
	HasteLocal.WantsModifiers = RealHasteLocal;
	HasteCorrection.WantsModifiers = RealHasteCorrection;
	SlowLocal.WantsModifiers = RealSlowLocal;
	SlowCorrection.WantsModifiers = RealSlowCorrection;
	SlowFallLocal.WantsModifiers = RealSlowFallLocal;
	SlowFallCorrection.WantsModifiers = RealSlowFallCorrection;

	// Preserve client location relative to the partial client authority we have
	const FVector AuthLocation = FMath::Lerp<FVector>(UpdatedComponent->GetComponentLocation(), ClientLoc, ClientAuthAlpha);
	UpdatedComponent->SetWorldLocation(AuthLocation, false);

	return bResult;
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
