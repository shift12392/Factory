// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MuduoCore : ModuleRules
{
	public MuduoCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		CppStandard = CppStandardVersion.Cpp20;

		bUseRTTI = false;
		bEnableExceptions = false;

		PublicDependencyModuleNames.Add("Core");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// 直接使用 winsock2，不再依赖 Sockets 模块，也不需要修改引擎源码。
			PublicDefinitions.Add("MUDUONET_PLATFORM_WINDOWS=1");
			PublicSystemLibraries.Add("ws2_32.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDefinitions.Add("MUDUONET_PLATFORM_LINUX=1");
		}
		else
		{
			// 抽象层已为其他平台预留位置，但实现尚未提供，编译期直接报错。
			PublicDefinitions.Add("MUDUONET_PLATFORM_UNSUPPORTED=1");
		}
	}
}
