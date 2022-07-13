// Copyright 2019-2021 Rexocrates. All Rights Reserved.

#include "VertexAnimToolsetEditor.h"
#include "AnimationRuntime.h"
#include "AssetRegistryModule.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Misc/FeedbackContext.h"
#include "Textures/SlateIcon.h"

//--------------------------------------
#include "VATEditorUtils.h"

#define LOCTEXT_NAMESPACE "FVertexAnimToolsetEditorModule"


void FVertexAnimToolsetEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	// hook up level editor extension for skeletal mesh conversion

	ModuleLoadedDelegateHandle = FModuleManager::Get().OnModulesChanged().AddLambda([this](FName InModuleName, EModuleChangeReason InChangeReason)
	{
		if (InChangeReason == EModuleChangeReason::ModuleLoaded)
		{
			if (InModuleName == "SkeletalMeshEditor")
			{
				//check(false); TRIGGERED
				AddSkeletalMeshEditorToolbarExtender();
			}
		}
	});
}

void FVertexAnimToolsetEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// This is not causing the stuck on exiting Unreal??
	RemoveSkeletalMeshEditorToolbarExtender();
	FModuleManager::Get().OnModulesChanged().Remove(ModuleLoadedDelegateHandle);
}

TSharedRef<FExtender> FVertexAnimToolsetEditorModule::GetAnimationEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<IAnimationEditor> InAnimationEditor)
{
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);

	UDebugSkelMeshComponent* MeshComponent = InAnimationEditor->GetPersonaToolkit()->GetPreviewMeshComponent();
	TSharedRef<IPersonaPreviewScene> Scene = InAnimationEditor->GetPersonaToolkit()->GetPreviewScene();
	
	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateRaw(this, &FVertexAnimToolsetEditorModule::HandleAddSkeletalMeshActionExtenderToToolbar, MeshComponent)
	);

	return Extender;
	
}

void FVertexAnimToolsetEditorModule::HandleAddSkeletalMeshActionExtenderToToolbar(FToolBarBuilder & ParentToolbarBuilder, UDebugSkelMeshComponent * InMeshComponent) const
{
	ParentToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([this, InMeshComponent]()
		{
			FVATEditorUtils::DoBakeProcess(InMeshComponent);
		})),
		NAME_None,
		LOCTEXT("BakeAnim", "Bake Anim"),
		LOCTEXT("BakeAnimToTextureTooltip", "Bake animation frames to textures"),
		FSlateIcon("EditorStyle", "Persona.TogglePreviewAsset", "Persona.TogglePreviewAsset.Small")
	);
}

void FVertexAnimToolsetEditorModule::AddSkeletalMeshEditorToolbarExtender()
{
	ISkeletalMeshEditorModule& SkeletalMeshEditorModule = FModuleManager::Get().LoadModuleChecked<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	auto& ToolbarExtenders = SkeletalMeshEditorModule.GetAllSkeletalMeshEditorToolbarExtenders();

	ToolbarExtenders.Add(
		ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender::CreateRaw(this, &FVertexAnimToolsetEditorModule::GetSkeletalMeshEditorToolbarExtender));
	SkeletalMeshEditorExtenderHandle = ToolbarExtenders.Last().GetHandle();
}

void FVertexAnimToolsetEditorModule::RemoveSkeletalMeshEditorToolbarExtender() const
{
	ISkeletalMeshEditorModule* SkeletalMeshEditorModule = FModuleManager::Get().GetModulePtr<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	if (SkeletalMeshEditorModule)
	{
		typedef ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender DelegateType;
		SkeletalMeshEditorModule->GetAllSkeletalMeshEditorToolbarExtenders().RemoveAll([=](const DelegateType& In) { return In.GetHandle() == SkeletalMeshEditorExtenderHandle; });
	}
}

TSharedRef<FExtender> FVertexAnimToolsetEditorModule::GetSkeletalMeshEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, TSharedRef<ISkeletalMeshEditor> InSkeletalMeshEditor)
{
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);
	UDebugSkelMeshComponent* MeshComponent = InSkeletalMeshEditor->GetPersonaToolkit()->GetPreviewMeshComponent();
	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateRaw(this, &FVertexAnimToolsetEditorModule::HandleAddSkeletalMeshActionExtenderToToolbar, MeshComponent)
	);

	return Extender;
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVertexAnimToolsetEditorModule, VertexAnimToolsetEditor)