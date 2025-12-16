// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CustomMovement : ModuleRules
{
	public CustomMovement(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", 
				"GameplayTags",
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"NetCore", 
				"GameplayAbilities",
			}
		);
	}
}
