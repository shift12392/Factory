// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MuduoUE : ModuleRules
{
	public MuduoUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MuduoCore",
			});
	}
}
