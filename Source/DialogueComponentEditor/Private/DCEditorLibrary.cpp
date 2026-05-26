// Copyright DYLO Gaming LLC 2026 All Rights Reserved.

#include "DCEditorLibrary.h"
#include "DCEditorSubsystem.h"

#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_IfThenElse.h"
#include "EdGraphNode_Comment.h"
#include "DCEditorSettings.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_Composite.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/Button.h"

#include "EditorAssetLibrary.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
// ======================== UE VERSION COMPAT MACROS ========================
#ifndef ANY_PACKAGE
#define ANY_PACKAGE nullptr
#endif
// API cutoff: UE 5.5+ uses the new APIs. UE 5.0-5.4 uses the old ones.
// Originally guarded at 5.7+, but Epic actually removed these in 5.5.
#define DCE_USE_NEW_API ((ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5))
#if DCE_USE_NEW_API
  #define DCE_ImportText(Prop, Text, ValuePtr, PPF, Owner) (Prop)->ImportText_Direct(Text, ValuePtr, Owner, PPF)
#else
  #define DCE_ImportText(Prop, Text, ValuePtr, PPF, Owner) (Prop)->ImportText(Text, ValuePtr, PPF, Owner)
#endif
#if DCE_USE_NEW_API
  #define DCE_TSF_RGBA8_CHECK(Fmt) false
#else
  #define DCE_TSF_RGBA8_CHECK(Fmt) ((Fmt) == TSF_RGBA8)
#endif
#if DCE_USE_NEW_API
  #define DCE_CompressImageArray(W, H, Colors, Out) { TArray64<uint8> _pngOut; FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), _pngOut); Out.Append(_pngOut); }
#else
  #define DCE_CompressImageArray(W, H, Colors, Out) FImageUtils::CompressImageArray(W, H, Colors, Out)
#endif
// =========================================================================

// Static member initialization
TArray<FString> UDCEditorLibrary::CapturedLogs;
bool UDCEditorLibrary::bCapturingLogs = false;
FDelegateHandle UDCEditorLibrary::LogCaptureDelegateHandle;

// ============================================================
// HELPERS
// ============================================================

UBlueprint* UDCEditorLibrary::LoadBlueprintAsset(const FString& Path)
{
	// Try loading directly first
	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);

	if (!Asset)
	{
		// Try with explicit object name (e.g., /Game/Blueprints/BP_Turret.BP_Turret)
		FString AssetName = FPaths::GetBaseFilename(Path);
		FString FullPath = Path + TEXT(".") + AssetName;
		Asset = UEditorAssetLibrary::LoadAsset(FullPath);
	}

	if (!Asset)
	{
		// Try StaticLoadObject as fallback
		Asset = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Path);
	}

	if (!Asset)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Failed to load asset: %s"), *Path);
		return nullptr;
	}
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	if (!BP)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Asset is not a Blueprint: %s"), *Path);
		return nullptr;
	}
	return BP;
}

FEdGraphPinType UDCEditorLibrary::ResolvePinType(const FString& TypeName)
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; // default

	FString Lower = TypeName.ToLower();

	if (Lower == TEXT("bool") || Lower == TEXT("boolean"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Lower == TEXT("int") || Lower == TEXT("integer"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Lower == TEXT("float"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (Lower == TEXT("double"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (Lower == TEXT("string") || Lower == TEXT("fstring"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Lower == TEXT("text") || Lower == TEXT("ftext"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Lower == TEXT("name") || Lower == TEXT("fname"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Lower == TEXT("vector") || Lower == TEXT("fvector"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Lower == TEXT("rotator") || Lower == TEXT("frotator"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Lower == TEXT("transform") || Lower == TEXT("ftransform"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Lower == TEXT("linearcolor") || Lower == TEXT("flinearcolor") || Lower == TEXT("color"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (Lower == TEXT("object"))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else
	{
		// Try to find as a class (e.g. "Actor", "Pawn", etc.)
		UClass* FoundClass = FindObject<UClass>(nullptr, *TypeName);
		if (FoundClass)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = FoundClass;
		}
		else
		{
			// Try as struct
			UScriptStruct* FoundStruct = FindObject<UScriptStruct>(nullptr, *TypeName);
			if (FoundStruct)
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				PinType.PinSubCategoryObject = FoundStruct;
			}
			else
			{
				UE_LOG(LogDCEditor, Warning, TEXT("Unknown type '%s', defaulting to float"), *TypeName);
				PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
			}
		}
	}

	return PinType;
}

// ============================================================
// VARIABLE MANAGEMENT
// ============================================================

bool UDCEditorLibrary::AddVariableToBlueprint(const FString& BlueprintPath, const FString& VarName, const FString& VarType, const FString& DefaultValue)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	FName VariableName(*VarName);

	// Check if variable already exists
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName == VariableName)
		{
			UE_LOG(LogDCEditor, Warning, TEXT("Variable '%s' already exists in %s"), *VarName, *BlueprintPath);
			return true;
		}
	}

	FEdGraphPinType PinType = ResolvePinType(VarType);

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, VariableName, PinType);

	if (bSuccess && !DefaultValue.IsEmpty())
	{
		SetVariableDefault(BlueprintPath, VarName, DefaultValue);
	}

	if (bSuccess)
	{
		UE_LOG(LogDCEditor, Log, TEXT("Added variable '%s' (%s) to %s"), *VarName, *VarType, *BlueprintPath);
	}
	else
	{
		UE_LOG(LogDCEditor, Error, TEXT("Failed to add variable '%s' to %s"), *VarName, *BlueprintPath);
	}

	return bSuccess;
}

bool UDCEditorLibrary::RemoveVariableFromBlueprint(const FString& BlueprintPath, const FString& VarName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	FName VariableName(*VarName);
	FBlueprintEditorUtils::RemoveMemberVariable(BP, VariableName);
	UE_LOG(LogDCEditor, Log, TEXT("Removed variable '%s' from %s"), *VarName, *BlueprintPath);
	return true;
}

bool UDCEditorLibrary::SetVariableDefault(const FString& BlueprintPath, const FString& VarName, const FString& Value)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	FName VariableName(*VarName);

	// Set default value on the CDO
	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
	if (!CDO)
	{
		// Compile first to generate the class
		FKismetEditorUtilities::CompileBlueprint(BP);
		CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
	}

	if (CDO)
	{
		FProperty* Prop = BP->GeneratedClass->FindPropertyByName(VariableName);
		if (Prop)
		{
			DCE_ImportText(Prop, *Value, Prop->ContainerPtrToValuePtr<void>(CDO), 0, CDO);
			CDO->MarkPackageDirty();
			UE_LOG(LogDCEditor, Log, TEXT("Set default for '%s' = '%s'"), *VarName, *Value);
			return true;
		}
	}

	// Fallback: set via the Blueprint's variable description
	for (FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarName == VariableName)
		{
			Var.DefaultValue = Value;
			UE_LOG(LogDCEditor, Log, TEXT("Set default value for '%s' = '%s' (via description)"), *VarName, *Value);
			return true;
		}
	}

	UE_LOG(LogDCEditor, Error, TEXT("Could not find variable '%s' to set default"), *VarName);
	return false;
}

// ============================================================
// SCS COMPONENT MANAGEMENT
// ============================================================

FString UDCEditorLibrary::AddComponentToBlueprint(const FString& BlueprintPath, const FString& ComponentClassName, const FString& ComponentName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP || !BP->SimpleConstructionScript) return TEXT("");

	// Find the component class
	UClass* CompClass = FindObject<UClass>(nullptr, *ComponentClassName);
	if (!CompClass)
	{
		// Try common prefixes
		CompClass = FindObject<UClass>(nullptr, *(TEXT("U") + ComponentClassName));
	}
	if (!CompClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Component class '%s' not found"), *ComponentClassName);
		return TEXT("");
	}

	// Create the SCS node
	FName DesiredName = ComponentName.IsEmpty() ? CompClass->GetFName() : FName(*ComponentName);
	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, DesiredName);
	if (!NewNode)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Failed to create SCS node for '%s'"), *ComponentClassName);
		return TEXT("");
	}

	// Add to root or default scene root
	const TArray<USCS_Node*>& RootNodes = BP->SimpleConstructionScript->GetRootNodes();
	if (RootNodes.Num() > 0 && CompClass->IsChildOf(USceneComponent::StaticClass()))
	{
		RootNodes[0]->AddChildNode(NewNode);
	}
	else
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString ResultName = NewNode->GetVariableName().ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added component '%s' (%s) to %s"), *ResultName, *ComponentClassName, *BlueprintPath);
	return ResultName;
}

FString UDCEditorLibrary::ListBlueprintVariables(const FString& BlueprintPath)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> VarArray;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
		}
		VarArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(VarArray, Writer);
	return Output;
}

// ============================================================
// SCS COMPONENT CONFIGURATION
// ============================================================

bool UDCEditorLibrary::SetComponentProperty(const FString& BlueprintPath, const FString& ComponentName, const FString& PropertyName, const FString& Value)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP || !BP->SimpleConstructionScript) return false;

	FName CompName(*ComponentName);
	const TArray<USCS_Node*>& AllNodes = BP->SimpleConstructionScript->GetAllNodes();

	for (USCS_Node* Node : AllNodes)
	{
		if (Node && Node->GetVariableName() == CompName)
		{
			UActorComponent* Template = Node->ComponentTemplate;
			if (Template)
			{
				FProperty* Prop = Template->GetClass()->FindPropertyByName(FName(*PropertyName));
				if (Prop)
				{
					// Special handling for object properties (e.g., StaticMesh, Material)
					FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop);
					if (ObjProp)
					{
						UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Value);
						if (LoadedObj)
						{
							ObjProp->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(Template), LoadedObj);
							Template->MarkPackageDirty();
							UE_LOG(LogDCEditor, Log, TEXT("Set object %s.%s = %s"), *ComponentName, *PropertyName, *LoadedObj->GetName());
							return true;
						}
						else
						{
							UE_LOG(LogDCEditor, Error, TEXT("Could not load object: %s"), *Value);
							return false;
						}
					}

					// For non-object properties, use ImportText
					DCE_ImportText(Prop, *Value, Prop->ContainerPtrToValuePtr<void>(Template), 0, Template);
					Template->MarkPackageDirty();
					UE_LOG(LogDCEditor, Log, TEXT("Set %s.%s = %s"), *ComponentName, *PropertyName, *Value);
					return true;
				}
				else
				{
					UE_LOG(LogDCEditor, Error, TEXT("Property '%s' not found on component '%s'"), *PropertyName, *ComponentName);
				}
			}
		}
	}

	UE_LOG(LogDCEditor, Error, TEXT("Component '%s' not found in %s"), *ComponentName, *BlueprintPath);
	return false;
}

FString UDCEditorLibrary::ListBlueprintComponents(const FString& BlueprintPath)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP || !BP->SimpleConstructionScript) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> CompArray;
	const TArray<USCS_Node*>& AllNodes = BP->SimpleConstructionScript->GetAllNodes();

	for (const USCS_Node* Node : AllNodes)
	{
		if (Node && Node->ComponentTemplate)
		{
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Node->ComponentClass->GetName());
			CompObj->SetStringField(TEXT("internal_name"), Node->ComponentTemplate->GetName());

			// List editable properties
			TArray<TSharedPtr<FJsonValue>> PropsArr;
			for (TFieldIterator<FProperty> PropIt(Node->ComponentTemplate->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (Prop->HasAnyPropertyFlags(CPF_Edit))
				{
					FString PropValue;
					Prop->ExportText_InContainer(0, PropValue, Node->ComponentTemplate, Node->ComponentTemplate, nullptr, 0);
					TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
					PropObj->SetStringField(TEXT("name"), Prop->GetName());
					PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
					PropObj->SetStringField(TEXT("value"), PropValue);
					PropsArr.Add(MakeShared<FJsonValueObject>(PropObj));
				}
			}
			CompObj->SetArrayField(TEXT("properties"), PropsArr);
			CompArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(CompArray, Writer);
	return Output;
}

bool UDCEditorLibrary::RenameComponent(const FString& BlueprintPath, const FString& OldName, const FString& NewName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP || !BP->SimpleConstructionScript) return false;

	const TArray<USCS_Node*>& AllNodes = BP->SimpleConstructionScript->GetAllNodes();
	for (USCS_Node* Node : AllNodes)
	{
		if (Node && Node->GetVariableName() == FName(*OldName))
		{
			FBlueprintEditorUtils::RenameComponentMemberVariable(BP, Node, FName(*NewName));
			UE_LOG(LogDCEditor, Log, TEXT("Renamed component '%s' to '%s'"), *OldName, *NewName);
			return true;
		}
	}
	return false;
}

// ============================================================
// WIDGET BLUEPRINT EDITING
// ============================================================

FString UDCEditorLibrary::AddWidgetToWidgetBlueprint(const FString& WidgetBlueprintPath, const FString& WidgetClassName, const FString& ParentWidgetName)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(WidgetBlueprintPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
	if (!WBP)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Failed to load Widget Blueprint: %s"), *WidgetBlueprintPath);
		return TEXT("");
	}

	UWidgetTree* Tree = WBP->WidgetTree;
	if (!Tree)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Widget Blueprint has no widget tree"));
		return TEXT("");
	}

	// Find the widget class
	UClass* WidgetClass = nullptr;
	FString Lower = WidgetClassName.ToLower();

	// Map common widget names to classes
	if (Lower == TEXT("canvaspanel")) WidgetClass = UCanvasPanel::StaticClass();
	else if (Lower == TEXT("progressbar")) WidgetClass = UProgressBar::StaticClass();
	else if (Lower == TEXT("textblock") || Lower == TEXT("text")) WidgetClass = UTextBlock::StaticClass();
	else if (Lower == TEXT("verticalbox") || Lower == TEXT("vbox")) WidgetClass = UVerticalBox::StaticClass();
	else if (Lower == TEXT("horizontalbox") || Lower == TEXT("hbox")) WidgetClass = UHorizontalBox::StaticClass();
	else if (Lower == TEXT("image")) WidgetClass = UImage::StaticClass();
	else if (Lower == TEXT("border")) WidgetClass = UBorder::StaticClass();
	else if (Lower == TEXT("overlay")) WidgetClass = UOverlay::StaticClass();
	else if (Lower == TEXT("sizebox")) WidgetClass = USizeBox::StaticClass();
	else if (Lower == TEXT("spacer")) WidgetClass = USpacer::StaticClass();
	else if (Lower == TEXT("button")) WidgetClass = UButton::StaticClass();
	else
	{
		WidgetClass = FindObject<UClass>(nullptr, *WidgetClassName);
	}

	if (!WidgetClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Unknown widget class: %s"), *WidgetClassName);
		return TEXT("");
	}

	// Find parent widget
	UWidget* ParentWidget = nullptr;
	if (!ParentWidgetName.IsEmpty())
	{
		Tree->ForEachWidget([&](UWidget* Widget) {
			if (Widget->GetName() == ParentWidgetName || Widget->GetDisplayLabel() == ParentWidgetName)
			{
				ParentWidget = Widget;
			}
		});
	}

	// Create the widget
	WBP->Modify();
	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WidgetClass);
	if (!NewWidget)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Failed to construct widget of class %s"), *WidgetClassName);
		return TEXT("");
	}

	// Add to tree
	if (!ParentWidget && !Tree->RootWidget)
	{
		Tree->RootWidget = NewWidget;
		UE_LOG(LogDCEditor, Log, TEXT("Set '%s' as root widget"), *NewWidget->GetName());
	}
	else
	{
		UPanelWidget* Panel = ParentWidget ? Cast<UPanelWidget>(ParentWidget) : Cast<UPanelWidget>(Tree->RootWidget);
		if (Panel)
		{
			Panel->AddChild(NewWidget);
			UE_LOG(LogDCEditor, Log, TEXT("Added '%s' as child of '%s'"), *NewWidget->GetName(), *Panel->GetName());
		}
		else
		{
			UE_LOG(LogDCEditor, Error, TEXT("Parent is not a panel widget, cannot add child"));
			return TEXT("");
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	return NewWidget->GetName();
}

bool UDCEditorLibrary::SetWidgetProperty(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& PropertyName, const FString& Value)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(WidgetBlueprintPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
	if (!WBP || !WBP->WidgetTree) return false;

	UWidget* TargetWidget = nullptr;
	WBP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
		if (Widget->GetName() == WidgetName)
		{
			TargetWidget = Widget;
		}
	});

	if (!TargetWidget)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Widget '%s' not found"), *WidgetName);
		return false;
	}

	FProperty* Prop = TargetWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Property '%s' not found on widget '%s'"), *PropertyName, *WidgetName);
		return false;
	}

	WBP->Modify();
	DCE_ImportText(Prop, *Value, Prop->ContainerPtrToValuePtr<void>(TargetWidget), 0, TargetWidget);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	UE_LOG(LogDCEditor, Log, TEXT("Set %s.%s = %s"), *WidgetName, *PropertyName, *Value);
	return true;
}

FString UDCEditorLibrary::ListWidgets(const FString& WidgetBlueprintPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(WidgetBlueprintPath);
	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
	if (!WBP || !WBP->WidgetTree) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> WidgetArray;

	WBP->WidgetTree->ForEachWidget([&](UWidget* Widget) {
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Widget->GetName());
		Obj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		Obj->SetStringField(TEXT("display_label"), Widget->GetDisplayLabel());
		Obj->SetBoolField(TEXT("is_root"), Widget == WBP->WidgetTree->RootWidget);

		UPanelWidget* Parent = Widget->GetParent();
		Obj->SetStringField(TEXT("parent"), Parent ? Parent->GetName() : TEXT("none"));
		WidgetArray.Add(MakeShared<FJsonValueObject>(Obj));
	});

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(WidgetArray, Writer);
	return Output;
}

// ============================================================
// BLUEPRINT GRAPH / NODE CREATION
// ============================================================

bool UDCEditorLibrary::RemoveNodeByGuid(const FString& BlueprintPath, const FString& NodeGuid)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	FGuid TargetGuid;
	FGuid::Parse(NodeGuid, TargetGuid);

	// Search ALL graph types: EventGraph, FunctionGraphs, MacroGraphs
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);
	AllGraphs.Append(BP->MacroGraphs);

	// Helper lambda to search and remove from a graph
	auto TryRemoveFromGraph = [&](UEdGraph* Graph) -> bool
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeGuid == TargetGuid)
			{
				Node->BreakAllNodeLinks();
				Graph->RemoveNode(Node);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
				UE_LOG(LogDCEditor, Log, TEXT("Removed node %s from %s/%s"),
					*NodeGuid, *BlueprintPath, *Graph->GetName());
				return true;
			}
		}
		return false;
	};

	for (UEdGraph* Graph : AllGraphs)
	{
		if (TryRemoveFromGraph(Graph)) return true;

		// Also search composite subgraphs within this graph
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
			if (Composite && Composite->BoundGraph)
			{
				if (TryRemoveFromGraph(Composite->BoundGraph)) return true;
			}
		}
	}

	UE_LOG(LogDCEditor, Error, TEXT("Node %s not found in any graph of %s"), *NodeGuid, *BlueprintPath);
	return false;
}

bool UDCEditorLibrary::AddCustomEvent(const FString& BlueprintPath, const FString& EventName, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph)
	{
		UE_LOG(LogDCEditor, Error, TEXT("No event graph found in %s"), *BlueprintPath);
		return false;
	}

	UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
	EventNode->CustomFunctionName = FName(*EventName);
	EventNode->bIsEditable = true;

	EventGraph->AddNode(EventNode, true, false);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	EventNode->NodePosX = NodeX;
	EventNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UE_LOG(LogDCEditor, Log, TEXT("Added custom event '%s' at (%d,%d)"), *EventName, NodeX, NodeY);
	return true;
}

FString UDCEditorLibrary::AddFunctionCallNode(const FString& BlueprintPath, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	// Find the function owner class - try multiple lookup strategies
	UClass* OwnerClass = nullptr;
	TArray<FString> Candidates = {
		FunctionOwnerClass,
		TEXT("U") + FunctionOwnerClass,
		TEXT("A") + FunctionOwnerClass,
	};

	for (const FString& Name : Candidates)
	{
		OwnerClass = FindObject<UClass>(nullptr, *Name);
		if (OwnerClass) break;
	}

	if (!OwnerClass)
	{
		// Try loading from common module paths
		TArray<FString> Modules = { TEXT("Engine"), TEXT("UMG"), TEXT("Kismet"), TEXT("AIModule"), TEXT("GameplayTasks") };
		for (const FString& Module : Modules)
		{
			for (const FString& Name : Candidates)
			{
				FString ClassPath = FString::Printf(TEXT("/Script/%s.%s"), *Module, *Name);
				OwnerClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (OwnerClass) break;
			}
			if (OwnerClass) break;
		}
	}

	if (!OwnerClass)
	{
		// Nuclear option: search ALL loaded classes for a matching function name
		UFunction* FoundFunction = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UFunction* Func = It->FindFunctionByName(FName(*FunctionName));
			if (Func)
			{
				OwnerClass = *It;
				UE_LOG(LogDCEditor, Log, TEXT("Found function '%s' on class '%s' via exhaustive search"), *FunctionName, *OwnerClass->GetName());
				break;
			}
		}
	}

	if (!OwnerClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Class '%s' not found and function '%s' not found on any loaded class"), *FunctionOwnerClass, *FunctionName);
		return TEXT("");
	}
	UE_LOG(LogDCEditor, Log, TEXT("Found class: %s"), *OwnerClass->GetName());

	UFunction* Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
	if (!Function)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Function '%s' not found on class '%s'"), *FunctionName, *FunctionOwnerClass);
		return TEXT("");
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(EventGraph);
	CallNode->SetFromFunction(Function);

	EventGraph->AddNode(CallNode, true, false);
	CallNode->CreateNewGuid();
	CallNode->PostPlacedNewNode();
	CallNode->AllocateDefaultPins();

	CallNode->NodePosX = NodeX;
	CallNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString NodeGuid = CallNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added function call '%s::%s', GUID: %s"), *FunctionOwnerClass, *FunctionName, *NodeGuid);
	return NodeGuid;
}

FString UDCEditorLibrary::AddFunctionCallNodeToComposite(const FString& BlueprintPath, const FString& CompositeNodeGuid, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	// Find the composite node and get its BoundGraph
	FGuid TargetGuid;
	FGuid::Parse(CompositeNodeGuid, TargetGuid);

	UEdGraph* TargetGraph = nullptr;
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);
	AllGraphs.Append(BP->MacroGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeGuid == TargetGuid)
			{
				UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
				if (Composite && Composite->BoundGraph)
					TargetGraph = Composite->BoundGraph;
				break;
			}
		}
		if (TargetGraph) break;
	}

	if (!TargetGraph)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Composite node GUID '%s' not found"), *CompositeNodeGuid);
		return TEXT("");
	}

	// Find the function owner class
	UClass* OwnerClass = nullptr;
	TArray<FString> Candidates = { FunctionOwnerClass, TEXT("U") + FunctionOwnerClass, TEXT("A") + FunctionOwnerClass };
	for (const FString& Name : Candidates)
	{
		OwnerClass = FindObject<UClass>(nullptr, *Name);
		if (OwnerClass) break;
	}

	if (!OwnerClass)
	{
		TArray<FString> Modules = { TEXT("Engine"), TEXT("UMG"), TEXT("Kismet"), TEXT("AIModule"), TEXT("GameplayTasks") };
		for (const FString& Module : Modules)
		{
			for (const FString& Name : Candidates)
			{
				FString ClassPath = FString::Printf(TEXT("/Script/%s.%s"), *Module, *Name);
				OwnerClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (OwnerClass) break;
			}
			if (OwnerClass) break;
		}
	}

	if (!OwnerClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UFunction* Func = It->FindFunctionByName(FName(*FunctionName));
			if (Func) { OwnerClass = *It; break; }
		}
	}

	if (!OwnerClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Class '%s' not found"), *FunctionOwnerClass);
		return TEXT("");
	}

	UFunction* Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
	if (!Function)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Function '%s' not found on class '%s'"), *FunctionName, *OwnerClass->GetName());
		return TEXT("");
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TargetGraph);
	CallNode->SetFromFunction(Function);
	TargetGraph->AddNode(CallNode, true, false);
	CallNode->CreateNewGuid();
	CallNode->PostPlacedNewNode();
	CallNode->AllocateDefaultPins();
	CallNode->NodePosX = NodeX;
	CallNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString NodeGuid = CallNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added function call '%s::%s' to composite, GUID: %s"), *FunctionOwnerClass, *FunctionName, *NodeGuid);
	return NodeGuid;
}

// Helper: find a class and function by name using multiple strategies
static bool FindClassAndFunction(const FString& FunctionOwnerClass, const FString& FunctionName, UClass*& OutClass, UFunction*& OutFunction)
{
	OutClass = nullptr;
	OutFunction = nullptr;

	TArray<FString> Candidates = { FunctionOwnerClass, TEXT("U") + FunctionOwnerClass, TEXT("A") + FunctionOwnerClass };
	for (const FString& Name : Candidates)
	{
		OutClass = FindObject<UClass>(nullptr, *Name);
		if (OutClass) break;
	}
	if (!OutClass)
	{
		TArray<FString> Modules = { TEXT("Engine"), TEXT("UMG"), TEXT("Kismet"), TEXT("AIModule"), TEXT("GameplayTasks") };
		for (const FString& Module : Modules)
		{
			for (const FString& Name : Candidates)
			{
				FString ClassPath = FString::Printf(TEXT("/Script/%s.%s"), *Module, *Name);
				OutClass = LoadClass<UObject>(nullptr, *ClassPath);
				if (OutClass) break;
			}
			if (OutClass) break;
		}
	}
	if (!OutClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->FindFunctionByName(FName(*FunctionName)))
			{
				OutClass = *It;
				break;
			}
		}
	}
	if (!OutClass) return false;
	OutFunction = OutClass->FindFunctionByName(FName(*FunctionName));
	return OutFunction != nullptr;
}

FString UDCEditorLibrary::AddFunctionCallNodeToFunctionGraph(const FString& BlueprintPath, const FString& FunctionGraphName, const FString& FunctionOwnerClass, const FString& FunctionName, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	// Find the named function graph
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph->GetName() == FunctionGraphName)
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Function graph '%s' not found"), *FunctionGraphName);
		return TEXT("");
	}

	UClass* OwnerClass = nullptr;
	UFunction* Function = nullptr;
	if (!FindClassAndFunction(FunctionOwnerClass, FunctionName, OwnerClass, Function))
	{
		UE_LOG(LogDCEditor, Error, TEXT("Function '%s' not found"), *FunctionName);
		return TEXT("");
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TargetGraph);
	CallNode->SetFromFunction(Function);
	TargetGraph->AddNode(CallNode, true, false);
	CallNode->CreateNewGuid();
	CallNode->PostPlacedNewNode();
	CallNode->AllocateDefaultPins();
	CallNode->NodePosX = NodeX;
	CallNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FString NodeGuid = CallNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added function call '%s::%s' to graph '%s', GUID: %s"), *FunctionOwnerClass, *FunctionName, *FunctionGraphName, *NodeGuid);
	return NodeGuid;
}

bool UDCEditorLibrary::ConnectNodesInFunctionGraph(const FString& BlueprintPath, const FString& FunctionGraphName, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	// Find the named function graph
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph->GetName() == FunctionGraphName)
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Function graph '%s' not found for ConnectNodes"), *FunctionGraphName);
		return false;
	}

	FGuid SourceGuid, TargetGuid;
	FGuid::Parse(SourceNodeId, SourceGuid);
	FGuid::Parse(TargetNodeId, TargetGuid);

	UK2Node* SourceNode = nullptr;
	UK2Node* TargetNode = nullptr;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(Node);
		if (!K2) continue;
		if (K2->NodeGuid == SourceGuid || K2->GetNodeTitle(ENodeTitleType::EditableTitle).ToString() == SourceNodeId)
			SourceNode = K2;
		if (K2->NodeGuid == TargetGuid || K2->GetNodeTitle(ENodeTitleType::EditableTitle).ToString() == TargetNodeId)
			TargetNode = K2;
	}

	if (!SourceNode || !TargetNode)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not find source or target node in graph '%s'"), *FunctionGraphName);
		return false;
	}

	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
		if (Pin->PinName == FName(*SourcePinName)) { SourcePin = Pin; break; }
	for (UEdGraphPin* Pin : TargetNode->Pins)
		if (Pin->PinName == FName(*TargetPinName)) { TargetPin = Pin; break; }

	if (!SourcePin || !TargetPin)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not find pins '%s' or '%s'"), *SourcePinName, *TargetPinName);
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && SourcePin->Direction == EGPD_Output)
	{
		TArray<UEdGraphPin*> LinkedCopy = SourcePin->LinkedTo;
		for (UEdGraphPin* Linked : LinkedCopy)
			Schema->BreakSinglePinLink(SourcePin, Linked);
	}

	Schema->TryCreateConnection(SourcePin, TargetPin);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	return true;
}

FString UDCEditorLibrary::AddSpawnActorNode(const FString& BlueprintPath, const FString& ActorClassPath, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	// Load the actor class by loading the Blueprint first, then getting GeneratedClass
	// (This matches how the marketplace plugin does it)
	UClass* ActorClass = nullptr;

	// First try loading as a Blueprint asset
	UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(ActorClassPath);
	if (!LoadedObj)
	{
		FString BaseName = FPaths::GetBaseFilename(ActorClassPath);
		LoadedObj = UEditorAssetLibrary::LoadAsset(ActorClassPath + TEXT(".") + BaseName);
	}

	UBlueprint* LoadedBP = Cast<UBlueprint>(LoadedObj);
	if (LoadedBP && LoadedBP->GeneratedClass)
	{
		ActorClass = LoadedBP->GeneratedClass;
		UE_LOG(LogDCEditor, Log, TEXT("Loaded BP GeneratedClass: %s"), *ActorClass->GetPathName());
	}
	else
	{
		// Fallback to LoadClass
		ActorClass = LoadClass<AActor>(nullptr, *ActorClassPath);
	}

	if (!ActorClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not load actor class: %s"), *ActorClassPath);
		return TEXT("");
	}

	// Create the standard UK2Node_SpawnActorFromClass node
	UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(EventGraph);
	EventGraph->AddNode(SpawnNode, true, false);
	SpawnNode->CreateNewGuid();
	SpawnNode->PostPlacedNewNode();
	SpawnNode->AllocateDefaultPins();

	// Set the class pin BEFORE ReconstructNode
	UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
	if (ClassPin)
	{
		ClassPin->DefaultObject = ActorClass;
		UE_LOG(LogDCEditor, Log, TEXT("Set class pin to %s"), *ActorClass->GetName());
	}

	SpawnNode->NodePosX = NodeX;
	SpawnNode->NodePosY = NodeY;

	// DO NOT call ReconstructNode or MarkBlueprintAsStructurallyModified
	// as they clear DefaultObject. The compile step will handle reconstruction.
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	FString Guid = SpawnNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added SpawnActorFromClass node for '%s', GUID: %s"), *ActorClass->GetName(), *Guid);
	return Guid;
}

// SpawnBlueprintActor not included in DCE

bool UDCEditorLibrary::ConnectNodes(const FString& BlueprintPath, const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	// Build a flat list of all graphs to search, including composite subgraphs
	TArray<UEdGraph*> AllSearchGraphs;
	AllSearchGraphs.Append(BP->UbergraphPages);
	AllSearchGraphs.Append(BP->FunctionGraphs);
	AllSearchGraphs.Append(BP->MacroGraphs);
	for (UEdGraph* G : TArray<UEdGraph*>(AllSearchGraphs))
	{
		for (UEdGraphNode* N : G->Nodes)
		{
			UK2Node_Composite* Comp = Cast<UK2Node_Composite>(N);
			if (Comp && Comp->BoundGraph) AllSearchGraphs.AddUnique(Comp->BoundGraph);
		}
	}

	// Find source and target nodes by GUID or name
	UK2Node* SourceNode = nullptr;
	UK2Node* TargetNode = nullptr;

	FGuid SourceGuid, TargetGuid;
	FGuid::Parse(SourceNodeId, SourceGuid);
	FGuid::Parse(TargetNodeId, TargetGuid);

	for (UEdGraph* Graph : AllSearchGraphs)
	{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node* K2 = Cast<UK2Node>(Node);
		if (!K2) continue;

		if (K2->NodeGuid == SourceGuid || K2->GetNodeTitle(ENodeTitleType::EditableTitle).ToString() == SourceNodeId)
		{
			SourceNode = K2;
		}
		if (K2->NodeGuid == TargetGuid || K2->GetNodeTitle(ENodeTitleType::EditableTitle).ToString() == TargetNodeId)
		{
			TargetNode = K2;
		}
	}
	} // end for AllSearchGraphs

	if (!SourceNode || !TargetNode)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not find source or target node"));
		return false;
	}

	// Find pins
	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;

	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin->PinName == FName(*SourcePinName))
		{
			SourcePin = Pin;
			break;
		}
	}

	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin->PinName == FName(*TargetPinName))
		{
			TargetPin = Pin;
			break;
		}
	}

	if (!SourcePin || !TargetPin)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not find pins '%s' or '%s'"), *SourcePinName, *TargetPinName);
		return false;
	}

	// Break existing connections on exec output pins (can only have one connection)
	if (SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && SourcePin->Direction == EGPD_Output)
	{
		SourcePin->BreakAllPinLinks();
	}
	// Also break existing on exec input pins
	if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && TargetPin->Direction == EGPD_Input)
	{
		TargetPin->BreakAllPinLinks();
	}

	SourcePin->MakeLinkTo(TargetPin);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	UE_LOG(LogDCEditor, Log, TEXT("Connected %s.%s -> %s.%s"), *SourceNodeId, *SourcePinName, *TargetNodeId, *TargetPinName);
	return true;
}

FString UDCEditorLibrary::AddBuiltInEvent(const FString& BlueprintPath, const FString& EventFunctionName, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	// Find the function on the Blueprint's parent class
	UFunction* EventFunc = BP->ParentClass->FindFunctionByName(FName(*EventFunctionName));
	if (!EventFunc)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Event function '%s' not found on %s"), *EventFunctionName, *BP->ParentClass->GetName());
		return TEXT("");
	}

	// Check if this event already exists in the graph
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(Node);
		if (ExistingEvent && ExistingEvent->EventReference.GetMemberName() == FName(*EventFunctionName))
		{
			UE_LOG(LogDCEditor, Log, TEXT("Event '%s' already exists, returning existing GUID"), *EventFunctionName);
			return ExistingEvent->NodeGuid.ToString();
		}
	}

	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(EventGraph);
	EventNode->EventReference.SetExternalMember(FName(*EventFunctionName), BP->ParentClass);
	EventNode->bOverrideFunction = true;
	EventNode->bInternalEvent = true;

	EventGraph->AddNode(EventNode, true, false);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	EventNode->NodePosX = NodeX;
	EventNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString Guid = EventNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added built-in event '%s', GUID: %s"), *EventFunctionName, *Guid);
	return Guid;
}

FString UDCEditorLibrary::AddBranchNode(const FString& BlueprintPath, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(EventGraph);

	EventGraph->AddNode(BranchNode, true, false);
	BranchNode->CreateNewGuid();
	BranchNode->PostPlacedNewNode();
	BranchNode->AllocateDefaultPins();

	BranchNode->NodePosX = NodeX;
	BranchNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString Guid = BranchNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added Branch node, GUID: %s"), *Guid);
	return Guid;
}

FString UDCEditorLibrary::AddCreateWidgetNode(const FString& BlueprintPath, const FString& WidgetClassPath, int32 NodeX, int32 NodeY)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	// Load the widget class
	UClass* WidgetClass = nullptr;
	UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(WidgetClassPath);
	if (!LoadedObj)
	{
		FString BaseName = FPaths::GetBaseFilename(WidgetClassPath);
		LoadedObj = UEditorAssetLibrary::LoadAsset(WidgetClassPath + TEXT(".") + BaseName);
	}
	UBlueprint* WidgetBP = Cast<UBlueprint>(LoadedObj);
	if (WidgetBP && WidgetBP->GeneratedClass)
	{
		WidgetClass = WidgetBP->GeneratedClass;
	}
	if (!WidgetClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("Could not load widget class: %s"), *WidgetClassPath);
		return TEXT("");
	}

	// Use the CreateWidget node class - find it dynamically since header is private
	UClass* CreateWidgetNodeClass = FindObject<UClass>(nullptr, TEXT("K2Node_CreateWidget"));
	if (!CreateWidgetNodeClass)
	{
		UE_LOG(LogDCEditor, Error, TEXT("K2Node_CreateWidget class not found"));
		return TEXT("");
	}

	UK2Node_ConstructObjectFromClass* WidgetNode = Cast<UK2Node_ConstructObjectFromClass>(
		NewObject<UObject>(EventGraph, CreateWidgetNodeClass));

	EventGraph->AddNode(WidgetNode, true, false);
	WidgetNode->CreateNewGuid();
	WidgetNode->PostPlacedNewNode();
	WidgetNode->AllocateDefaultPins();

	// Set the class pin
	UEdGraphPin* ClassPin = WidgetNode->GetClassPin();
	if (ClassPin)
	{
		ClassPin->DefaultObject = WidgetClass;
		UE_LOG(LogDCEditor, Log, TEXT("Set widget class pin to %s"), *WidgetClass->GetName());
	}

	WidgetNode->NodePosX = NodeX;
	WidgetNode->NodePosY = NodeY;

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	FString Guid = WidgetNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added CreateWidget node for '%s', GUID: %s"), *WidgetClass->GetName(), *Guid);
	return Guid;
}

bool UDCEditorLibrary::SetNodePinDefault(const FString& BlueprintPath, const FString& NodeGuid, const FString& PinName, const FString& DefaultValue)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	FGuid TargetGuid;
	FGuid::Parse(NodeGuid, TargetGuid);

	// Build a flat list of all graphs to search, including composite subgraphs
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);
	AllGraphs.Append(BP->MacroGraphs);

	// Add composite subgraphs
	for (UEdGraph* Graph : TArray<UEdGraph*>(AllGraphs)) // copy to avoid modification during iteration
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
			if (Composite && Composite->BoundGraph)
			{
				AllGraphs.AddUnique(Composite->BoundGraph);
			}
		}
	}

	for (UEdGraph* Graph : AllGraphs)
	{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->NodeGuid == TargetGuid)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->PinName == FName(*PinName))
				{
					const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

					// Special handling for class pins
					if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
					{
						UClass* ClassObj = LoadClass<UObject>(nullptr, *DefaultValue);
						if (!ClassObj)
						{
							FString BaseName = FPaths::GetBaseFilename(DefaultValue);
							ClassObj = LoadClass<UObject>(nullptr, *(DefaultValue + TEXT(".") + BaseName + TEXT("_C")));
						}
						if (ClassObj)
						{
							Pin->DefaultObject = ClassObj;
							Pin->DefaultValue = ClassObj->GetPathName();
							Schema->TrySetDefaultObject(*Pin, ClassObj);
							FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
							UE_LOG(LogDCEditor, Log, TEXT("Set class pin %s = %s (path: %s)"), *PinName, *ClassObj->GetName(), *ClassObj->GetPathName());
							return true;
						}
						UE_LOG(LogDCEditor, Error, TEXT("Could not load class: %s"), *DefaultValue);
						return false;
					}

					Schema->TrySetDefaultValue(*Pin, DefaultValue);
					FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
					UE_LOG(LogDCEditor, Log, TEXT("Set pin default %s.%s = %s"), *NodeGuid, *PinName, *DefaultValue);
					return true;
				}
			}
			UE_LOG(LogDCEditor, Error, TEXT("Pin '%s' not found on node %s"), *PinName, *NodeGuid);
			return false;
		}
	}
	} // end for AllGraphs

	UE_LOG(LogDCEditor, Error, TEXT("Node %s not found"), *NodeGuid);
	return false;
}

FString UDCEditorLibrary::AddVariableGetNode(const FString& BlueprintPath, const FString& VariableName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(EventGraph);
	GetNode->VariableReference.SetSelfMember(FName(*VariableName));

	EventGraph->AddNode(GetNode, true, false);
	GetNode->CreateNewGuid();
	GetNode->PostPlacedNewNode();
	GetNode->AllocateDefaultPins();

	GetNode->NodePosX = 100;
	GetNode->NodePosY = FMath::RandRange(-300, 300);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString Guid = GetNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added VariableGet '%s', GUID: %s"), *VariableName, *Guid);
	return Guid;
}

FString UDCEditorLibrary::AddVariableSetNode(const FString& BlueprintPath, const FString& VariableName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("");

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return TEXT("");

	UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(EventGraph);
	SetNode->VariableReference.SetSelfMember(FName(*VariableName));

	EventGraph->AddNode(SetNode, true, false);
	SetNode->CreateNewGuid();
	SetNode->PostPlacedNewNode();
	SetNode->AllocateDefaultPins();

	SetNode->NodePosX = 400;
	SetNode->NodePosY = FMath::RandRange(-300, 300);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString Guid = SetNode->NodeGuid.ToString();
	UE_LOG(LogDCEditor, Log, TEXT("Added VariableSet '%s', GUID: %s"), *VariableName, *Guid);
	return Guid;
}

FString UDCEditorLibrary::DescribeGraph(const FString& BlueprintPath, const FString& GraphName)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("{}");

	UEdGraph* Graph = nullptr;
	if (GraphName == TEXT("EventGraph"))
	{
		Graph = FBlueprintEditorUtils::FindEventGraph(BP);
	}
	else
	{
		// Search ALL graph types
		TArray<UEdGraph*> AllGraphs;
		AllGraphs.Append(BP->FunctionGraphs);
		AllGraphs.Append(BP->MacroGraphs);
		AllGraphs.Append(BP->UbergraphPages);
		for (UEdGraph* G : AllGraphs)
		{
			if (G->GetName() == GraphName)
			{
				Graph = G;
				break;
			}
		}
	}

	if (!Graph) return TEXT("{\"error\":\"Graph not found\"}");

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetNumberField(TEXT("x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("y"), Node->NodePosY);

		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

			TArray<TSharedPtr<FJsonValue>> LinkedArray;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetStringField(TEXT("node_guid"), Linked->GetOwningNode()->NodeGuid.ToString());
				LinkObj->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
				LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
			PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("graph_name"), Graph->GetName());
	Result->SetArrayField(TEXT("nodes"), NodesArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

FString UDCEditorLibrary::DescribeCompositeGraph(const FString& BlueprintPath, const FString& CompositeNodeGuid)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("{\"error\":\"Blueprint not found\"}");

	// Search all graphs for a composite node with the matching GUID
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);
	AllGraphs.Append(BP->MacroGraphs);

	FGuid TargetGuid;
	FGuid::Parse(CompositeNodeGuid, TargetGuid);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeGuid == TargetGuid)
			{
				UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
				if (!Composite || !Composite->BoundGraph)
					return TEXT("{\"error\":\"Node found but not a composite or has no BoundGraph\"}");

				UEdGraph* SubGraph = Composite->BoundGraph;
				TArray<TSharedPtr<FJsonValue>> NodesArray;
				for (UEdGraphNode* SubNode : SubGraph->Nodes)
				{
					TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
					NodeObj->SetStringField(TEXT("guid"), SubNode->NodeGuid.ToString());
					NodeObj->SetStringField(TEXT("class"), SubNode->GetClass()->GetName());
					NodeObj->SetStringField(TEXT("title"), SubNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					NodeObj->SetNumberField(TEXT("x"), SubNode->NodePosX);
					NodeObj->SetNumberField(TEXT("y"), SubNode->NodePosY);

					TArray<TSharedPtr<FJsonValue>> PinsArray;
					for (UEdGraphPin* Pin : SubNode->Pins)
					{
						TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
						PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
						PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
						PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
						PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
						TArray<TSharedPtr<FJsonValue>> LinkedArray;
						for (UEdGraphPin* Linked : Pin->LinkedTo)
						{
							TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
							LinkObj->SetStringField(TEXT("node_guid"), Linked->GetOwningNode()->NodeGuid.ToString());
							LinkObj->SetStringField(TEXT("pin_name"), Linked->PinName.ToString());
							LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
						}
						PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
						PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
					}
					NodeObj->SetArrayField(TEXT("pins"), PinsArray);
					NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
				}

				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("graph_name"), SubGraph->GetName());
				Result->SetNumberField(TEXT("node_count"), SubGraph->Nodes.Num());
				Result->SetArrayField(TEXT("nodes"), NodesArray);

				FString Output;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
				FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
				return Output;
			}
		}
	}

	return TEXT("{\"error\":\"Composite node GUID not found\"}");
}

// ============================================================
// COMPILE & SAVE
// ============================================================

bool UDCEditorLibrary::CompileAndSaveBlueprint(const FString& BlueprintPath)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return false;

	// Save class pin DefaultObjects before compile (they get cleared)
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	TMap<FGuid, UObject*> SavedClassPinDefaults;
	if (EventGraph)
	{
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
				{
					UE_LOG(LogDCEditor, Log, TEXT("  ClassPin scan: Node=%s Pin=%s DefaultObject=%s"),
						*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Left(30),
						*Pin->PinName.ToString(),
						Pin->DefaultObject ? *Pin->DefaultObject->GetName() : TEXT("NULL"));

					if (Pin->DefaultObject != nullptr)
					{
						SavedClassPinDefaults.Add(Node->NodeGuid, Pin->DefaultObject);
					}
				}
			}
		}
		UE_LOG(LogDCEditor, Log, TEXT("Saved %d class pin defaults before compile"), SavedClassPinDefaults.Num());
	}

	FKismetEditorUtilities::CompileBlueprint(BP);

	// Restore class pin DefaultObjects after compile
	// Save the actual UEdGraphPin pointers and their defaults, since the node objects
	// survive compile but their pin DefaultObjects get cleared
	if (SavedClassPinDefaults.Num() > 0)
	{
		// Re-scan all pins and restore any null class pins that we saved
		UEdGraph* PostCompileGraph = FBlueprintEditorUtils::FindEventGraph(BP);
		if (PostCompileGraph)
		{
			for (UEdGraphNode* Node : PostCompileGraph->Nodes)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if ((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
						 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass) &&
						Pin->DefaultObject == nullptr)
					{
						// Find a saved default for any class pin
						for (auto& Pair : SavedClassPinDefaults)
						{
							Pin->DefaultObject = Pair.Value;
							UE_LOG(LogDCEditor, Log, TEXT("Restored class pin on %s.%s -> %s"),
								*Node->GetClass()->GetName(), *Pin->PinName.ToString(), *Pair.Value->GetName());
							SavedClassPinDefaults.Remove(Pair.Key);
							break;
						}
					}
				}
			}
		}
		BP->MarkPackageDirty();
	}

	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	UE_LOG(LogDCEditor, Log, TEXT("Compiled and saved: %s"), *BlueprintPath);
	return true;
}

FString UDCEditorLibrary::CheckBlueprintCompileStatus(const FString& BlueprintPath)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("Blueprint not found");

	FKismetEditorUtilities::CompileBlueprint(BP);

	if (BP->Status == BS_Error)
	{
		return TEXT("ERRORS");
	}
	else if (BP->Status == BS_UpToDateWithWarnings)
	{
		return TEXT("WARNINGS");
	}
	return TEXT("");  // empty = clean
}

void UDCEditorLibrary::AddCommentBox(const FString& BlueprintPath, const FString& Comment, const TArray<FString>& NodeGuids)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return;

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!EventGraph) return;

	// Get tag prefix from settings
	FString Prefix = TEXT("[CB]");
	if (const UDCEditorSettings* Settings = GetDefault<UDCEditorSettings>())
	{
		Prefix = Settings->CommentTagPrefix;
	}

	FString FullComment = FString::Printf(TEXT("%s %s"), *Prefix, *Comment);

	// Find the nodes by GUID to calculate bounding box
	int32 MinX = INT32_MAX, MinY = INT32_MAX, MaxX = INT32_MIN, MaxY = INT32_MIN;
	int32 FoundNodes = 0;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		FString NodeGuidStr = Node->NodeGuid.ToString();
		for (const FString& Guid : NodeGuids)
		{
			if (NodeGuidStr == Guid)
			{
				MinX = FMath::Min(MinX, Node->NodePosX - 50);
				MinY = FMath::Min(MinY, Node->NodePosY - 80);
				MaxX = FMath::Max(MaxX, Node->NodePosX + 400);
				MaxY = FMath::Max(MaxY, Node->NodePosY + 200);
				FoundNodes++;
				break;
			}
		}
	}

	if (FoundNodes == 0) return;

	// Create comment node
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(EventGraph);
	CommentNode->NodeComment = FullComment;
	CommentNode->NodePosX = MinX;
	CommentNode->NodePosY = MinY;
	CommentNode->NodeWidth = MaxX - MinX;
	CommentNode->NodeHeight = MaxY - MinY;

	// Set comment color from settings
	if (const UDCEditorSettings* Settings = GetDefault<UDCEditorSettings>())
	{
		CommentNode->CommentColor = Settings->CommentColor;
	}

	EventGraph->AddNode(CommentNode, true, false);
	CommentNode->CreateNewGuid();

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	UE_LOG(LogDCEditor, Log, TEXT("Added comment: %s"), *FullComment);
}

bool UDCEditorLibrary::StartPIE()
{
	if (GEditor)
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);
		UE_LOG(LogDCEditor, Log, TEXT("Requested Play In Editor"));
		return true;
	}
	return false;
}

// ============================================================
// OUTPUT LOG CAPTURE
// ============================================================

void UDCEditorLibrary::StartLogCapture()
{
	CapturedLogs.Empty();
	bCapturingLogs = true;
	UE_LOG(LogDCEditor, Log, TEXT("Log capture started"));
}

FString UDCEditorLibrary::StopLogCapture()
{
	bCapturingLogs = false;
	// Just return recent logs from file
	return GetRecentLogs(50);
}

FString UDCEditorLibrary::GetRecentLogs(int32 NumLines)
{
	// Read the most recent log file
	FString LogDir = FPaths::ProjectLogDir();
	FString LogFilePath;

	// Find the most recent log file
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *(LogDir / TEXT("*.log")), true, false);

	if (LogFiles.Num() == 0) return TEXT("[]");

	LogFiles.Sort();
	LogFilePath = LogDir / LogFiles.Last();

	FString LogContent;
	if (!FFileHelper::LoadFileToString(LogContent, *LogFilePath))
	{
		return TEXT("[\"Failed to read log file\"]");
	}

	TArray<FString> Lines;
	LogContent.ParseIntoArrayLines(Lines);

	// Get last N lines
	int32 StartIndex = FMath::Max(0, Lines.Num() - NumLines);
	TArray<TSharedPtr<FJsonValue>> LogArray;
	for (int32 i = StartIndex; i < Lines.Num(); i++)
	{
		LogArray.Add(MakeShared<FJsonValueString>(Lines[i]));
	}

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(LogArray, Writer);
	return Output;
}

// ============================================================
// ASSET INTROSPECTION
// ============================================================

FString UDCEditorLibrary::DescribeBlueprint(const FString& BlueprintPath)
{
	UBlueprint* BP = LoadBlueprintAsset(BlueprintPath);
	if (!BP) return TEXT("{\"error\":\"Blueprint not found\"}");

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), BP->GetName());
	Result->SetStringField(TEXT("path"), BlueprintPath);
	Result->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));

	// Variables (name + type + default + UE-side Category so the editor can
	// mirror the exact grouping/ordering the details panel uses).
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("default"), Var.DefaultValue);
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	// Components
	if (BP->SimpleConstructionScript)
	{
		TArray<TSharedPtr<FJsonValue>> CompsArray;
		for (const USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
				CompObj->SetStringField(TEXT("class"), Node->ComponentClass->GetName());
				CompsArray.Add(MakeShared<FJsonValueObject>(CompObj));
			}
		}
		Result->SetArrayField(TEXT("components"), CompsArray);
	}

	// Graphs
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : BP->UbergraphPages)
	{
		TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
		GObj->SetStringField(TEXT("name"), Graph->GetName());
		GObj->SetStringField(TEXT("type"), TEXT("EventGraph"));
		GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GObj));
	}
	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
		GObj->SetStringField(TEXT("name"), Graph->GetName());
		GObj->SetStringField(TEXT("type"), TEXT("Function"));
		GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GObj));
	}
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

// ========== GENERIC PROPERTY TEXT I/O ==========

#include "EngineUtils.h"

AActor* UDCEditorLibrary::FindActorByLabel(const FString& Label)
{
	if (!GEditor) return nullptr;
	// Collect candidate worlds: editor world + all PIE worlds
	TArray<UWorld*> Worlds;
	if (UWorld* EW = GEditor->GetEditorWorldContext().World()) Worlds.Add(EW);
	for (const FWorldContext& WC : GEditor->GetWorldContexts())
	{
		if (WC.WorldType == EWorldType::PIE && WC.World()) Worlds.AddUnique(WC.World());
	}
	// 1st pass: exact path match (unique); 2nd pass: label/name match (label may collide).
	for (UWorld* W : Worlds)
	{
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!A) continue;
			if (A->GetPathName().Equals(Label, ESearchCase::IgnoreCase)) return A;
		}
	}
	for (UWorld* W : Worlds)
	{
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!A) continue;
			if (A->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)) return A;
			if (A->GetName().Equals(Label, ESearchCase::IgnoreCase)) return A;
		}
	}
	return nullptr;
}

static bool PropertyNameMatches(FProperty* Prop, const FString& Query)
{
	if (!Prop) return false;
	if (Prop->GetName().Equals(Query, ESearchCase::IgnoreCase)) return true;
	const FString Disp = Prop->GetDisplayNameText().ToString();
	if (Disp.Equals(Query, ESearchCase::IgnoreCase)) return true;
	// Loose match: strip whitespace from both
	auto Strip = [](const FString& S){ FString Out; for (TCHAR C : S) if (!FChar::IsWhitespace(C)) Out.AppendChar(C); return Out; };
	if (Strip(Prop->GetName()).Equals(Strip(Query), ESearchCase::IgnoreCase)) return true;
	if (Strip(Disp).Equals(Strip(Query), ESearchCase::IgnoreCase)) return true;
	return false;
}

bool UDCEditorLibrary::ResolvePropertyOnActor(AActor* Actor, const FString& PropertyName, UObject*& OutOwner, FProperty*& OutProp)
{
	if (!Actor) return false;
	// Check actor itself
	for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
	{
		if (PropertyNameMatches(*It, PropertyName))
		{
			OutOwner = Actor; OutProp = *It; return true;
		}
	}
	// Check each component
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* C : Components)
	{
		if (!C) continue;
		for (TFieldIterator<FProperty> It(C->GetClass()); It; ++It)
		{
			if (PropertyNameMatches(*It, PropertyName))
			{
				OutOwner = C; OutProp = *It; return true;
			}
		}
	}
	return false;
}

FString UDCEditorLibrary::GetActorPropertyAsText(const FString& ActorLabel, const FString& PropertyName)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor) { R->SetBoolField(TEXT("ok"), false); R->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel)); goto Emit; }
	{
		UObject* Owner = nullptr; FProperty* Prop = nullptr;
		if (!ResolvePropertyOnActor(Actor, PropertyName, Owner, Prop))
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), FString::Printf(TEXT("Property not found on actor or components: %s"), *PropertyName));
			goto Emit;
		}
		void* Data = Prop->ContainerPtrToValuePtr<void>(Owner);
		FString Out;
		// PPF_ExternalEditor tells UE to export every field including defaults (1:1 with the struct)
		Prop->ExportText_Direct(Out, Data, Data, Owner, PPF_ExternalEditor);
		R->SetBoolField(TEXT("ok"), true);
		R->SetStringField(TEXT("text"), Out);
		R->SetStringField(TEXT("resolved_property"), Prop->GetName());
		R->SetStringField(TEXT("owner_class"), Owner->GetClass()->GetName());
	}
Emit:
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Output;
}

FString UDCEditorLibrary::SetActorPropertyFromText(const FString& ActorLabel, const FString& PropertyName, const FString& Text)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor) { R->SetBoolField(TEXT("ok"), false); R->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorLabel)); goto Emit; }
	{
		UObject* Owner = nullptr; FProperty* Prop = nullptr;
		if (!ResolvePropertyOnActor(Actor, PropertyName, Owner, Prop))
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), FString::Printf(TEXT("Property not found: %s"), *PropertyName));
			goto Emit;
		}
		Owner->Modify();
		void* Data = Prop->ContainerPtrToValuePtr<void>(Owner);
		FStringOutputDevice ErrorText;
		#if DCE_USE_NEW_API
		const TCHAR* Result = Prop->ImportText_Direct(*Text, Data, Owner, PPF_ExternalEditor);
#else
		const TCHAR* Result = Prop->ImportText(*Text, Data, PPF_ExternalEditor, Owner, &ErrorText);
#endif
		if (!Result)
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), FString::Printf(TEXT("ImportText failed: %s"), *((FString)ErrorText)));
			goto Emit;
		}
		// Fire property-changed so details panel refreshes
		FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
		Owner->PostEditChangeProperty(Evt);
		Actor->MarkPackageDirty();
		R->SetBoolField(TEXT("ok"), true);
		R->SetStringField(TEXT("resolved_property"), Prop->GetName());
		R->SetStringField(TEXT("owner_class"), Owner->GetClass()->GetName());
		if (!((FString)ErrorText).IsEmpty()) R->SetStringField(TEXT("warnings"), (FString)ErrorText);
	}
Emit:
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Output;
}

#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"










FString UDCEditorLibrary::DumpEnums(const FString& NamePrefix)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> EnumsObj = MakeShared<FJsonObject>();
	int32 Count = 0;
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* E = *It;
		if (!E) continue;
		const FString N = E->GetName();
		if (!NamePrefix.IsEmpty() && !N.StartsWith(NamePrefix)) continue;
		// Skip transient/garbage
		if (E->HasAnyFlags(RF_ClassDefaultObject)) continue;
		TSharedPtr<FJsonObject> EObj = MakeShared<FJsonObject>();
		EObj->SetStringField(TEXT("path"), E->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Entries;
		const int32 Num = E->NumEnums();
		// NumEnums includes a hidden _MAX entry — skip it
		for (int32 i = 0; i < Num; ++i)
		{
			const FName EntryName = E->GetNameByIndex(i);
			const FString EntryStr = EntryName.ToString();
			if (EntryStr.EndsWith(TEXT("_MAX"))) continue;
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("name"), EntryStr);
			R->SetStringField(TEXT("display"), E->GetDisplayNameTextByIndex(i).ToString());
			R->SetNumberField(TEXT("value"), (double)E->GetValueByIndex(i));
			Entries.Add(MakeShared<FJsonValueObject>(R));
		}
		EObj->SetArrayField(TEXT("entries"), Entries);
		EnumsObj->SetObjectField(N, EObj);
		Count++;
	}
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetNumberField(TEXT("count"), Count);
	Root->SetObjectField(TEXT("enums"), EnumsObj);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

static void DescribeStructRecursive(UScriptStruct* Struct, TSharedPtr<FJsonObject>& OutRoot, TSet<UScriptStruct*>& Visited);

static TSharedPtr<FJsonObject> DescribeFProperty(FProperty* P, TSet<UScriptStruct*>& Visited, TSharedPtr<FJsonObject>& RootStructs)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), P->GetName());
	O->SetStringField(TEXT("display"), P->GetDisplayNameText().ToString());
	O->SetStringField(TEXT("cpp_type"), P->GetClass()->GetName());
	if (auto* Arr = CastField<FArrayProperty>(P)) {
		O->SetBoolField(TEXT("is_array"), true);
		O->SetObjectField(TEXT("inner"), DescribeFProperty(Arr->Inner, Visited, RootStructs));
	} else if (auto* Set = CastField<FSetProperty>(P)) {
		O->SetBoolField(TEXT("is_set"), true);
		O->SetObjectField(TEXT("inner"), DescribeFProperty(Set->ElementProp, Visited, RootStructs));
	} else if (auto* Map = CastField<FMapProperty>(P)) {
		O->SetBoolField(TEXT("is_map"), true);
		O->SetObjectField(TEXT("key"), DescribeFProperty(Map->KeyProp, Visited, RootStructs));
		O->SetObjectField(TEXT("value"), DescribeFProperty(Map->ValueProp, Visited, RootStructs));
	} else if (auto* SP = CastField<FStructProperty>(P)) {
		O->SetStringField(TEXT("struct"), SP->Struct ? SP->Struct->GetName() : FString());
		if (SP->Struct && !Visited.Contains(SP->Struct))
		{
			Visited.Add(SP->Struct);
			TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
			DescribeStructRecursive(SP->Struct, Nested, Visited);
			RootStructs->SetObjectField(SP->Struct->GetName(), Nested);
		}
	} else if (auto* EP = CastField<FEnumProperty>(P)) {
		if (EP->GetEnum()) O->SetStringField(TEXT("enum"), EP->GetEnum()->GetName());
	} else if (auto* BP = CastField<FByteProperty>(P)) {
		if (BP->Enum) O->SetStringField(TEXT("enum"), BP->Enum->GetName());
	} else if (auto* OP = CastField<FObjectPropertyBase>(P)) {
		if (OP->PropertyClass) O->SetStringField(TEXT("object_class"), OP->PropertyClass->GetName());
	}
	return O;
}

static void DescribeStructRecursive(UScriptStruct* Struct, TSharedPtr<FJsonObject>& OutRoot, TSet<UScriptStruct*>& Visited)
{
	TArray<TSharedPtr<FJsonValue>> Fields;
	TSharedPtr<FJsonObject> NestedContainer = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		Fields.Add(MakeShared<FJsonValueObject>(DescribeFProperty(*It, Visited, NestedContainer)));
	}
	OutRoot->SetStringField(TEXT("name"), Struct->GetName());
	OutRoot->SetStringField(TEXT("path"), Struct->GetPathName());
	OutRoot->SetArrayField(TEXT("fields"), Fields);
	// Merge nested structs discovered into OutRoot's siblings via a "nested" map
	OutRoot->SetObjectField(TEXT("nested"), NestedContainer);
}

FString UDCEditorLibrary::DescribeStruct(const FString& StructPath)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
	if (!Struct)
	{
		Struct = LoadObject<UScriptStruct>(nullptr, *StructPath);
	}
	if (!Struct)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), FString::Printf(TEXT("Struct not found: %s"), *StructPath));
	}
	else
	{
		TSet<UScriptStruct*> Visited;
		Visited.Add(Struct);
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		DescribeStructRecursive(Struct, Root, Visited);
		R->SetBoolField(TEXT("ok"), true);
		R->SetObjectField(TEXT("root"), Root);
	}
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Out;
}

FString UDCEditorLibrary::CallFunctionOnLiveWidget(const FString& WidgetBlueprintPath, const FString& FunctionName)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> CalledOn;
	int32 Count = 0;

	// Load blueprint to get its generated class
	UObject* BPObj = LoadObject<UObject>(nullptr, *WidgetBlueprintPath);
	UClass* TargetClass = nullptr;
	if (UBlueprint* BP = Cast<UBlueprint>(BPObj))
	{
		TargetClass = BP->GeneratedClass;
	}
	else if (UClass* C = Cast<UClass>(BPObj))
	{
		TargetClass = C;
	}
	if (!TargetClass)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not resolve class for: %s"), *WidgetBlueprintPath));
	}
	else
	{
		// Iterate live UserWidget instances
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* W = *It;
			if (!W || W->HasAnyFlags(RF_ClassDefaultObject) || !IsValid(W)) continue;
			if (!W->IsA(TargetClass)) continue;
			// Find the function
			UFunction* Fn = W->FindFunction(FName(*FunctionName));
			if (!Fn) continue;
			// Only call no-param functions (or functions where all params are optional/defaulted; we skip for safety)
			bool bHasRequiredParams = false;
			for (TFieldIterator<FProperty> PIt(Fn); PIt; ++PIt)
			{
				FProperty* P = *PIt;
				if (P->HasAnyPropertyFlags(CPF_Parm) && !P->HasAnyPropertyFlags(CPF_ReturnParm) && !P->HasAnyPropertyFlags(CPF_OutParm))
				{
					bHasRequiredParams = true; break;
				}
			}
			if (bHasRequiredParams) continue;
			// Allocate parms buffer (likely zero for no-arg) and call
			uint8* Parms = (uint8*)FMemory_Alloca(Fn->ParmsSize);
			FMemory::Memzero(Parms, Fn->ParmsSize);
			W->ProcessEvent(Fn, Parms);
			CalledOn.Add(MakeShared<FJsonValueString>(W->GetClass()->GetName()));
			Count++;
		}
		R->SetBoolField(TEXT("ok"), true);
		R->SetNumberField(TEXT("count"), Count);
		R->SetArrayField(TEXT("called_on"), CalledOn);
	}
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Out;
}

FString UDCEditorLibrary::SelectBranchOnLiveWidget(const FString& WidgetBlueprintPath, const FString& PropertyName, int32 Value, const FString& RefreshFunctionName)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	UObject* BPObj = LoadObject<UObject>(nullptr, *WidgetBlueprintPath);
	UClass* TargetClass = nullptr;
	if (UBlueprint* BP = Cast<UBlueprint>(BPObj)) TargetClass = BP->GeneratedClass;
	else if (UClass* C = Cast<UClass>(BPObj)) TargetClass = C;
	if (!TargetClass)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not resolve class for: %s"), *WidgetBlueprintPath));
	}
	else
	{
		int32 Hits = 0, VarSet = 0, Called = 0;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* W = *It;
			if (!W || W->HasAnyFlags(RF_ClassDefaultObject) || !IsValid(W)) continue;
			if (!W->IsA(TargetClass)) continue;
			++Hits;
			// Set the int property if it exists.
			if (FProperty* P = W->GetClass()->FindPropertyByName(*PropertyName))
			{
				if (FIntProperty* IP = CastField<FIntProperty>(P))
				{
					IP->SetPropertyValue_InContainer(W, Value);
					++VarSet;
				}
			}
			// Call refresh function with int arg.
			if (!RefreshFunctionName.IsEmpty())
			{
				if (UFunction* Fn = W->FindFunction(FName(*RefreshFunctionName)))
				{
					uint8* Parms = (uint8*)FMemory_Alloca(Fn->ParmsSize);
					FMemory::Memzero(Parms, Fn->ParmsSize);
					// Copy the int into the first CPF_Parm slot.
					for (TFieldIterator<FProperty> PIt(Fn); PIt; ++PIt)
					{
						FProperty* Prm = *PIt;
						if (Prm->HasAnyPropertyFlags(CPF_Parm) && !Prm->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
						{
							if (FIntProperty* IP2 = CastField<FIntProperty>(Prm))
							{
								IP2->SetPropertyValue_InContainer(Parms, Value);
							}
							break;
						}
					}
					W->ProcessEvent(Fn, Parms);
					++Called;
				}
			}
		}
		R->SetBoolField(TEXT("ok"), true);
		R->SetNumberField(TEXT("hits"), Hits);
		R->SetNumberField(TEXT("var_set"), VarSet);
		R->SetNumberField(TEXT("called"), Called);
	}
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Out;
}

FString UDCEditorLibrary::GetLiveWidgetIntProperty(const FString& WidgetBlueprintPath, const FString& PropertyName)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	UObject* BPObj = LoadObject<UObject>(nullptr, *WidgetBlueprintPath);
	UClass* TargetClass = nullptr;
	if (UBlueprint* BP = Cast<UBlueprint>(BPObj)) TargetClass = BP->GeneratedClass;
	else if (UClass* C = Cast<UClass>(BPObj)) TargetClass = C;
	bool bFound = false; int32 Value = 0;
	if (TargetClass)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* W = *It;
			if (!W || W->HasAnyFlags(RF_ClassDefaultObject) || !IsValid(W)) continue;
			if (!W->IsA(TargetClass)) continue;
			if (FProperty* P = W->GetClass()->FindPropertyByName(*PropertyName))
			{
				if (FIntProperty* IP = CastField<FIntProperty>(P))
				{
					Value = IP->GetPropertyValue_InContainer(W);
					bFound = true;
					break;
				}
			}
		}
	}
	R->SetBoolField(TEXT("ok"), bFound);
	if (bFound) R->SetNumberField(TEXT("idx"), Value);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Out;
}

FString UDCEditorLibrary::PlayEditorSound(const FString& SoundAssetPath)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	// Empty path = stop any currently-playing preview (PlayPreviewSound(nullptr)
	// is UE's built-in way to halt the editor preview).
	if (SoundAssetPath.IsEmpty())
	{
		if (GEditor)
		{
			// PlayPreviewSound(nullptr) actually RESTARTS the prior sound (UE
			// source: ResetPreviewAudioComponent stops + leaves Sound, then Play()
			// runs again). Stop the preview component directly instead.
			if (UAudioComponent* AC = GEditor->GetPreviewAudioComponent())
			{
				AC->Stop();
			}
			R->SetBoolField(TEXT("ok"), true);
			R->SetStringField(TEXT("stopped"), TEXT("true"));
		}
		else
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), TEXT("GEditor is null"));
		}
		FString OutStop;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutStop);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return OutStop;
	}
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundAssetPath);
	if (!Sound)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), FString::Printf(TEXT("Could not load sound: %s"), *SoundAssetPath));
	}
	else if (GEditor)
	{
		GEditor->PlayPreviewSound(Sound);
		R->SetBoolField(TEXT("ok"), true);
		R->SetStringField(TEXT("played"), Sound->GetPathName());
		R->SetStringField(TEXT("class"), Sound->GetClass()->GetName());
	}
	else
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), TEXT("GEditor is null"));
	}
	FString Out2;
	TSharedRef<TJsonWriter<>> Writer2 = TJsonWriterFactory<>::Create(&Out2);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer2);
	return Out2;
}

FString UDCEditorLibrary::ExportTextureAsPNG(const FString& AssetPath, const FString& OutFilename)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *AssetPath);
	if (!Tex)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), TEXT("Texture not found: ") + AssetPath);
		FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(R.ToSharedRef(), W); return Out;
	}

	// Read source mip data (editor-only, preserves original import).
	FTextureSource& Src = Tex->Source;
	if (!Src.IsValid() || Src.GetSizeX() <= 0 || Src.GetSizeY() <= 0)
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), TEXT("Texture has no valid source data"));
		FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(R.ToSharedRef(), W); return Out;
	}

	const int32 W = Src.GetSizeX();
	const int32 H = Src.GetSizeY();
	TArray64<uint8> RawData;
	Src.GetMipData(RawData, 0);

	// Build an FColor array from the source bytes. Most editor textures are
	// BGRA8 or RGBA8; anything else falls back to a white placeholder so the
	// function never hard-fails.
	TArray<FColor> Colors;
	Colors.SetNumUninitialized(W * H);
	const ETextureSourceFormat Fmt = Src.GetFormat();
	if (Fmt == TSF_BGRA8 && RawData.Num() >= W * H * 4)
	{
		FMemory::Memcpy(Colors.GetData(), RawData.GetData(), W * H * 4);
	}
	else if (DCE_TSF_RGBA8_CHECK(Fmt) && RawData.Num() >= W * H * 4)
	{
		const uint8* S = RawData.GetData();
		for (int32 i = 0; i < W * H; ++i)
		{
			Colors[i] = FColor(S[i*4], S[i*4+1], S[i*4+2], S[i*4+3]);
		}
	}
	else if (Fmt == TSF_G8 && RawData.Num() >= W * H)
	{
		for (int32 i = 0; i < W * H; ++i)
		{
			const uint8 V = RawData[i];
			Colors[i] = FColor(V, V, V, 255);
		}
	}
	else
	{
		// Unsupported source format — fill with magenta so thumbnails are at
		// least *visible*, flagging that the format needs adding.
		for (int32 i = 0; i < W * H; ++i)
			Colors[i] = FColor(255, 0, 255, 255);
	}

	// Compress to PNG via FImageUtils — bypasses UExporter entirely, so no
	// "No png exporter found" LogExporter warning.
	TArray<uint8> PNGData;
	DCE_CompressImageArray(W, H, Colors, PNGData);

	// Write to disk.
	if (!FFileHelper::SaveArrayToFile(PNGData, *OutFilename))
	{
		R->SetBoolField(TEXT("ok"), false);
		R->SetStringField(TEXT("error"), TEXT("Failed to write PNG to ") + OutFilename);
		FString Out; TSharedRef<TJsonWriter<>> W2 = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(R.ToSharedRef(), W2); return Out;
	}

	R->SetBoolField(TEXT("ok"), true);
	R->SetStringField(TEXT("path"), OutFilename);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
	return Out;
}
