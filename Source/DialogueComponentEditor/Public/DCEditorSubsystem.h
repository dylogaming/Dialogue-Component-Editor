// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "DCEditorSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDCEditor, Log, All);

/**
 * Watches a project folder for Python scripts and auto-executes them.
 * This is the bridge that lets Claude Code control the Unreal Editor.
 */
UCLASS()
class DIALOGUECOMPONENTEDITOR_API UDCEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void ExecuteScript(const FString& ScriptPath);
	void ExecuteScriptQuiet(const FString& ScriptPath, bool bQuiet, const FString& OpTag);
	void EnsureDirectories();

	FString BridgeDir;
	FString PendingDir;
	FString ResultsDir;
	FString CompletedDir;

	FTSTicker::FDelegateHandle TickDelegateHandle;

	// Level-change cleanup: Python keeps live UObject references (via
	// FPyReferenceCollector) for everything it has touched, including live
	// Editor Utility Widgets this plugin calls into. When the user switches
	// levels, those refs pin the old world through the widget → W_Dialogue →
	// Widget Creator → level actor chain, causing "World Memory Leaks" fatal
	// asserts. On map change we force a Python gc.collect() + UE GC to
	// release the widget refs before the old world is torn down.
	void OnPreSaveWorld(uint32 SaveFlags, class UWorld* World);
	void OnMapChanged(class UWorld* World, EMapChangeType ChangeType);
	FDelegateHandle PreSaveWorldHandle;
	FDelegateHandle MapChangedHandle;
	void ClearPythonWidgetRefs();
};