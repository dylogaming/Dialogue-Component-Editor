// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DCEditorLibrary.generated.h"

/**
 * Blueprint Function Library exposed to Python.
 * Provides the Blueprint editing operations that UE's Python API doesn't expose natively.
 */
UCLASS()
class DIALOGUECOMPONENTEDITOR_API UDCEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ========== BLUEPRINT VARIABLE MANAGEMENT ==========

	/** Add a variable to a Blueprint. Type can be: bool, int, float, double, string, text, vector, rotator, transform, name, object, class */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool AddVariableToBlueprint(const FString& BlueprintPath, const FString& VarName, const FString& VarType, const FString& DefaultValue = TEXT(""));

	/** Remove a variable from a Blueprint */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool RemoveVariableFromBlueprint(const FString& BlueprintPath, const FString& VarName);

	/** Set a variable's default value on the Blueprint CDO */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool SetVariableDefault(const FString& BlueprintPath, const FString& VarName, const FString& Value);

	/** List all user-defined variables in a Blueprint. Returns JSON array. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString ListBlueprintVariables(const FString& BlueprintPath);

	// ========== SCS COMPONENT MANAGEMENT ==========

	/** Add a component to a Blueprint via SCS. Returns the component variable name. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddComponentToBlueprint(const FString& BlueprintPath, const FString& ComponentClassName, const FString& ComponentName = TEXT(""));

	/** Set a property on an SCS (construction script) component by name. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool SetComponentProperty(const FString& BlueprintPath, const FString& ComponentName, const FString& PropertyName, const FString& Value);

	/** List all SCS components in a Blueprint. Returns JSON array. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString ListBlueprintComponents(const FString& BlueprintPath);

	/** Rename an SCS component */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool RenameComponent(const FString& BlueprintPath, const FString& OldName, const FString& NewName);

	// ========== WIDGET BLUEPRINT EDITING ==========

	/** Add a widget to a Widget Blueprint's widget tree. Returns the widget name. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddWidgetToWidgetBlueprint(const FString& WidgetBlueprintPath, const FString& WidgetClassName, const FString& ParentWidgetName = TEXT(""));

	/** Set a property on a widget inside a Widget Blueprint */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool SetWidgetProperty(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& PropertyName, const FString& Value);

	/** List all widgets in a Widget Blueprint. Returns JSON array. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString ListWidgets(const FString& WidgetBlueprintPath);

	// ========== BLUEPRINT GRAPH / NODE CREATION ==========

	/** Remove a node from the event graph by GUID */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool RemoveNodeByGuid(const FString& BlueprintPath, const FString& NodeGuid);

	/** Add a custom event to a Blueprint's event graph */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool AddCustomEvent(const FString& BlueprintPath, const FString& EventName, int32 NodeX = 0, int32 NodeY = 0);

	/** Add a function call node to a Blueprint graph. Returns the node's GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddFunctionCallNode(const FString& BlueprintPath, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX = 300, int32 NodeY = 0);

	/** Add a function call node inside a Composite node's subgraph. Returns the node's GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddFunctionCallNodeToComposite(const FString& BlueprintPath, const FString& CompositeNodeGuid, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX = 300, int32 NodeY = 0);

	/** Add a function call node to a named function graph (not the EventGraph). Returns the node's GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddFunctionCallNodeToFunctionGraph(const FString& BlueprintPath, const FString& FunctionGraphName, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX = 300, int32 NodeY = 0);

	/** Connect two nodes inside a named function graph by their pin names. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool ConnectNodesInFunctionGraph(const FString& BlueprintPath, const FString& FunctionGraphName, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName);

	/** Add a SpawnActorFromClass node with the class pre-set. Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddSpawnActorNode(const FString& BlueprintPath, const FString& ActorClassPath, int32 NodeX = 500, int32 NodeY = 0);

	/** Add a built-in event node (e.g., ReceiveActorBeginOverlap, ReceiveAnyDamage). Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddBuiltInEvent(const FString& BlueprintPath, const FString& EventFunctionName, int32 NodeX = 0, int32 NodeY = 0);

	/** Add an If/Then/Else (Branch) node. Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddBranchNode(const FString& BlueprintPath, int32 NodeX = 0, int32 NodeY = 0);

	/** Add a standard Create Widget node with class pre-set. Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddCreateWidgetNode(const FString& BlueprintPath, const FString& WidgetClassPath, int32 NodeX = 0, int32 NodeY = 0);

	// SpawnBlueprintActor not included in DCE

	/** Connect two nodes by their pin names. Nodes identified by GUID or name. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool ConnectNodes(const FString& BlueprintPath, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName);

	/** Set a default value on a node's pin. Node identified by GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool SetNodePinDefault(const FString& BlueprintPath, const FString& NodeGuid, const FString& PinName, const FString& DefaultValue);

	/** Add a variable get node to the event graph. Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddVariableGetNode(const FString& BlueprintPath, const FString& VariableName);

	/** Add a variable set node to the event graph. Returns GUID. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString AddVariableSetNode(const FString& BlueprintPath, const FString& VariableName);

	/** Describe all nodes and connections in a Blueprint graph. Returns JSON. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString DescribeGraph(const FString& BlueprintPath, const FString& GraphName = TEXT("EventGraph"));

	/** Describe the subgraph inside a Collapsed/Composite node by its GUID. Returns JSON. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString DescribeCompositeGraph(const FString& BlueprintPath, const FString& CompositeNodeGuid);

	// ========== COMMENT BOXES ==========

	/** Add a comment box around specified node GUIDs with a description. Uses the tag prefix from plugin settings. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static void AddCommentBox(const FString& BlueprintPath, const FString& Comment, const TArray<FString>& NodeGuids);

	// ========== PLAY IN EDITOR ==========

	/** Start Play In Editor (actual play, not simulate). Returns true if started. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool StartPIE();

	/** Check if a Blueprint has compile errors. Returns empty string if clean, error message if not. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString CheckBlueprintCompileStatus(const FString& BlueprintPath);

	// ========== BLUEPRINT COMPILATION ==========

	/** Compile and save a Blueprint */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static bool CompileAndSaveBlueprint(const FString& BlueprintPath);

	// ========== OUTPUT LOG CAPTURE ==========

	/** Start capturing output log messages */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static void StartLogCapture();

	/** Stop capturing and return all captured log messages as JSON array */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString StopLogCapture();

	/** Get recent output log lines (last N lines). Returns JSON array. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString GetRecentLogs(int32 NumLines = 100);

	// ========== ASSET INTROSPECTION ==========

	/** Describe a Blueprint's full structure (variables, components, graphs). Returns JSON. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString DescribeBlueprint(const FString& BlueprintPath);

	// ========== GENERIC PROPERTY TEXT I/O ==========

	/**
	 * Get a property on an actor as UE's text format (same format as copy/paste in details panel).
	 * ActorLabel: the display label of the actor in the level (case-insensitive). Searches editor world, falls back to PIE worlds.
	 * PropertyName: display name of the property (spaces, emojis, etc allowed — matches Blueprint variable display names).
	 * Returns JSON: {"ok":bool, "text":string, "error":string}
	 */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString GetActorPropertyAsText(const FString& ActorLabel, const FString& PropertyName);

	/**
	 * Set a property on an actor from UE's text format. Uses FProperty::ImportText_Direct under the hood.
	 * Also works for component properties — searches the actor's components if the property isn't found on the actor itself.
	 * Returns JSON: {"ok":bool, "error":string}
	 */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString SetActorPropertyFromText(const FString& ActorLabel, const FString& PropertyName, const FString& Text);

	/** Returns JSON map of all UEnums (optionally filtered by name prefix) -> { entries: [{name, display, value}] } */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString DumpEnums(const FString& NamePrefix = TEXT(""));

	/** Describe a UScriptStruct (UserDefinedStruct or native). Recursively dumps fields, types, nested structs, and enum refs. StructPath: e.g. "/Game/Path/S_Foo.S_Foo" */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString DescribeStruct(const FString& StructPath);

	/** Call a no-arg BlueprintCallable function on any live UserWidget instance of the given WidgetBlueprint. Useful for refreshing Editor Utility Widgets. Returns JSON {ok, called_on: [class names]}. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString CallFunctionOnLiveWidget(const FString& WidgetBlueprintPath, const FString& FunctionName);

	/** Play a sound asset in the editor (preview, not PIE). Uses GEditor->PlayPreviewSound. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString PlayEditorSound(const FString& SoundAssetPath);

	/** Set an int property on every live EditorUtilityWidget matching WidgetBlueprintPath,
	 *  then call a refresh function with that int. All C++ — no Python widget wrapper
	 *  is ever held, so this avoids the FPyReferenceCollector leak that pins the level. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString SelectBranchOnLiveWidget(const FString& WidgetBlueprintPath, const FString& PropertyName, int32 Value, const FString& RefreshFunctionName);

	/** Read an int property from the first live EditorUtilityWidget of the given class.
	 *  Returns JSON {ok, idx} — no Python widget wrapper allocated, so no level-pin. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString GetLiveWidgetIntProperty(const FString& WidgetBlueprintPath, const FString& PropertyName);

	/** Render a Texture2D asset to a PNG file via FImageUtils. Bypasses UE's
	 *  UExporter path that logs "No png exporter found" warnings. Returns JSON
	 *  {ok, path} on success. */
	UFUNCTION(BlueprintCallable, Category = "DialogueComponentEditor")
	static FString ExportTextureAsPNG(const FString& AssetPath, const FString& OutFilename);

private:
	/** Helper: find an actor by label across editor + PIE worlds */
	static AActor* FindActorByLabel(const FString& Label);
	/** Helper: find a property on an object or its components, return Object+Property pair */
	static bool ResolvePropertyOnActor(AActor* Actor, const FString& PropertyName, UObject*& OutOwner, FProperty*& OutProp);

private:
	/** Helper to load and validate a Blueprint */
	static UBlueprint* LoadBlueprintAsset(const FString& Path);

	/** Helper to resolve a pin type from a string name */
	static FEdGraphPinType ResolvePinType(const FString& TypeName);

	/** Log capture storage */
	static TArray<FString> CapturedLogs;
	static bool bCapturingLogs;
	static FDelegateHandle LogCaptureDelegateHandle;
};
