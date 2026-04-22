// Copyright 2026 DYLO Gaming. All Rights Reserved.

#include "DialogueComponentEditorModule.h"
#include "DCEditorSubsystem.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#define LOCTEXT_NAMESPACE "FDialogueComponentEditorModule"

// Starting port — will auto-increment if already in use.
static const int32 DCE_PORT_START = 8766;
static const int32 DCE_PORT_END = 8786; // Try up to 20 ports

// Custom style set — lives for the module's lifetime so the toolbar icon stays registered.
static TSharedPtr<FSlateStyleSet> GDCEditorStyleSet;
static bool GDCEditorStyleRegistered = false;

// Helper: check if a port is available by trying to bind a socket.
static bool IsPortAvailable(int32 Port)
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub) return true; // Assume available if we can't check
	FSocket* TestSocket = SocketSub->CreateSocket(NAME_Stream, TEXT("DCE port test"), false);
	if (!TestSocket) return true;
	TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
	Addr->SetIp(0x7F000001); // 127.0.0.1
	Addr->SetPort(Port);
	bool bBound = TestSocket->Bind(*Addr);
	TestSocket->Close();
	SocketSub->DestroySocket(TestSocket);
	return bBound;
}

void FDialogueComponentEditorModule::StartupModule()
{
	// Register custom toolbar icon from Resources/Icon40.png
	GDCEditorStyleSet = MakeShareable(new FSlateStyleSet(FName("DCEditorStyle")));
	FString PluginBaseDir = FPaths::ConvertRelativePathToFull(
		IPluginManager::Get().FindPlugin("DialogueComponentEditor")->GetBaseDir());
	FString Icon40Path = FPaths::Combine(PluginBaseDir, TEXT("Resources/Icon40.png"));
	FString Icon20Path = FPaths::Combine(PluginBaseDir, TEXT("Resources/Icon20.png"));
	GDCEditorStyleSet->SetContentRoot(FPaths::Combine(PluginBaseDir, TEXT("Resources")));
	UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Icon path: %s (exists: %s)"), *Icon40Path, FPaths::FileExists(Icon40Path) ? TEXT("yes") : TEXT("no"));
	if (FPaths::FileExists(Icon40Path))
	{
		GDCEditorStyleSet->Set("DCEditor.ToolbarIcon", new FSlateImageBrush(Icon40Path, FVector2D(40, 40)));
		GDCEditorStyleSet->Set("DCEditor.ToolbarIcon.Small", new FSlateImageBrush(
			FPaths::FileExists(Icon20Path) ? Icon20Path : Icon40Path, FVector2D(20, 20)));
		FSlateStyleRegistry::RegisterSlateStyle(*GDCEditorStyleSet.Get());
		GDCEditorStyleRegistered = true;
	}

	// Menus aren't ready at module load — defer one tick.
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDialogueComponentEditorModule::RegisterToolbarButton));
}

void FDialogueComponentEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	KillServer();

	// Unregister custom style
	if (GDCEditorStyleRegistered && GDCEditorStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*GDCEditorStyleSet.Get());
		GDCEditorStyleRegistered = false;
	}
	GDCEditorStyleSet.Reset();
}

void FDialogueComponentEditorModule::RegisterToolbarButton()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	if (!Menu) return;

	// Use custom icon if registered, fall back to built-in icon
	FSlateIcon ButtonIcon = GDCEditorStyleRegistered
		? FSlateIcon(FName("DCEditorStyle"), "DCEditor.ToolbarIcon", "DCEditor.ToolbarIcon.Small")
		: FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings");

	FToolMenuSection& Section = Menu->FindOrAddSection("DialogueComponentEditor");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenDialogueComponentEditor",
		FUIAction(FExecuteAction::CreateRaw(this, &FDialogueComponentEditorModule::OnButtonClicked)),
		LOCTEXT("ButtonLabel", "Dialogue Editor"),
		LOCTEXT("ButtonTooltip", "Launch the Dialogue Component Editor in your browser"),
		ButtonIcon
	));

	UToolMenus::Get()->RefreshAllWidgets();
}

void FDialogueComponentEditorModule::OnButtonClicked()
{
	// If the server is already running, re-open the browser (user closed the tab).
	if (ServerProcess.IsValid() && FPlatformProcess::IsProcRunning(ServerProcess))
	{
		UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Server already running on port %d — reopening browser."), RunningPort);
		FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/"), RunningPort);
		FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
		return;
	}

	// Locate the bundled server script inside the plugin's Content/Python/.
	FString PluginContentDir = FPaths::Combine(
		FPaths::ProjectPluginsDir(),
		TEXT("DialogueComponentEditor/Content"));
	FString ScriptPath = FPaths::Combine(
		PluginContentDir, TEXT("Python/dialogue_component_editor_server.py"));
	FString WebDir = FPaths::Combine(PluginContentDir, TEXT("Web"));

	ScriptPath = FPaths::ConvertRelativePathToFull(ScriptPath);
	WebDir = FPaths::ConvertRelativePathToFull(WebDir);

	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogDCEditor, Error, TEXT("[DialogueComponentEditor] Server script not found: %s"), *ScriptPath);
		return;
	}

	// Bridge dir: DCE's own folder for pending/results/completed scripts.
	FString BridgeDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("DialogueComponentEditor")));

	// Find an available port
	RunningPort = 0;
	for (int32 Port = DCE_PORT_START; Port <= DCE_PORT_END; ++Port)
	{
		if (IsPortAvailable(Port))
		{
			RunningPort = Port;
			break;
		}
	}
	if (RunningPort == 0)
	{
		UE_LOG(LogDCEditor, Error, TEXT("[DialogueComponentEditor] No available port in range %d-%d"), DCE_PORT_START, DCE_PORT_END);
		return;
	}

	// Resolve Python executable: prefer UE's bundled Python (no user install
	// needed), fall back to system PATH as last resort. Cross-platform paths.
	FString PythonExe;
	bUsingBundledPython = false;
	{
		TArray<FString> Candidates = {
#if PLATFORM_WINDOWS
			FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Win64/python3.exe")),
			FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Win64/python.exe")),
#elif PLATFORM_MAC
			FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3")),
#elif PLATFORM_LINUX
			FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3")),
#endif
		};
		for (const FString& Cand : Candidates)
		{
			FString Full = FPaths::ConvertRelativePathToFull(Cand);
			if (FPaths::FileExists(Full))
			{
				PythonExe = Full;
				bUsingBundledPython = true;
				break;
			}
		}
		if (PythonExe.IsEmpty())
		{
			PythonExe = TEXT("python");
			UE_LOG(LogDCEditor, Warning, TEXT("[DialogueComponentEditor] UE bundled Python not found, falling back to system PATH."));
		}
		ResolvedPythonPath = PythonExe;
		UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Using Python: %s (%s)"),
			*PythonExe, bUsingBundledPython ? TEXT("UE bundled") : TEXT("system PATH"));
	}
	FString Args = FString::Printf(
		TEXT("\"%s\" --port %d --bridge \"%s\" --webdir \"%s\""),
		*ScriptPath, RunningPort, *BridgeDir, *WebDir);

	UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Launching: %s %s"), *PythonExe, *Args);

	uint32 PID = 0;
	ServerProcess = FPlatformProcess::CreateProc(
		*PythonExe, *Args,
		false,  // bLaunchDetached
		true,   // bLaunchHidden — no console window, no taskbar entry
		true,   // bLaunchReallyHidden
		&PID, 0, nullptr, nullptr);

	if (!ServerProcess.IsValid())
	{
		UE_LOG(LogDCEditor, Error, TEXT("[DialogueComponentEditor] Failed to spawn server."));
		return;
	}

	UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Server started (PID %u) on port %d"), PID, RunningPort);

	// Open browser after a short delay so the server can bind.
	int32 PortCopy = RunningPort;
	FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/"), PortCopy);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[URL](float) -> bool {
			FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
			return false;
		}
	), 1.5f);
}

void FDialogueComponentEditorModule::KillServer()
{
	if (ServerProcess.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(ServerProcess))
		{
			FPlatformProcess::TerminateProc(ServerProcess, /*bKillTree=*/true);
			UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Bridge server terminated."));
		}
		FPlatformProcess::CloseProc(ServerProcess);
		ServerProcess.Reset();
	}
	RunningPort = 0;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDialogueComponentEditorModule, DialogueComponentEditor)
