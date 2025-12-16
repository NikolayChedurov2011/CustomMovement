// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "ModifierTypes.generated.h"

#define NO_MODIFIER UINT8_MAX

UENUM(BlueprintType)
enum class EModifierFallZ : uint8
{
	Disabled			UMETA(ToolTip="Do not remove Velocity.Z when modifier starts"),
	Enabled				UMETA(ToolTip="Remove Velocity.Z when modifier starts"),
	Falling				UMETA(ToolTip="Remove Velocity.Z when modifier starts, but only if the character is falling (Velocity.Z < 0)"),
	Rising				UMETA(ToolTip="Remove Velocity.Z when modifier starts, but only if the character is rising (Velocity.Z > 0)"),
};

/**
 * Parameters for a modifier that affects character movement
 */
USTRUCT(BlueprintType)
struct CUSTOMMOVEMENT_API FMovementModifierParams
{
	GENERATED_BODY()

	FMovementModifierParams(float InMaxWalkSpeed = 1.f, float InMaxAcceleration = 1.f, float InBrakingDeceleration = 1.f,
		float InGroundFriction = 1.f, float InBrakingFriction = 1.f, bool bInAffectsRootMotion = false)
		: MaxWalkSpeed(InMaxWalkSpeed)
		, MaxAcceleration(InMaxAcceleration)
		, BrakingDeceleration(InBrakingDeceleration)
		, GroundFriction(InGroundFriction)
		, BrakingFriction(InBrakingFriction)
		, bAffectsRootMotion(bInAffectsRootMotion)
	{}
	
	/** The maximum ground speed when walking. Also determines maximum lateral speed when falling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float MaxWalkSpeed;
	
	/** Max Acceleration (rate of change of velocity) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float MaxAcceleration;
	
	/**
	 * Deceleration when walking and not applying acceleration. This is a constant opposing force that directly lowers velocity by a constant value.
	 * @see GroundFriction, MaxAcceleration
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float BrakingDeceleration;

	/**
	 * Setting that affects movement control. Higher values allow faster changes in direction.
	 * If bUseSeparateBrakingFriction is false, also affects the ability to stop more quickly when braking (whenever Acceleration is zero), where it is multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * This can be used to simulate slippery surfaces such as ice or oil by changing the value (possibly based on the material pawn is standing on).
	 * @see BrakingDecelerationWalking, BrakingFriction, bUseSeparateBrakingFriction, BrakingFrictionFactor
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float GroundFriction;
	
	/**
	 * Friction (drag) coefficient applied when braking (whenever Acceleration = 0, or if character is exceeding max speed); actual value used is this multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * Braking is composed of friction (velocity-dependent drag) and constant deceleration.
	 * This is the current value, used in all movement modes; if this is not desired, override it or bUseSeparateBrakingFriction when movement mode changes.
	 * @note Only used if bUseSeparateBrakingFriction setting is true, otherwise current friction such as GroundFriction is used.
	 * @see bUseSeparateBrakingFriction, BrakingFrictionFactor, GroundFriction, BrakingDecelerationWalking
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float BrakingFriction;

	/** If true, this modifier's MaxWalkSpeed scalar affects root motion translation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	bool bAffectsRootMotion;
};

/**
 * Parameters for a modifier that affects falling
 */
USTRUCT(BlueprintType)
struct CUSTOMMOVEMENT_API FFallingModifierParams
{
	GENERATED_BODY()

	FFallingModifierParams(float InGravityScalar = 1.f, EModifierFallZ InRemoveVelocityZ = EModifierFallZ::Disabled)
		: bGravityScalarFromVelocityZ(false)
		, GravityScalar(InGravityScalar)
		, GravityScalarFallVelocityCurve(nullptr)
		, RemoveVelocityZOnStart(InRemoveVelocityZ)
		, bOverrideAirControl(false)
		, AirControlScalar(1.f)
		, AirControlOverride(1.f)
	{}

	/** If true, use GravityScalarFallVelocityCurve */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier)
	bool bGravityScalarFromVelocityZ;

	/** Gravity is multiplied by this amount */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x", EditCondition="!bGravityScalarFromVelocityZ", EditConditionHides))
	float GravityScalar;

	/** Gravity scale curve based on fall velocity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(EditCondition="bGravityScalarFromVelocityZ", EditConditionHides))
	TObjectPtr<UCurveFloat> GravityScalarFallVelocityCurve;

	/** Set Velocity.Z = 0.f when air fall starts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(DisplayName="Remove Velocity Z On Start"))
	EModifierFallZ RemoveVelocityZOnStart;

	/** If true, directly set the air control value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier)
	bool bOverrideAirControl;

	/**
	 * When falling, amount of lateral movement control available to the character.
	 * 0 = no control, 1 = full control at max speed of MaxWalkSpeed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(ClampMin="0", UIMin="0", ForceUnits="x", EditCondition="!bOverrideAirControl", EditConditionHides))
	float AirControlScalar;

	/**
	 * When falling, amount of lateral movement control available to the character.
	 * 0 = no control, 1 = full control at max speed of MaxWalkSpeed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Modifier, meta=(EditCondition="bOverrideAirControl", EditConditionHides))
	float AirControlOverride;

	/**
	 * Get the gravity scalar based on the current velocity.
	 * If bGravityScalarFromVelocityZ is true, uses GravityScalarFallVelocityCurve to determine the scalar based on Velocity.Z.
	 * Otherwise, returns GravityScalar.
	 */
	float GetGravityScalar(const FVector& Velocity) const
	{
		if (!ensureMsgf(!bGravityScalarFromVelocityZ || GravityScalarFallVelocityCurve != nullptr, TEXT("GravityScalarFallVelocityCurve must be set")))
		{
			return 1.f;
		}
		return bGravityScalarFromVelocityZ ? GravityScalarFallVelocityCurve->GetFloatValue(Velocity.Z) : GravityScalar;
	}

	/**
	 * Get the air control value based on the current air control scalar.
	 * If bOverrideAirControl is true, returns AirControlOverride.
	 * Otherwise, returns AirControlScalar multiplied by CurrentAirControl.
	 * @param CurrentAirControl The current air control value to scale.
	 */
	float GetAirControl(float CurrentAirControl) const
	{
		return bOverrideAirControl ? AirControlOverride : AirControlScalar * CurrentAirControl;
	}
};