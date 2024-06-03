// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Factory : ModuleRules
{
	public Factory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] 
		{ "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"Sockets","Networking","LuTCP","ProceduralMeshComponent" });
	}
}
