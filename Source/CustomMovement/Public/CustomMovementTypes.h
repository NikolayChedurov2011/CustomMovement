#pragma once

#include "CoreMinimal.h"
#include "CustomMovementTypes.generated.h"

UENUM(BlueprintType)
enum class ECustomMovementGaitMode : uint8
{
	Walk,
	Run,
	Sprint,
};

UENUM(BlueprintType)
enum class ECustomMovementStance : uint8
{
	Stand,
	Crouch,
	Prone,
};