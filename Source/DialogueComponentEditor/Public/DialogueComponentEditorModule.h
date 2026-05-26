// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDialogueComponentEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterToolbarButton();
	void PopulateOptionsMenu(class UToolMenu* Menu);
	void OnButtonClicked();
	void LaunchEUW();

	/** Handle to the bridge-server subprocess. Null when not running. */
	FProcHandle ServerProcess;

	/** Resolved Python path — stored so "already running" can report it. */
	FString ResolvedPythonPath;

	/** Whether the resolved path is UE-bundled or system fallback. */
	bool bUsingBundledPython = false;

	/** The port the server is actually running on (auto-assigned). */
	int32 RunningPort = 0;

	/** Kill the bridge subprocess if it's still alive. */
	void KillServer();
};
