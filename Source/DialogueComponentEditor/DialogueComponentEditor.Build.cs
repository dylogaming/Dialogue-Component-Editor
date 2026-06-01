// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

using UnrealBuildTool;

public class DialogueComponentEditor : ModuleRules
{
	public DialogueComponentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"Projects",
			"LevelEditor",
			"EditorSubsystem",
			"PythonScriptPlugin",
			"Kismet",
			"BlueprintGraph",
			"KismetCompiler",
			"UMG",
			"UMGEditor",
			"InputCore",
			"AssetTools",
			"EditorScriptingUtilities",
			"Json",
			"JsonUtilities",
			"DeveloperSettings",
			"Sockets",
			"Networking",
			"Blutility",
			"Settings",
			"EditorStyle"
		});
	}
}
