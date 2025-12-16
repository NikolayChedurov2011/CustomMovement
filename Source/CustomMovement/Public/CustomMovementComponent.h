#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
//#include "GameFramework/CharacterMovementReplication.h"

#include "CustomMovementTypes.h"
#include "Modifier/ModifierTypes.h"
#include "Tags/CM_GameplayTags.h"

#include "CustomMovementComponent.generated.h"

class FPredictedSavedMove;

struct FCM_NetworkMoveData : public FCharacterNetworkMoveData
{
public:

	using Super = FCharacterNetworkMoveData;
	
	uint8 SavedHasteLevel    = NO_MODIFIER;
	uint8 SavedSlowLevel     = NO_MODIFIER;
	uint8 SavedSlowFallLevel = NO_MODIFIER;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FCM_NetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	using Super = FCharacterNetworkMoveDataContainer;
	
	FCM_NetworkMoveData MoveData[3];

	FCM_NetworkMoveDataContainer()
	{
		NewMoveData     = &MoveData[0];
		PendingMoveData = &MoveData[1];
		OldMoveData     = &MoveData[2];
	}
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUSTOMMOVEMENT_API UCustomMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual FCharacterNetworkMoveDataContainer& GetNetworkMoveDataContainer()
	{
		return CM_MoveDataContainer;
	}

private:
	FCM_NetworkMoveDataContainer CM_MoveDataContainer;
	
public:
	/** Max Acceleration (rate of change of velocity) */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float MaxAccelerationRunning;

	/** The maximum ground speed when Running. */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float MaxWalkSpeedRunning;

	/**
	 * Deceleration when walking and not applying acceleration. This is a constant opposing force that directly lowers velocity by a constant value.
	 * @see GroundFriction, MaxAcceleration
	 */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float BrakingDecelerationRunning;

	/**
	 * Setting that affects movement control. Higher values allow faster changes in direction.
	 * If bUseSeparateBrakingFriction is false, also affects the ability to stop more quickly when braking (whenever Acceleration is zero), where it is multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * This can be used to simulate slippery surfaces such as ice or oil by changing the value (possibly based on the material pawn is standing on).
	 * @see BrakingDecelerationWalking, BrakingFriction, bUseSeparateBrakingFriction, BrakingFrictionFactor
	 */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float GroundFrictionRunning;

	/**
     * When struggling to surpass walk speed, which can occur with heavy rotation and low acceleration, we
     * mitigate the check so there isn't a constant re-entry that can occur as an edge case.
     * This can optionally be used inversely, to require you to considerably exceed MaxSpeedWalking before Running
     * will actually take effect.
     */
    UPROPERTY(Category="Character Movement: Walking", AdvancedDisplay, EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
    float VelocityCheckMitigatorRunning;

	/**
	 * Friction (drag) coefficient applied when braking (whenever Acceleration = 0, or if character is exceeding max speed); actual value used is this multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * Braking is composed of friction (velocity-dependent drag) and constant deceleration.
	 * This is the current value, used in all movement modes; if this is not desired, override it or bUseSeparateBrakingFriction when movement mode changes.
	 * @note Only used if bUseSeparateBrakingFriction setting is true, otherwise current friction such as GroundFriction is used.
	 * @see bUseSeparateBrakingFriction, BrakingFrictionFactor, GroundFriction, BrakingDecelerationWalking
	 */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", EditCondition="bUseSeparateBrakingFriction"))
	float BrakingFrictionRunning;

public:
	/**
     * When struggling to surpass walk speed, which can occur with heavy rotation and low acceleration, we
     * mitigate the check so there isn't a constant re-entry that can occur as an edge case.
     * This can optionally be used inversely, to require you to considerably exceed MaxSpeedWalking before Strolling
     * will actually take effect.
     */
    UPROPERTY(Category="Character Movement: Walking", AdvancedDisplay, EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
    float VelocityCheckMitigatorWalking;

public:
	/** If true, try to Walk (or keep Walking) on next update. If false, try to stop Walking on next update. */
	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadOnly)
	uint8 bWantsToWalk:1;

public:
	/** If true, sprinting acceleration will only be applied when IsSprintingAtSpeed() returns true */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite)
	bool bUseMaxAccelerationSprintingOnlyAtSpeed;
	
	/** Max Acceleration (rate of change of velocity) */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float MaxAccelerationSprinting;
	
	/** The maximum ground speed when Sprinting. */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float MaxWalkSpeedSprinting;

	/**
	 * Deceleration when walking and not applying acceleration. This is a constant opposing force that directly lowers velocity by a constant value.
	 * @see GroundFriction, MaxAcceleration
	 */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float BrakingDecelerationSprinting;

	/**
	 * Setting that affects movement control. Higher values allow faster changes in direction.
	 * If bUseSeparateBrakingFriction is false, also affects the ability to stop more quickly when braking (whenever Acceleration is zero), where it is multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * This can be used to simulate slippery surfaces such as ice or oil by changing the value (possibly based on the material pawn is standing on).
	 * @see BrakingDecelerationWalking, BrakingFriction, bUseSeparateBrakingFriction, BrakingFrictionFactor
	 */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float GroundFrictionSprinting;

	/**
     * When struggling to surpass walk speed, which can occur with heavy rotation and low acceleration, we
     * mitigate the check so there isn't a constant re-entry that can occur as an edge case.
     * This can optionally be used inversely, to require you to considerably exceed MaxSpeedWalking before sprinting
     * will actually take effect.
     */
    UPROPERTY(Category="Character Movement: Walking", AdvancedDisplay, EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
    float VelocityCheckMitigatorSprinting;
	
	/**
	 * Friction (drag) coefficient applied when braking (whenever Acceleration = 0, or if character is exceeding max speed); actual value used is this multiplied by BrakingFrictionFactor.
	 * When braking, this property allows you to control how much friction is applied when moving across the ground, applying an opposing force that scales with current velocity.
	 * Braking is composed of friction (velocity-dependent drag) and constant deceleration.
	 * This is the current value, used in all movement modes; if this is not desired, override it or bUseSeparateBrakingFriction when movement mode changes.
	 * @note Only used if bUseSeparateBrakingFriction setting is true, otherwise current friction such as GroundFriction is used.
	 * @see bUseSeparateBrakingFriction, BrakingFrictionFactor, GroundFriction, BrakingDecelerationWalking
	 */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", EditCondition="bUseSeparateBrakingFriction"))
	float BrakingFrictionSprinting;

	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(InlineEditConditionToggle))
	bool bRestrictSprintInputAngle;

	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, meta=(EditCondition="bRestrictSprintInputAngle", ClampMin="0.0", ClampMax="180.0", UIMin = "0.0", UIMax = "180.0", ForceUnits="degrees"))
	float MaxInputAngleSprint;

	UPROPERTY(Category="Character Movement: Walking", VisibleAnywhere)
	float MaxInputNormalSprint;
	
public:
	/** If true, try to Sprint (or keep Sprinting) on next update. If false, try to stop Sprinting on next update. */
	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadOnly)
	uint8 bWantsToSprint:1;

public:
	/** How much Stamina to start with */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float BaseMaxStamina;
	
	/** Modify maximum speed when stamina is drained. */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float MaxWalkSpeedScalarStaminaDrained;

	/** Modify maximum acceleration when stamina is drained. */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float MaxAccelerationScalarStaminaDrained;

	/** Modify maximum braking deceleration when stamina is drained. */
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ForceUnits="x"))
	float MaxBrakingDecelerationScalarStaminaDrained;
	
	/** The rate at which stamina is drained while sprinting */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite)
	float SprintStaminaDrainRate;

	/** The rate at which stamina is regenerated when not being drained */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite)
	float StaminaRegenRate;

	/** The rate at which stamina is regenerated when in a drained state */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite)
	float StaminaDrainedRegenRate;

	/** If true, stamina recovery from drained state is based on percentage instead of amount */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite)
	bool bStaminaRecoveryFromPct;
	
	/** Amount of Stamina to recover before being considered recovered */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", EditCondition="!bStaminaRecoveryFromPct", EditConditionHides))
	float StaminaRecoveryAmount;

	/** Percentage of Stamina to recover before being considered recovered */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ClampMax="1", UIMax="1", ForceUnits="%", EditCondition="bStaminaRecoveryFromPct", EditConditionHides))
	float StaminaRecoveryPct;

	/** If Stamina Pct is below this value then cannot start sprinting */
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0", ClampMax="1", UIMax="1", ForceUnits="%", EditCondition="bStaminaRecoveryFromPct", EditConditionHides))
	float StartSprintStaminaPct;
	
public:
	/** Maximum stamina difference that is allowed between client and server before a correction occurs. */
	UPROPERTY(Category="Character Movement (Networking)", EditDefaultsOnly, meta=(ClampMin="0.0", UIMin="0.0"))
	float NetworkStaminaCorrectionThreshold;
	
protected:
	/** THIS SHOULD ONLY BE MODIFIED IN DERIVED CLASSES FROM OnStaminaChanged AND NOWHERE ELSE */
	UPROPERTY(ReplicatedUsing = OnRep_Stamina)
	float Stamina;

private:
	UPROPERTY(ReplicatedUsing = OnRep_MaxStamina)
	float MaxStamina;

	UPROPERTY()
	bool bStaminaDrained;

	UFUNCTION()
	void OnRep_Stamina();
	UFUNCTION()
	void OnRep_MaxStamina();

public:
	/**
	 * Haste modifies movement properties such as speed and acceleration
	 * Scaling applied on a per-Haste-level basis
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, FMovementModifierParams> Haste;

	/**
	 * Limits the maximum number of Haste levels that can be applied to the character
	 * This value is shared between each type of Haste
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(InlineEditConditionToggle))
	bool bLimitMaxHastes = true;
	
	/**
	 * Maximum number of Haste levels that can be applied to the character
	 * This value is shared between each type of Haste
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(ClampMin=1, UIMin=1, UIMax=32, EditCondition="bLimitMaxHastes"))
	int32 MaxHastes = 8;

	/** Indexed list of Haste levels, used to determine the current Haste level based on index */
	UPROPERTY()
	TArray<FGameplayTag> HasteLevels;

public:
	/**
	 * Slow modifies movement properties such as speed and acceleration
	 * Scaling applied on a per-Slow-level basis
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, FMovementModifierParams> Slow;

	/**
	 * Limits the maximum number of Slow levels that can be applied to the character
	 * This value is shared between each type of Slow
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(InlineEditConditionToggle))
	bool bLimitMaxSlows = true;
	
	/**
	 * Maximum number of Slow levels that can be applied to the character
	 * This value is shared between each type of Slow
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(ClampMin=1, UIMin=1, UIMax=32, EditCondition="bLimitMaxSlows"))
	int32 MaxSlows = 8;

	/** Indexed list of Slow levels, used to determine the current Slow level based on index */
	UPROPERTY()
	TArray<FGameplayTag> SlowLevels;
	
public:
	/**
	 * SlowFall changes falling properties, such as gravity and air control
	 * Scaling applied on a per-SlowFall-level basis
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, FFallingModifierParams> SlowFall;

	/**
	 * Limits the maximum number of SlowFall levels that can be applied to the character
	 * This value is shared between each type of SlowFall
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(InlineEditConditionToggle))
	bool bLimitMaxSlowFalls = true;
	
	/**
	 * Maximum number of SlowFall levels that can be applied to the character
	 * This value is shared between each type of SlowFall
	 * It limits both the number being serialized and sent over the network, as well as having gameplay implications
	 * Priority is granted in order, because modifiers consume the remaining slots, so LocalPredicted -> WithCorrection - ServerInitiated
	 */
	UPROPERTY(Category="Character Movement: Modifiers", EditAnywhere, BlueprintReadWrite, meta=(ClampMin=1, UIMin=1, UIMax=32, EditCondition="bLimitMaxSlowFalls"))
	int32 MaxSlowFalls = 8;

	/** Indexed list of SlowFall levels, used to determine the current SlowFall level */
	UPROPERTY()
	TArray<FGameplayTag> SlowFallLevels;

public:
	UCustomMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	virtual void BeginPlay() override;
	
public:
	UFUNCTION(BlueprintPure)
	virtual ECustomMovementGaitMode GetGaitMode() const;
	//virtual ECustomMovementGaitMode GetGaitSpeed() const;

	virtual bool IsGaitAtSpeed(float Mitigator) const;
	virtual bool IsWalk() const;  // Do not mistake this for UCharacterMovementComponent::IsWalking
	virtual bool IsWalkingAtSpeed() const { return IsWalk() && IsGaitAtSpeed(VelocityCheckMitigatorWalking); }
	virtual bool IsRunning() const;
	virtual bool IsRunningAtSpeed() const { return IsRunning() && IsGaitAtSpeed(VelocityCheckMitigatorRunning); }
	virtual bool IsSprintingAtSpeed() const { return IsSprinting() && IsGaitAtSpeed(VelocityCheckMitigatorSprinting); }
	virtual bool IsSprintingInEffect() const { return IsSprintingAtSpeed() && IsSprintWithinAllowableInputAngle(); }


public:
	// Movement scalars

	/** Max speed scalar without sprinting checks */
	virtual float GetGaitSpeedFactor() const;
	
	virtual float GetMaxAccelerationScalar() const;
	virtual float GetMaxSpeedScalar() const;
	virtual float GetMaxBrakingDecelerationScalar() const;
	virtual float GetGroundFrictionScalar() const;
	virtual float GetBrakingFrictionScalar() const;
	virtual float GetGravityZScalar() const;
	virtual float GetRootMotionTranslationScalar() const;

	// Base movement values
	
	virtual float GetBaseMaxAcceleration() const;
	virtual float GetBaseMaxSpeed() const;
	virtual float GetBaseMaxBrakingDeceleration() const;
	virtual float GetBaseGroundFriction(float DefaultGroundFriction) const;
	virtual float GetBaseBrakingFriction() const;

	// Final movement values
	
	virtual float GetMaxAcceleration() const override { return GetBaseMaxAcceleration() * GetMaxAccelerationScalar(); }
	virtual float GetMaxSpeed() const override { return GetBaseMaxSpeed() * GetMaxSpeedScalar(); }
	virtual float GetMaxBrakingDeceleration() const override { return GetBaseMaxBrakingDeceleration() * GetMaxBrakingDecelerationScalar(); }
	virtual float GetGroundFriction(float DefaultGroundFriction) const { return GetBaseGroundFriction(DefaultGroundFriction) * GetGroundFrictionScalar(); }
	virtual float GetBrakingFriction() const { return GetBaseBrakingFriction() * GetBrakingFrictionScalar(); }

	// Falling
	
	virtual float GetGravityZ() const override;
	virtual FVector GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration) override;

public:	
	virtual void CalcStamina(float DeltaTime);
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration) override;

public:
	virtual bool CanWalkOffLedges() const override;
	virtual bool CanAttemptJump() const override;
	
public:
	/**
	 * Call CharacterOwner->OnStartWalk() if successful.
	 * In general, you should set bWantsToWalk instead to have the Walk persist during movement, or just use the Walk functions on the owning Character.
	 */
	virtual void Walk();

	/**
	 * Checks if default capsule size fits (no encroachment), and trigger OnEndWalk() on the owner if successful.
	 */
	virtual void UnWalk();

	/** Returns true if the character is allowed to Walk in the current state. */
	virtual bool CanWalkInCurrentState() const;
	
public:
	UFUNCTION(BlueprintCallable, Category="Character Movement: Walking")
	void SetMaxInputAngleSprint(float InMaxAngleSprint);
	
	virtual bool IsSprinting() const;

	/**
	 * Call CharacterOwner->OnStartSprint() if successful.
	 * In general, you should set bWantsToSprint instead to have the Sprint persist during movement, or just use the Sprint functions on the owning Character.
	 */
	virtual void Sprint();
	
	/**
	 * Checks if default capsule size fits (no encroachment), and trigger OnEndSprint() on the owner if successful.
	 */
	virtual void UnSprint();

	/** Returns true if the character is allowed to Sprint in the current state. */
	virtual bool CanSprintInCurrentState() const;

	/**
	 * This check ensures that we are not sprinting backward or sideways, while allowing leeway 
	 * This angle allows sprinting when holding forward, forward left, forward right
	 * but not left or right or backward)
	 */
	virtual bool IsSprintWithinAllowableInputAngle() const;

public:
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	float GetStaminaPct() const { return Stamina / MaxStamina; }

	float GetStamina() const { return Stamina; }
	float GetMaxStamina() const { return MaxStamina; }
	bool IsStaminaDrained() const { return bStaminaDrained; }
	virtual bool IsStaminaRecovered() const
	{
		return bStaminaRecoveryFromPct ? GetStaminaPct() >= StaminaRecoveryPct : GetStamina() >= StaminaRecoveryAmount;
	}

	void SetStamina(float NewStamina);
	void SetMaxStamina(float NewMaxStamina);
	void SetStaminaDrained(bool bNewValue);
	
protected:
	/*
	 * Drain state entry and exit is handled here. Drain state is used to prevent rapid re-entry of sprinting or other
	 * such abilities before sufficient stamina has regenerated. However, in the default implementation, 100%
	 * stamina must be regenerated. Consider overriding this, check the implementation's comment for more information.
	 */
	virtual void OnStaminaChanged(float PrevValue, float NewValue);
	virtual void OnMaxStaminaChanged(float PrevValue, float NewValue);
	virtual void OnStaminaDrained();
	virtual void OnStaminaDrainRecovered();

public:
	/* Haste Implementation */

	UPROPERTY(ReplicatedUsing=OnRep_HasteLevel)
	uint8 HasteLevel = NO_MODIFIER;

	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	bool IsHasteActive() const { return HasteLevel != NO_MODIFIER; }
	const FMovementModifierParams* GetHasteParams() const { return Haste.Find(GetHasteLevel()); }
	FGameplayTag GetHasteLevel() const { return HasteLevels.IsValidIndex(HasteLevel) ? HasteLevels[HasteLevel] : FGameplayTag::EmptyTag; }
	uint8 GetHasteLevelIndex(const FGameplayTag& Level) const { return HasteLevels.IndexOfByKey(Level) > INDEX_NONE ? HasteLevels.IndexOfByKey(Level) : NO_MODIFIER; }
	virtual bool CanHasteInCurrentState() const;

	float GetHasteSpeedScalar() const { return GetHasteParams() ? GetHasteParams()->MaxWalkSpeed : 1.f; }
	float GetHasteAccelScalar() const { return GetHasteParams() ? GetHasteParams()->MaxAcceleration : 1.f; }
	float GetHasteBrakingScalar() const { return GetHasteParams() ? GetHasteParams()->BrakingDeceleration : 1.f; }
	float GetHasteGroundFrictionScalar() const { return GetHasteParams() ? GetHasteParams()->GroundFriction : 1.f; }
	float GetHasteBrakingFrictionScalar() const { return GetHasteParams() ? GetHasteParams()->BrakingFriction : 1.f; }
	bool HasteAffectsRootMotion() const { return GetHasteParams() ? GetHasteParams()->bAffectsRootMotion : false; }

	UFUNCTION()
	void OnRep_HasteLevel();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void SetHasteByTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void ClearHaste();
	
	//UFUNCTION(Server, Reliable)		void ServerSetHasteLevel(const uint8 NewLevel);
	//UFUNCTION(Server, Reliable)		void ServerClearHaste();
	/* ~Haste Implementation */

public:
	/* Slow Implementation */
	
	UPROPERTY(ReplicatedUsing=OnRep_SlowLevel)
	uint8 SlowLevel = NO_MODIFIER;

	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	bool IsSlowActive() const { return SlowLevel != NO_MODIFIER; }
	const FMovementModifierParams* GetSlowParams() const { return Slow.Find(GetSlowLevel()); }
	FGameplayTag GetSlowLevel() const { return SlowLevels.IsValidIndex(SlowLevel) ? SlowLevels[SlowLevel] : FGameplayTag::EmptyTag; }
	uint8 GetSlowLevelIndex(const FGameplayTag& Level) const { return SlowLevels.IndexOfByKey(Level) > INDEX_NONE ? SlowLevels.IndexOfByKey(Level) : NO_MODIFIER; }
	virtual bool CanSlowInCurrentState() const;

	float GetSlowSpeedScalar() const { return GetSlowParams() ? GetSlowParams()->MaxWalkSpeed : 1.f; }
	float GetSlowAccelScalar() const { return GetSlowParams() ? GetSlowParams()->MaxAcceleration : 1.f; }
	float GetSlowBrakingScalar() const { return GetSlowParams() ? GetSlowParams()->BrakingDeceleration : 1.f; }
	float GetSlowGroundFrictionScalar() const { return GetSlowParams() ? GetSlowParams()->GroundFriction : 1.f; }
	float GetSlowBrakingFrictionScalar() const { return GetSlowParams() ? GetSlowParams()->BrakingFriction : 1.f; }
	bool SlowAffectsRootMotion() const { return GetSlowParams() ? GetSlowParams()->bAffectsRootMotion : false; }

	UFUNCTION()
	void OnRep_SlowLevel();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void SetSlowByTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void ClearSlow();

	//UFUNCTION(Server, Reliable)		void ServerSetSlowLevel(const uint8 NewLevel);
	//UFUNCTION(Server, Reliable)		void ServerClearSlow();
	/* ~Slow Implementation */

public:
	/* SlowFall Implementation */

	UPROPERTY(ReplicatedUsing=OnRep_SlowFallLevel)
	uint8 SlowFallLevel = NO_MODIFIER;

	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	bool IsSlowFallActive() const { return SlowFallLevel != NO_MODIFIER; }
	const FFallingModifierParams* GetSlowFallParams() const { return SlowFall.Find(GetSlowFallLevel()); }
	FGameplayTag GetSlowFallLevel() const { return SlowFallLevels.IsValidIndex(SlowFallLevel) ? SlowFallLevels[SlowFallLevel] : FGameplayTag::EmptyTag; }
	uint8 GetSlowFallLevelIndex(const FGameplayTag& Level) const { return SlowFallLevels.IndexOfByKey(Level) > INDEX_NONE ? SlowFallLevels.IndexOfByKey(Level) : NO_MODIFIER; }
	virtual bool CanSlowFallInCurrentState() const;

	virtual float GetSlowFallGravityZScalar() const { return GetSlowFallParams() ? GetSlowFallParams()->GetGravityScalar(Velocity) : 1.f; }
	virtual bool RemoveVelocityZOnSlowFallStart() const;

	UFUNCTION()
	void OnRep_SlowFallLevel();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void SetSlowFallByTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void ClearSlowFalling();

	//UFUNCTION(Server, Reliable)		void ServerSetSlowFallLevel(const uint8 NewLevel);
	//UFUNCTION(Server, Reliable)		void ServerClearSlowFalling();
	/* ~SlowFall Implementation */

public:
	virtual void UpdateModifierMovementState();
	
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;

protected:
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;

protected:
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp,
		FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase,
		bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

protected:
	virtual void TickCharacterPose(float DeltaTime) override;  // ACharacter::GetAnimRootMotionTranslationScale() is non-virtual so we have to duplicate this entire function

	
public:
	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_Character. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	
protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;


public:

	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void StartSprint();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void EndSprint();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void StartWalk();
	
	UFUNCTION(BlueprintCallable, Category="Custom Character Movement")
	void EndWalk();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
private:

	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting {false};
	UPROPERTY(ReplicatedUsing = OnRep_IsWalking)
	bool bIsWalking {false};

	UFUNCTION()
	void OnRep_IsSprinting();
	UFUNCTION()
	void OnRep_IsWalking();
};








class CUSTOMMOVEMENT_API FPredictedSavedMove : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	FPredictedSavedMove()
		: bWantsToWalk(0)
		, bWantsToSprint(0)
		, bStaminaDrained(false)
		, StartStamina(0)
		, EndStamina(0)
	{}

	virtual ~FPredictedSavedMove() override
	{}
	
	uint8 bWantsToWalk:1;
	uint8 bWantsToSprint:1;
	uint8 bStaminaDrained:1;
	
	float StartStamina;
	float EndStamina;
	
	uint8 HasteLevel = NO_MODIFIER;
	uint8 SlowLevel = NO_MODIFIER;
	uint8 SlowFallLevel = NO_MODIFIER;

	// Bit masks used by GetCompressedFlags() to encode movement information.
	/*enum CompressedFlagsExtra
	{
		FLAGEX_Walk			= 0x01,
		FLAGEX_Sprint		= 0x02,
		// Remaining bit masks are available for custom flags, but may be used in the future.
		FLAGEX_Custom_0		= 0x04,
		FLAGEX_Custom_1		= 0x08,
		FLAGEX_Custom_2		= 0x10,
		FLAGEX_Custom_3		= 0x08,
		FLAGEX_Custom_4		= 0x10,
		FLAGEX_Custom_5		= 0x80,
	};*/

	enum CompressedFlags
	{
		FLAG_Walk			= 0x10,
		FLAG_Sprint			= 0x20,
		FLAG_Custom_2		= 0x40,
		FLAG_Custom_3		= 0x80,
	};

	/** Returns a byte containing encoded special movement information (jumping, crouching, etc.)	 */
	//virtual uint8 GetCompressedFlagsExtra() const;
	
	virtual uint8 GetCompressedFlags() const override;
	
	/** Clear saved move properties, so it can be re-used. */
	virtual void Clear() override;
		
	/** Returns true if this move can be combined with NewMove for replication without changing any behavior */
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;

	/** Called to set up this saved move (when initially created) to make a predictive correction. */
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character & ClientData) override;

	/** Called before ClientUpdatePosition uses this SavedMove to make a predictive correction	 */
	virtual void PrepMoveFor(ACharacter* C) override;
	
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override;

	/** Combine this move with an older move and update relevant state. */
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;

};

class CUSTOMMOVEMENT_API FPredictedNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
	using Super = FNetworkPredictionData_Client_Character;

public:
	FPredictedNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{}

	virtual FSavedMovePtr AllocateNewMove() override;
};
