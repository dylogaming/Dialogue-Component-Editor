// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#include "DCEditorSubsystem.h"
#include "IPythonScriptPlugin.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "UObject/UObjectGlobals.h"
#include "LevelEditor.h"
#include "UnrealEdMisc.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDCEditor);

void UDCEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BridgeDir = FPaths::ProjectDir() / TEXT("DialogueComponentEditor");
	PendingDir = BridgeDir / TEXT("pending");
	ResultsDir = BridgeDir / TEXT("results");
	CompletedDir = BridgeDir / TEXT("completed");

	EnsureDirectories();

	// Tick every 50ms (was 500ms) so dropped scripts are executed almost
	// immediately — makes actor-switches / live pushes feel instant.
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UDCEditorSubsystem::Tick),
		0.05f
	);

	// Hook map-change / world-save so we can release Python's grip on any
	// live widgets BEFORE the old world is torn down. Prevents the
	// "World Memory Leaks" fatal assert when the user switches levels while
	// the dialogue editor has recently called widget functions.
	if (GEditor)
	{
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (LevelEditor)
		{
			MapChangedHandle = LevelEditor->OnMapChanged().AddUObject(this, &UDCEditorSubsystem::OnMapChanged);
		}
		#if (ENGINE_MAJOR_VERSION < 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5)
		PreSaveWorldHandle = FEditorDelegates::PreSaveWorld.AddUObject(this, &UDCEditorSubsystem::OnPreSaveWorld);
#endif
	}

	UE_LOG(LogDCEditor, Log, TEXT("Claude Bridge initialized. Watching: %s"), *PendingDir);
}

void UDCEditorSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	#if (ENGINE_MAJOR_VERSION < 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 5)
	if (PreSaveWorldHandle.IsValid())
	{
		FEditorDelegates::PreSaveWorld.Remove(PreSaveWorldHandle);
	}
#endif
	if (MapChangedHandle.IsValid())
	{
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (LevelEditor) LevelEditor->OnMapChanged().Remove(MapChangedHandle);
	}
	UE_LOG(LogDCEditor, Log, TEXT("Claude Bridge shut down."));
	Super::Deinitialize();
}

void UDCEditorSubsystem::OnPreSaveWorld(uint32 SaveFlags, UWorld* World)
{
	ClearPythonWidgetRefs();
}

void UDCEditorSubsystem::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	// Fired before AND after a map change — clear Python refs + force GC on
	// TearDownWorld / LoadMap so the old world can actually collect.
	ClearPythonWidgetRefs();
}

void UDCEditorSubsystem::ClearPythonWidgetRefs()
{
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable()) return;
	// Drop Python's cached wrappers + hint the engine to collect. Wrapped in
	// try/except so an import error (e.g., headless) can't break the editor.
	PythonPlugin->ExecPythonCommand(TEXT(
		"try:\n"
		"    import gc as _cb_gc\n"
		"    _cb_gc.collect()\n"
		"except Exception: pass\n"
	));
	// UE's GC runs right after — forcing it here ensures the object graph
	// detaches before the engine tries to unload the old world.
	CollectGarbage(RF_NoFlags);
}

bool UDCEditorSubsystem::Tick(float DeltaTime)
{
	TArray<FString> ScriptFiles;
	IFileManager::Get().FindFiles(ScriptFiles, *(PendingDir / TEXT("*.py")), true, false);

	if (ScriptFiles.Num() == 0)
	{
		return true;
	}

	ScriptFiles.Sort();

	// Quiet mode: when DialogueComponentEditor/.quiet exists, suppress per-script logs
	// (user toggles it from the editor header → /log_level?verbose=0).
	const bool bQuiet = FPaths::FileExists(BridgeDir / TEXT(".quiet"));
	for (const FString& FileName : ScriptFiles)
	{
		FString FullPath = PendingDir / FileName;
		// Pull the human op tag out of filenames like `web_<op>_<uid>.py`.
		FString OpTag;
		{
			FString Base = FPaths::GetBaseFilename(FileName); // drops .py
			if (Base.StartsWith(TEXT("web_")))
			{
				FString Rest = Base.RightChop(4); // strip "web_"
				int32 LastUnderscore;
				if (Rest.FindLastChar('_', LastUnderscore) && LastUnderscore > 0)
				{
					OpTag = Rest.Left(LastUnderscore);
				}
				else
				{
					OpTag = Rest;
				}
			}
		}
		// Compact log: one line per op. When tagged, show the op name only.
		// Untagged (rare) shows the filename. "OK" line is dropped; errors
		// still log prominently inside ExecuteScriptQuiet.
		if (!bQuiet)
		{
			if (OpTag.IsEmpty()) { UE_LOG(LogDCEditor, Log, TEXT("%s"), *FileName); }
			else { UE_LOG(LogDCEditor, Log, TEXT("%s"), *OpTag); }
		}
		ExecuteScriptQuiet(FullPath, /*bQuiet=*/true, OpTag);

		FString DestPath = CompletedDir / FileName;
		IFileManager::Get().Move(*DestPath, *FullPath);
	}

	return true;
}

void UDCEditorSubsystem::ExecuteScript(const FString& ScriptPath)
{
	ExecuteScriptQuiet(ScriptPath, false, FString());
}

void UDCEditorSubsystem::ExecuteScriptQuiet(const FString& ScriptPath, bool bQuiet, const FString& OpTag)
{
	FString ScriptContent;
	if (!FFileHelper::LoadFileToString(ScriptContent, *ScriptPath))
	{
		// Errors ALWAYS log regardless of quiet mode.
		UE_LOG(LogDCEditor, Error, TEXT("Failed to read script: %s"), *ScriptPath);
		return;
	}

	FString ResultsNormalized = ResultsDir;
	ResultsNormalized.ReplaceInline(TEXT("\\"), TEXT("/"));

	FString Preamble = FString::Printf(
		TEXT("import sys, os\n")
		TEXT("CLAUDE_BRIDGE_RESULTS_DIR = r'%s'\n")
		TEXT("CLAUDE_BRIDGE_SCRIPT_NAME = r'%s'\n"),
		*ResultsNormalized,
		*FPaths::GetBaseFilename(ScriptPath)
	);

	FString FullScript = Preamble + TEXT("\n") + ScriptContent;

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (PythonPlugin && PythonPlugin->IsPythonAvailable())
	{
		bool bSuccess = PythonPlugin->ExecPythonCommand(*FullScript);
		if (!bSuccess)
		{
			if (OpTag.IsEmpty()) { UE_LOG(LogDCEditor, Error, TEXT("FAILED: %s"), *FPaths::GetCleanFilename(ScriptPath)); }
			else { UE_LOG(LogDCEditor, Error, TEXT("FAILED %s"), *OpTag); }
		}
		// Success is already logged once by the caller (Tick) via the compact
		// one-line-per-op format; no extra "OK" line here.
	}
	else
	{
		UE_LOG(LogDCEditor, Error, TEXT("Python Script Plugin is not available. Enable it in Edit > Plugins."));
	}
}

void UDCEditorSubsystem::EnsureDirectories()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*PendingDir);
	PlatformFile.CreateDirectoryTree(*ResultsDir);
	PlatformFile.CreateDirectoryTree(*CompletedDir);
}