// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#include "DialogueComponentEditorModule.h"
#include "DCEditorSubsystem.h"
#include "DCEditorSettings.h"
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
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "ISettingsModule.h"
// UEditorStyleSettings header path differs by engine version. UE 5.0 ships it under the
// EditorStyle module's Classes/ subfolder; 5.4+ relocated it under UnrealEd's Settings/.
#if defined(__has_include) && __has_include("Settings/EditorStyleSettings.h")
#include "Settings/EditorStyleSettings.h"
#else
#include "Classes/EditorStyleSettings.h"
#endif
#include "Settings/LevelEditorPlaySettings.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FDialogueComponentEditorModule"

// Show a transient Unreal editor toast (bottom-right) so clicking the toolbar
// button gives immediate in-editor feedback that the launch was triggered.
static void ShowDCEEditorNotification(const FText& Message, bool bSuccess = true)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 4.0f;
	Info.bFireAndForget = true;
	Info.bUseSuccessFailIcons = true;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

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

	// Split-button: action button (icon = launch) + arrow combo (settings dropdown).
	// Same pattern as Skeletal Mesh Editor's Reimport split-button.
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenDialogueComponentEditor",
		FUIAction(FExecuteAction::CreateRaw(this, &FDialogueComponentEditorModule::OnButtonClicked)),
		LOCTEXT("ButtonLabel", "Dialogue Editor"),
		LOCTEXT("ButtonTooltip", "Launch the Dialogue Component Editor in your browser. Click the arrow for options."),
		ButtonIcon
	));

	Section.AddEntry(FToolMenuEntry::InitComboButton(
		"OpenDialogueComponentEditorArrow",
		FUIAction(),
		FNewToolMenuDelegate::CreateRaw(this, &FDialogueComponentEditorModule::PopulateOptionsMenu),
		FText::GetEmpty(),
		LOCTEXT("ArrowTooltip", "Dialogue Editor options: browser launch, legacy Editor Utility Widget, project settings."),
		FSlateIcon(),
		/*bSimpleComboBox=*/true
	));

	UToolMenus::Get()->RefreshAllWidgets();
}

void FDialogueComponentEditorModule::PopulateOptionsMenu(UToolMenu* Menu)
{
	FToolMenuSection& LaunchSection = Menu->AddSection("DCELaunch", LOCTEXT("LaunchHeader", "Dialogue Editor"));
	LaunchSection.AddMenuEntry(
		"LaunchDialogueEditor",
		LOCTEXT("LaunchLabel", "Launch Dialogue Editor"),
		LOCTEXT("LaunchTip", "Launch the browser editor (same as clicking the toolbar icon)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDialogueComponentEditorModule::OnButtonClicked))
	);
	// On-demand launch of the legacy Editor Utility Widget. Replaces the old
	// "Also launch ..." toggle: the browser editor is the primary UI, so the EUW
	// is now opt-in per click instead of auto-spawning on every editor launch.
	LaunchSection.AddMenuEntry(
		"LaunchEUW",
		LOCTEXT("LaunchEUWLabel", "Launch Editor Utility Widget"),
		LOCTEXT("LaunchEUWTip", "Open the legacy in-editor Editor Utility Widget now. Set its asset path in Project Settings -> Plugins -> Claude Bridge."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDialogueComponentEditorModule::MaybeLaunchEUW))
	);

	FToolMenuSection& Behavior = Menu->AddSection("DCEBehavior", LOCTEXT("BehaviorHeader", "Behavior"));

	auto MakeToggle = [&Behavior](FName Name, FText Label, FText Tooltip, bool UDCEditorSettings::* Field)
	{
		Behavior.AddMenuEntry(
			Name, Label, Tooltip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Field]()
				{
					UDCEditorSettings* S = GetMutableDefault<UDCEditorSettings>();
					S->*Field = !(S->*Field);
					S->SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Field]()
				{
					return GetDefault<UDCEditorSettings>()->*Field;
				})
			),
			EUserInterfaceActionType::ToggleButton
		);
	};

	MakeToggle("OpenBrowser",
		LOCTEXT("OpenBrowserLabel", "Open browser after launch"),
		LOCTEXT("OpenBrowserTip", "Automatically open the editor in your default browser when the bridge server starts. Turn off to copy the URL into a different browser yourself."),
		&UDCEditorSettings::bOpenBrowserAfterLaunch);

	MakeToggle("VerboseLogging",
		LOCTEXT("VerboseLoggingLabel", "Verbose logging"),
		LOCTEXT("VerboseLoggingTip", "Log every bridge operation. Useful for debugging, noisy otherwise."),
		&UDCEditorSettings::bVerboseLogging);

	FToolMenuSection& Appearance = Menu->AddSection("DCEAppearance", LOCTEXT("AppearanceHeader", "Editor Appearance"));
	Appearance.AddMenuEntry(
		"UseSmallToolbarIcons",
		LOCTEXT("UseSmallToolbarIconsLabel", "Use small toolbar icons"),
		LOCTEXT("UseSmallToolbarIconsTip", "Toggles UE's Editor Preferences -> General - Appearance -> User Interface -> Use Small Tool Bar Icons. Shrinks toolbar buttons across the editor, reclaiming vertical space."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([]()
			{
				UEditorStyleSettings* S = GetMutableDefault<UEditorStyleSettings>();
				S->bUseSmallToolBarIcons = !S->bUseSmallToolBarIcons;
				S->PostEditChange();
				S->SaveConfig();
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([]()
			{
				return GetDefault<UEditorStyleSettings>()->bUseSmallToolBarIcons != 0;
			})
		),
		EUserInterfaceActionType::ToggleButton
	);

	// Mirrors UE's Editor Preferences -> Level Editor - Play -> Play in New Window -> Always On Top.
	// Bitfield on ULevelEditorPlaySettings, so we toggle through GetMutableDefault rather than a
	// generic property helper. Keeps the PIE window above other windows when Play In Editor starts.
	Appearance.AddMenuEntry(
		"PIEAlwaysOnTop",
		LOCTEXT("PIEAlwaysOnTopLabel", "PIE window always on top"),
		LOCTEXT("PIEAlwaysOnTopTip", "Toggles UE's Editor Preferences -> Level Editor - Play -> Play in New Window -> Always On Top. Keeps the PIE window above other windows when you start Play In Editor."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([]()
			{
				ULevelEditorPlaySettings* S = GetMutableDefault<ULevelEditorPlaySettings>();
				S->PIEAlwaysOnTop = !S->PIEAlwaysOnTop;
				S->PostEditChange();
				S->SaveConfig();
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([]()
			{
				return GetDefault<ULevelEditorPlaySettings>()->PIEAlwaysOnTop != 0;
			})
		),
		EUserInterfaceActionType::ToggleButton
	);

	FToolMenuSection& Misc = Menu->AddSection("DCEMisc", FText::GetEmpty());
	Misc.AddMenuEntry(
		"OpenProjectSettings",
		LOCTEXT("OpenSettingsLabel", "Open Project Settings..."),
		LOCTEXT("OpenSettingsTip", "Open Project Settings -> Plugins -> Claude Bridge for the full settings list."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (ISettingsModule* SettingsMod = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
			{
				SettingsMod->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("Claude Bridge"));
			}
		}))
	);
}

// On-demand launch of the legacy Editor Utility Widget, fired from the toolbar
// dropdown's "Launch Editor Utility Widget" button. No longer gated on an
// auto-launch toggle — the EUW only opens when the user explicitly asks for it.
void FDialogueComponentEditorModule::MaybeLaunchEUW()
{
	const UDCEditorSettings* Settings = GetDefault<UDCEditorSettings>();
	if (!Settings || !Settings->EUWBlueprintPath.IsValid())
	{
		UE_LOG(LogDCEditor, Warning, TEXT("[DialogueComponentEditor] No Editor Utility Widget asset path set. Set it in Project Settings -> Plugins -> Claude Bridge."));
		return;
	}
	UObject* AssetObj = Settings->EUWBlueprintPath.TryLoad();
	UEditorUtilityWidgetBlueprint* EUW = Cast<UEditorUtilityWidgetBlueprint>(AssetObj);
	if (!EUW)
	{
		UE_LOG(LogDCEditor, Warning, TEXT("[DialogueComponentEditor] EUW path %s is not a valid EditorUtilityWidgetBlueprint."),
			*Settings->EUWBlueprintPath.ToString());
		return;
	}
	if (!GEditor) return;
	UEditorUtilitySubsystem* EUSub = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (EUSub)
	{
		EUSub->SpawnAndRegisterTab(EUW);
		UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Launched EUW: %s"), *Settings->EUWBlueprintPath.ToString());
	}
}

void FDialogueComponentEditorModule::OnButtonClicked()
{
	const UDCEditorSettings* Settings = GetDefault<UDCEditorSettings>();
	const bool bOpenBrowser = Settings ? Settings->bOpenBrowserAfterLaunch : true;

	// If the server is already running, re-open the browser (user closed the tab).
	if (ServerProcess.IsValid() && FPlatformProcess::IsProcRunning(ServerProcess))
	{
		UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Server already running on port %d%s."), RunningPort,
			bOpenBrowser ? TEXT(" — reopening browser") : TEXT(""));
		ShowDCEEditorNotification(bOpenBrowser
			? FText::Format(LOCTEXT("DCEAlreadyRunningReopen", "Dialogue Editor already running — reopening browser (port {0})."), FText::AsNumber(RunningPort, &FNumberFormattingOptions::DefaultNoGrouping()))
			: LOCTEXT("DCEAlreadyRunning", "Dialogue Editor server already running."));
		if (bOpenBrowser)
		{
			FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/"), RunningPort);
			FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
		}
		return;
	}

	// Locate the bundled server script inside the plugin's Content/Python/.
	// Use IPluginManager so the path resolves whether the plugin is installed
	// project-side, engine-side, or as a Marketplace install.
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DialogueComponentEditor"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogDCEditor, Error, TEXT("[DialogueComponentEditor] Plugin not found via IPluginManager."));
		return;
	}
	FString PluginContentDir = Plugin->GetContentDir();
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
		ShowDCEEditorNotification(LOCTEXT("DCELaunchFailed", "Dialogue Editor failed to start the bridge server. See the Output Log."), /*bSuccess=*/false);
		return;
	}

	UE_LOG(LogDCEditor, Log, TEXT("[DialogueComponentEditor] Server started (PID %u) on port %d"), PID, RunningPort);
	ShowDCEEditorNotification(FText::Format(
		LOCTEXT("DCELaunching", "Launching Dialogue Editor on port {0}…"),
		FText::AsNumber(RunningPort, &FNumberFormattingOptions::DefaultNoGrouping())));

	// Open browser after a short delay so the server can bind.
	if (bOpenBrowser)
	{
		int32 PortCopy = RunningPort;
		FString URL = FString::Printf(TEXT("http://127.0.0.1:%d/"), PortCopy);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[URL](float) -> bool {
				FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
				return false;
			}
		), 1.5f);
	}
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
