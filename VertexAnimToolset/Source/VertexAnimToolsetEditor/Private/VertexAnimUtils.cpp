// Copyright 2019-2021 Rexocrates. All Rights Reserved.

#include "VertexAnimUtils.h"

#include "AnimationRuntime.h"
#include "AssetRegistryModule.h"
#include "AssetViewerSettings.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "IAnimationEditor.h"
#include "IContentBrowserSingleton.h"
#include "IPersonaPreviewScene.h"
#include "MeshAttributeArray.h"
#include "MeshDescription.h"
#include "RawMesh.h"
#include "SkeletalRenderPublic.h"
#include "StaticMeshResources.h"
#include "VertexAnimProfile.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FeedbackContext.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Runtime/Engine/Private/SkeletalRenderCPUSkin.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

//--------------------------------------

#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/PlayerController.h"


#define LOCTEXT_NAMESPACE "PickAssetDialog"

float FVertexAnimUtils::EncodeFloat(const float & T, const float& Bound)
{
	// Divide to normalize, + 1 * 0.5 to account for negative Values
	return ((T / Bound) + 1.f) * 0.5f;
}

float FVertexAnimUtils::DeEncodeFloat(const float & T, const float & Bound)
{
	return ((T * 2.f) - 1.f) * Bound;
}

FVector FVertexAnimUtils::DeEncodeVec(const FVector4& T, const float& Bound)
{
	return (((FVector(T) * 2.f) - 1.f) * T.W) * Bound;
}

FVector4 FVertexAnimUtils::BitEncodeVecId(const FVector T, const float Bound, const int32 Id)
{
	const FVector N = ((T / Bound) +1.0) * 0.5;

	const uint32 RI = N.X * 1023;
	const uint32 GI = N.Y * 1023;
	const uint32 BI = N.Z * 1023;

	const uint8 R8 = static_cast<uint8>(RI);
	const uint8 G8 = static_cast<uint8>(GI);
	const uint8 B8 = static_cast<uint8>(BI);

	// Should I be doing and & on these??
	const uint8 RA = static_cast<uint8>((RI >> 8) & 0x3);
	const uint8 GA = static_cast<uint8>((GI >> 6) & 0xc);
	const uint8 BA = static_cast<uint8>((BI >> 4) & 0x30);
	const uint8 IA = static_cast<uint8>(Id) << 6;

	const uint8 A8 = ((RA | GA) | BA) | IA;

	return FVector4(R8 / 255.0, G8 / 255.0, B8 / 255.0, A8 / 255.0);
}

FVector4 FVertexAnimUtils::BitEncodeVecId_HD(const FVector T, const float Bound, const int32 Id)
{
	const FVector N = ((T / Bound) + 1.0) * 0.5;

	const uint32 RI = N.X * 0xFFFFF; 
	const uint32 GI = N.Y * 0xFFFFF;
	const uint32 BI = N.Z * 0xFFFFF;

	const uint16 R8 = static_cast<uint16>(RI);
	const uint16 G8 = static_cast<uint16>(GI);
	const uint16 B8 = static_cast<uint16>(BI);

	// Should I be doing and & on these??
	const uint16 RA = static_cast<uint16>((RI >> 16) & 0xF);
	const uint16 GA = static_cast<uint16>((GI >> 12) & 0xF0);
	const uint16 BA = static_cast<uint16>((BI >> 8) & 0xF00);
	const uint16 IA = static_cast<uint16>(Id) << 12;
	
	const uint16 A8 = ((RA | GA) | BA) | IA;

	return FVector4(
		-1.0 + ((R8 / 65535.0) * 2.0),
		-1.0 + ((G8 / 65535.0) * 2.0),
		-1.0 + ((B8 / 65535.0) * 2.0),
		-1.0 + ((A8 / 65535.0) * 2.0));
}


int32 FVertexAnimUtils::Grid2DIndex(const int32& X, const int32& Y, const int32& Width)
{
	//(LocationX * GridSize.Height) + LocationY // CellIndex
	return (Y * Width) + X;
}
// This should be Y with Width
int32 FVertexAnimUtils::Grid2D_X(const int32 & Index, const int32 & Width)
{
	// Original // X  = CellIndex / GridSize.Height
	return Index % Width;
}

// This Should be X with Width
int32 FVertexAnimUtils::Grid2D_Y(const int32 & Index, const int32 & Width)
{
	// For Y = CellIndex % GridSize.Height
	return  Index / Width;
}

// Helper function for ConvertMeshesToStaticMesh
static void AddOrDuplicateMaterial(UMaterialInterface* InMaterialInterface, const FString& InPackageName, TArray<UMaterialInterface*>& OutMaterials)
{
	if (InMaterialInterface && !InMaterialInterface->GetOuter()->IsA<UPackage>())
	{
		// Convert runtime material instances to new concrete material instances
		// Create new package
		const FString OriginalMaterialName = InMaterialInterface->GetName();
		FString MaterialPath = FPackageName::GetLongPackagePath(InPackageName) / OriginalMaterialName;
		FString MaterialName;
		const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(MaterialPath, TEXT(""), MaterialPath, MaterialName);
		UPackage* MaterialPackage = CreatePackage(*MaterialPath);

		// Duplicate the object into the new package
		UMaterialInterface* NewMaterialInterface = DuplicateObject<UMaterialInterface>(InMaterialInterface, MaterialPackage, *MaterialName);
		NewMaterialInterface->SetFlags(RF_Public | RF_Standalone);

		if (UMaterialInstanceDynamic* MaterialInstanceDynamic = Cast<UMaterialInstanceDynamic>(NewMaterialInterface))
		{
			UMaterialInstanceDynamic* OldMaterialInstanceDynamic = CastChecked<UMaterialInstanceDynamic>(InMaterialInterface);
			MaterialInstanceDynamic->K2_CopyMaterialInstanceParameters(OldMaterialInstanceDynamic);
		}

		if (NewMaterialInterface->MarkPackageDirty())
		{
			FAssetRegistryModule::AssetCreated(NewMaterialInterface);
			InMaterialInterface = NewMaterialInterface;
		}
	}

	OutMaterials.Add(InMaterialInterface);
}

// Helper function for ConvertMeshesToStaticMesh
template <typename ComponentType>
static void ProcessMaterials(ComponentType* InComponent, const FString& InPackageName, TArray<UMaterialInterface*>& OutMaterials)
{
	const int32 NumMaterials = InComponent->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; MaterialIndex++)
	{
		UMaterialInterface* MaterialInterface = InComponent->GetMaterial(MaterialIndex);
		AddOrDuplicateMaterial(MaterialInterface, InPackageName, OutMaterials);
	}
}

// Helper function for ConvertMeshesToStaticMesh
static bool IsValidSkinnedMeshComponent(const USkinnedMeshComponent* InComponent)
{
	return InComponent && InComponent->MeshObject && InComponent->IsVisible();
}

/** Helper struct for tracking validity of optional buffers */
struct FRawMeshTracker
{
	FRawMeshTracker()
		: bValidColors(false)
	{
		FMemory::Memset(bValidTexCoords, 0);
	}

	bool bValidTexCoords[MAX_MESH_TEXTURE_COORDS];
	bool bValidColors;
};


// Helper function for ConvertMeshesToStaticMesh
static void SkinnedMeshToRawMeshes(USkinnedMeshComponent* InSkinnedMeshComponent, int32 InOverallMaxLODs, const FMatrix& InComponentToWorld, const FString& InPackageName, TArray<FRawMeshTracker>& OutRawMeshTrackers, TArray<FRawMesh>& OutRawMeshes, TArray<UMaterialInterface*>& OutMaterials)
{
	const int32 BaseMaterialIndex = OutMaterials.Num();

	// Export all LODs to raw meshes
	const int32 NumLODs = InSkinnedMeshComponent->GetNumLODs();

	for (int32 OverallLODIndex = 0; OverallLODIndex < InOverallMaxLODs; OverallLODIndex++)
	{
		const int32 LODIndexRead = FMath::Min(OverallLODIndex, NumLODs - 1);

		FRawMesh& RawMesh = OutRawMeshes[OverallLODIndex];
		FRawMeshTracker& RawMeshTracker = OutRawMeshTrackers[OverallLODIndex];
		const int32 BaseVertexIndex = RawMesh.VertexPositions.Num();

		FSkeletalMeshLODInfo& SrcLODInfo = *(InSkinnedMeshComponent->SkeletalMesh->GetLODInfo(LODIndexRead));

		// Get the CPU skinned verts for this LOD
		TArray<FFinalSkinVertex> FinalVertices;
		InSkinnedMeshComponent->GetCPUSkinnedVertices(FinalVertices, LODIndexRead);

		FSkeletalMeshRenderData& SkeletalMeshRenderData = InSkinnedMeshComponent->MeshObject->GetSkeletalMeshRenderData();
		FSkeletalMeshLODRenderData& LODData = SkeletalMeshRenderData.LODRenderData[LODIndexRead];

		// Copy skinned vertex positions
		for (int32 VertIndex = 0; VertIndex < FinalVertices.Num(); ++VertIndex)
		{
			RawMesh.VertexPositions.Add(static_cast<UE::Math::TVector4<float>>(
					InComponentToWorld.TransformPosition(static_cast<FVector>(
						FinalVertices[VertIndex].Position))));
		}

		const uint32 NumTexCoords = FMath::Min(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(), static_cast<uint32>(MAX_MESH_TEXTURE_COORDS));
		const int32 NumSections = LODData.RenderSections.Num();
		FRawStaticIndexBuffer16or32Interface& IndexBuffer = *LODData.MultiSizeIndexContainer.GetIndexBuffer();

		for (int32 SectionIndex = 0; SectionIndex < NumSections; SectionIndex++)
		{
			const FSkelMeshRenderSection& SkelMeshSection = LODData.RenderSections[SectionIndex];
			if (InSkinnedMeshComponent->IsMaterialSectionShown(SkelMeshSection.MaterialIndex, LODIndexRead))
			{
				// Build 'wedge' info
				const int32 NumWedges = SkelMeshSection.NumTriangles * 3;
				for (int32 WedgeIndex = 0; WedgeIndex < NumWedges; WedgeIndex++)
				{
					const int32 VertexIndexForWedge = IndexBuffer.Get(SkelMeshSection.BaseIndex + WedgeIndex);

					RawMesh.WedgeIndices.Add(BaseVertexIndex + VertexIndexForWedge);

					const FFinalSkinVertex& SkinnedVertex = FinalVertices[VertexIndexForWedge];
					const FVector3f TangentX = static_cast<UE::Math::TVector4<float>>(InComponentToWorld.TransformVector(SkinnedVertex.TangentX.ToFVector()));
					const FVector3f TangentZ = static_cast<UE::Math::TVector4<float>>(InComponentToWorld.TransformVector(SkinnedVertex.TangentZ.ToFVector()));
					const FVector4f UnpackedTangentZ = SkinnedVertex.TangentZ.ToFVector4f();
					const FVector3f TangentY = (TangentZ ^ TangentX).GetSafeNormal() * UnpackedTangentZ.W;

					RawMesh.WedgeTangentX.Add(TangentX);
					RawMesh.WedgeTangentY.Add(TangentY);
					RawMesh.WedgeTangentZ.Add(TangentZ);

					for (uint32 TexCoordIndex = 0; TexCoordIndex < MAX_MESH_TEXTURE_COORDS; TexCoordIndex++)
					{
						if (TexCoordIndex >= NumTexCoords)
						{
							RawMesh.WedgeTexCoords[TexCoordIndex].AddDefaulted();
						}
						else
						{
							RawMesh.WedgeTexCoords[TexCoordIndex].Add(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndexForWedge, TexCoordIndex));
							RawMeshTracker.bValidTexCoords[TexCoordIndex] = true;
						}
					}

					if (LODData.StaticVertexBuffers.ColorVertexBuffer.IsInitialized())
					{
						RawMesh.WedgeColors.Add(LODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIndexForWedge));
						RawMeshTracker.bValidColors = true;
					}
					else
					{
						RawMesh.WedgeColors.Add(FColor::White);
					}
				}

				int32 MaterialIndex = SkelMeshSection.MaterialIndex;
				// use the remapping of material indices if there is a valid value
				if (SrcLODInfo.LODMaterialMap.IsValidIndex(SectionIndex) && SrcLODInfo.LODMaterialMap[SectionIndex] != INDEX_NONE)
				{
					MaterialIndex = FMath::Clamp<int32>(SrcLODInfo.LODMaterialMap[SectionIndex], 0, InSkinnedMeshComponent->SkeletalMesh->GetMaterials().Num());
				}

				// copy face info
				for (uint32 TriIndex = 0; TriIndex < SkelMeshSection.NumTriangles; TriIndex++)
				{
					RawMesh.FaceMaterialIndices.Add(BaseMaterialIndex + MaterialIndex);
					RawMesh.FaceSmoothingMasks.Add(0); // Assume this is ignored as bRecomputeNormals is false
				}
			}
		}
	}

	ProcessMaterials<USkinnedMeshComponent>(InSkinnedMeshComponent, InPackageName, OutMaterials);
}


UStaticMesh* FVertexAnimUtils::ConvertMeshesToStaticMesh(const TArray<UMeshComponent*>& InMeshComponents, const FTransform& InRootTransform, const FString& InPackageName)
{
	UStaticMesh* StaticMesh = nullptr;

	// Build a package name to use
	FString MeshName;
	FString PackageName;
	if (InPackageName.IsEmpty())
	{
		FString NewNameSuggestion = FString(TEXT("StaticMesh"));
		FString PackageNameSuggestion = FString(TEXT("/Game/Meshes/")) + NewNameSuggestion;
		FString Name;
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(PackageNameSuggestion, TEXT(""), PackageNameSuggestion, Name);

		TSharedPtr<SDlgPickAssetPath> PickAssetPathWidget =
			SNew(SDlgPickAssetPath)
			.Title(LOCTEXT("ConvertToStaticMeshPickName", "Choose New StaticMesh Location"))
			.DefaultAssetPath(FText::FromString(PackageNameSuggestion));

		if (PickAssetPathWidget->ShowModal() == EAppReturnType::Ok)
		{
			// Get the full name of where we want to create the mesh asset.
			PackageName = PickAssetPathWidget->GetFullAssetPath().ToString();
			MeshName = FPackageName::GetLongPackageAssetName(PackageName);

			// Check if the user inputed a valid asset name, if they did not, give it the generated default name
			if (MeshName.IsEmpty())
			{
				// Use the defaults that were already generated.
				PackageName = PackageNameSuggestion;
				MeshName = *Name;
			}
		}
	}
	else
	{
		PackageName = InPackageName;
		MeshName = *FPackageName::GetLongPackageAssetName(PackageName);
	}

	if (!PackageName.IsEmpty() && !MeshName.IsEmpty())
	{
		TArray<FRawMesh> RawMeshes;
		TArray<UMaterialInterface*> Materials;

		TArray<FRawMeshTracker> RawMeshTrackers;

		FMatrix WorldToRoot = InRootTransform.ToMatrixWithScale().Inverse();

		// first do a pass to determine the max LOD level we will be combining meshes into
		int32 OverallMaxLODs = 0;
		for (UMeshComponent* MeshComponent : InMeshComponents)
		{
			USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(MeshComponent);
			if (IsValidSkinnedMeshComponent(SkinnedMeshComponent))
			{
				OverallMaxLODs = FMath::Max(SkinnedMeshComponent->MeshObject->GetSkeletalMeshRenderData().LODRenderData.Num(), OverallMaxLODs);
			}
		}

		// Resize raw meshes to accommodate the number of LODs we will need
		RawMeshes.SetNum(OverallMaxLODs);
		RawMeshTrackers.SetNum(OverallMaxLODs);

		// Export all visible components
		for (UMeshComponent* MeshComponent : InMeshComponents)
		{
			FMatrix ComponentToWorld = MeshComponent->GetComponentTransform().ToMatrixWithScale() * WorldToRoot;

			USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(MeshComponent);
			if (IsValidSkinnedMeshComponent(SkinnedMeshComponent))
			{
				SkinnedMeshToRawMeshes(SkinnedMeshComponent, OverallMaxLODs, ComponentToWorld, PackageName, RawMeshTrackers, RawMeshes, Materials);
			}
		}

		uint32 MaxInUseTextureCoordinate = 0;

		// scrub invalid vert color & tex coord data
		check(RawMeshes.Num() == RawMeshTrackers.Num());
		for (int32 RawMeshIndex = 0; RawMeshIndex < RawMeshes.Num(); RawMeshIndex++)
		{
			if (!RawMeshTrackers[RawMeshIndex].bValidColors)
			{
				RawMeshes[RawMeshIndex].WedgeColors.Empty();
			}

			for (uint32 TexCoordIndex = 0; TexCoordIndex < MAX_MESH_TEXTURE_COORDS; TexCoordIndex++)
			{
				if (!RawMeshTrackers[RawMeshIndex].bValidTexCoords[TexCoordIndex])
				{
					RawMeshes[RawMeshIndex].WedgeTexCoords[TexCoordIndex].Empty();
				}
				else
				{
					// Store first texture coordinate index not in use
					MaxInUseTextureCoordinate = FMath::Max(MaxInUseTextureCoordinate, TexCoordIndex);
				}
			}
		}

		// Check if we got some valid data.
		bool bValidData = false;
		for (FRawMesh& RawMesh : RawMeshes)
		{
			if (RawMesh.IsValidOrFixable())
			{
				bValidData = true;
				break;
			}
		}

		if (bValidData)
		{
			// Then find/create it.
			UPackage* Package = CreatePackage(*PackageName);
			check(Package);

			// Create StaticMesh object
			StaticMesh = NewObject<UStaticMesh>(Package, *MeshName, RF_Public | RF_Standalone);
			StaticMesh->InitResources();

			StaticMesh->SetLightingGuid(FGuid::NewGuid());

			// Determine which texture coordinate map should be used for storing/generating the lightmap UVs
			const uint32 LightMapIndex = FMath::Min(MaxInUseTextureCoordinate + 1, static_cast<uint32>(MAX_MESH_TEXTURE_COORDS) - 1);

			// Add source to new StaticMesh
			for (FRawMesh& RawMesh : RawMeshes)
			{
				if (RawMesh.IsValidOrFixable())
				{
					FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();
					SrcModel.BuildSettings.bRecomputeNormals = false;
					SrcModel.BuildSettings.bRecomputeTangents = false;
					SrcModel.BuildSettings.bRemoveDegenerates = true;
					SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
					SrcModel.BuildSettings.bUseFullPrecisionUVs = false;
					SrcModel.BuildSettings.bGenerateLightmapUVs = true;
					SrcModel.BuildSettings.SrcLightmapIndex = 0;
					SrcModel.BuildSettings.DstLightmapIndex = LightMapIndex;
					SrcModel.SaveRawMesh(RawMesh);
				}
			}

			// Copy materials to new mesh 
			for (UMaterialInterface* Material : Materials)
			{
				StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
			}

			//Set the Imported version before calling the build
			StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;

			// Set light map coordinate index to match DstLightmapIndex
			StaticMesh->SetLightMapCoordinateIndex(LightMapIndex);

			// setup section info map
			for (int32 RawMeshLODIndex = 0; RawMeshLODIndex < RawMeshes.Num(); RawMeshLODIndex++)
			{
				const FRawMesh& RawMesh = RawMeshes[RawMeshLODIndex];
				TArray<int32> UniqueMaterialIndices;
				for (int32 MaterialIndex : RawMesh.FaceMaterialIndices)
				{
					UniqueMaterialIndices.AddUnique(MaterialIndex);
				}

				int32 SectionIndex = 0;
				for (int32 UniqueMaterialIndex : UniqueMaterialIndices)
				{
					StaticMesh->GetSectionInfoMap().Set(RawMeshLODIndex, SectionIndex, FMeshSectionInfo(UniqueMaterialIndex));
					SectionIndex++;
				}
			}
			StaticMesh->GetOriginalSectionInfoMap().CopyFrom(StaticMesh->GetSectionInfoMap());

			// Build mesh from source
			StaticMesh->Build(false);
			StaticMesh->PostEditChange();

			if (!StaticMesh->MarkPackageDirty())
			{
				UE_LOG(LogTemp, Warning, TEXT("Could not set the SM as dirty"));
			}

			// Notify asset registry of new asset
			FAssetRegistryModule::AssetCreated(StaticMesh);

			// Display notification so users can quickly access the mesh
			if (GIsEditor)
			{
				FNotificationInfo Info(FText::Format(LOCTEXT("SkeletalMeshConverted", "Successfully Converted Mesh"), FText::FromString(StaticMesh->GetName())));
				Info.ExpireDuration = 8.0f;
				Info.bUseLargeFont = false;
				Info.Hyperlink = FSimpleDelegate::CreateLambda([=]() { GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(TArray<UObject*>({ StaticMesh })); });
				Info.HyperlinkText = FText::Format(LOCTEXT("OpenNewAnimationHyperlink", "Open {0}"), FText::FromString(StaticMesh->GetName()));
				TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
				if (Notification.IsValid())
				{
					Notification->SetCompletionState(SNotificationItem::CS_Success);
				}
			}
		}
	}

	return StaticMesh;
}



void FVertexAnimUtils::VATUVsToStaticMeshLODs(UStaticMesh* StaticMesh, const int32 UVChannel, const TArray<TArray<FVector2D>>& UVs)
{
	for (int32 LOD = 0; LOD < StaticMesh->GetNumLODs(); LOD++)
	{
		const uint32 PaintingMeshLODIndex = LOD;

		if (StaticMesh->IsSourceModelValid(PaintingMeshLODIndex))
		{
			if (StaticMesh->GetSourceModel(PaintingMeshLODIndex).IsRawMeshEmpty() == false)
			{
				const uint32 NumUVChannels = StaticMesh->GetNumUVChannels(PaintingMeshLODIndex);
				// Extract the raw mesh.
				FRawMesh Mesh;
				StaticMesh->GetSourceModel(PaintingMeshLODIndex).LoadRawMesh(Mesh);

				// Build a mapping of vertex positions to vertex colors.
				for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
				{
					const int32 VertID = Mesh.WedgeIndices[WedgeIndex];
					for (uint32 TexCoordIndex = 0; TexCoordIndex < MAX_MESH_TEXTURE_COORDS; TexCoordIndex++)
					{
						if (TexCoordIndex <= NumUVChannels)
						{
							if ((TexCoordIndex == UVChannel))
							{
								if (Mesh.WedgeTexCoords[TexCoordIndex].Num() != Mesh.WedgeIndices.Num())
								{
									Mesh.WedgeTexCoords[TexCoordIndex].SetNum(Mesh.WedgeIndices.Num());
								}

								Mesh.WedgeTexCoords[TexCoordIndex][WedgeIndex] = static_cast<FVector2f>(UVs[PaintingMeshLODIndex][VertID]);
							}
							else if(TexCoordIndex >= NumUVChannels)
							{
								Mesh.WedgeTexCoords[TexCoordIndex].Empty();
							}
						}
						else
						{
							Mesh.WedgeTexCoords[TexCoordIndex].Empty();
						}
					}
				}

				// Determine which texture coordinate map should be used for storing/generating the lightmap UVs
				const uint32 LightMapIndex = FMath::Min(UVChannel == NumUVChannels ? NumUVChannels + 1 : NumUVChannels, static_cast<uint32>(MAX_MESH_TEXTURE_COORDS) - 1);
				StaticMesh->GetSourceModel(PaintingMeshLODIndex).BuildSettings.DstLightmapIndex = LightMapIndex;

				// Save the new raw mesh.
				StaticMesh->GetSourceModel(PaintingMeshLODIndex).SaveRawMesh(Mesh);


				StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
				// Set light map coordinate index to match DstLightmapIndex
				StaticMesh->SetLightMapCoordinateIndex(LightMapIndex);
			}
		}
	}

	// Build mesh from source
	StaticMesh->Build(false);
	StaticMesh->PostEditChange();
	if (!StaticMesh->MarkPackageDirty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not set the SM as dirty"));
	}
}

void FVertexAnimUtils::VATColorsToStaticMeshLODs(UStaticMesh* StaticMesh, const TArray<TArray<FColor>>& Colors)
{
	for (int32 LOD = 0; LOD < StaticMesh->GetNumLODs(); LOD++)
	{
		const uint32 PaintingMeshLODIndex = LOD;

		if (StaticMesh->IsSourceModelValid(PaintingMeshLODIndex))
		{
			if (StaticMesh->GetSourceModel(PaintingMeshLODIndex).IsRawMeshEmpty() == false)
			{
				// Extract the raw mesh.
				FRawMesh Mesh;
				StaticMesh->GetSourceModel(PaintingMeshLODIndex).LoadRawMesh(Mesh);

				// Reserve space for the new vertex colors.
				if (Mesh.WedgeColors.Num() == 0 || Mesh.WedgeColors.Num() != Mesh.WedgeIndices.Num())
				{
					Mesh.WedgeColors.Empty(Mesh.WedgeIndices.Num());
					Mesh.WedgeColors.AddUninitialized(Mesh.WedgeIndices.Num());
				}

				// Build a mapping of vertex positions to vertex colors.
				for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
				{
					const int32 VertID = Mesh.WedgeIndices[WedgeIndex];
					Mesh.WedgeColors[WedgeIndex] = Colors[PaintingMeshLODIndex][VertID];
				}

				// Save the new raw mesh.
				StaticMesh->GetSourceModel(PaintingMeshLODIndex).SaveRawMesh(Mesh);
				StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
			}
		}
	}

	// Build mesh from source
	StaticMesh->Build(false);
	StaticMesh->PostEditChange();
	if (!StaticMesh->MarkPackageDirty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Could not mark SM as dirty"));
	}
}

void SPickAssetDialog::Construct(const FArguments& InArgs)
{
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	const TSharedPtr <SVerticalBox> AssetPickerBox1 = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(0.1)
		.FillHeight(0.1)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Profile", "Profile"))
		];
	
	{
		// Vertex Anim Profile
		{
			FAssetPickerConfig AssetPickerConfig;
			AssetPickerConfig.Filter.ClassNames.Add(UVertexAnimProfile::StaticClass()->GetFName());
			AssetPickerConfig.SelectionMode = ESelectionMode::Single;

			AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate_Profile);

			AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateStatic(&SPickAssetDialog::Filter);
			AssetPickerConfig.bAllowNullSelection = true;
			AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
			AssetPickerConfig.ThumbnailScale = 1.f;

			AssetPickerBox1->AddSlot()
				.FillHeight(1)
				.Padding(3)
				[
					ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
				];
		}
	}

	// 
	AssetPath = FText::FromString(FPackageName::GetLongPackagePath(InArgs._DefaultAssetPath.ToString()));
	AssetName = FText::FromString(FPackageName::GetLongPackageAssetName(InArgs._DefaultAssetPath.ToString()));

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = AssetPath.ToString();
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SPickAssetDialog::OnPathChange);
	PathPickerConfig.bAddDefaultPath = true;
	//

	
    SWindow::Construct(SWindow::FArguments()
	.Title(InArgs._Title)
	.SupportsMinimize(false)
	.SupportsMaximize(false)
	.ClientSize(FVector2D(1080, 720))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
	.Padding(2, 2, 2, 4)
	.HAlign(HAlign_Fill)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
	.FillHeight(1)
	.Padding(3)
	.HAlign(HAlign_Fill)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
	.FillHeight(2)
	.Padding(3)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
	.FillWidth(1)
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		AssetPickerBox1.ToSharedRef()
	]
	]
	]
	]
	]
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 10, 0)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OnlyCreateStaticMesh", "Only Create Static Mesh"))
		]

	+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 10, 0)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("OnlyCreateStaticMesh_Tooltip", "Only create static mesh without baking any animation to textures"))
		.IsChecked(ECheckBoxState::Unchecked)
		.OnCheckStateChanged(this, &SPickAssetDialog::OnCheckedOnlyCreateStaticMesh)

		]

		]
    + SVerticalBox::Slot()
    .AutoHeight()
    .HAlign(HAlign_Right)
    .VAlign(VAlign_Bottom)
    [
	SNew(SUniformGridPanel)
	.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
	.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
	.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
	+ SUniformGridPanel::Slot(0, 0)
	[
		SNew(SButton)
		.Text(LOCTEXT("OK", "OK"))
	.HAlign(HAlign_Center)
	.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
	.OnClicked(this, &SPickAssetDialog::OnButtonClick, EAppReturnType::Ok)
	]
    + SUniformGridPanel::Slot(1, 0)
    [
	SNew(SButton)
	.Text(LOCTEXT("Cancel", "Cancel"))
	.HAlign(HAlign_Center)
	.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
	.OnClicked(this, &SPickAssetDialog::OnButtonClick, EAppReturnType::Cancel)
    ]
    ]
	]);
}

void SPickAssetDialog::OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo)
{
	AssetName = NewName;
}

void SPickAssetDialog::OnPathChange(const FString& NewPath)
{
	AssetPath = FText::FromString(NewPath);
}

FReply SPickAssetDialog::OnButtonClick(EAppReturnType::Type ButtonID)
{
	if (ButtonID == EAppReturnType::Ok)
	{
		const int32 ValidateResult = ValidateProfile();

		switch (ValidateResult)
		{
		case 0: // Valid Profile
			break;
		case 1:	// NULL Profile
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NULLProfile", "No Profile Selected"));
			return FReply::Unhandled();
		case 2:	// Invalid Offsets or Normals Texture
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NULLOffsetsNormalsTexture", "Selected Profile has invalid Offsets or Normals Texture"));
			return FReply::Unhandled();
		case 3:	// No Width / Height correspondence between Profile and Offsets Texture
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWidthHeightCorrespondenceOffsets", "Selected Profile has no Width / Height correspondence with Offsets texture"));
			return FReply::Unhandled();
		case 4: // No Width / Height correspondence between Profile and Normals Texture
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("Invalid Override Width Height", "Deactivated Auto Size, but invalid Override Size"));
			return FReply::Unhandled();
		case 5: // Profile has not Anims
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("SelectedProfileHasNoAnims", "Selected Profile has no Anims"));
			return FReply::Unhandled();
		case 6: // Invalid Sequence Ref
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidSequenceRefInProfile", "Selected Profile has anim with invalid Sequence Ref"));
			return FReply::Unhandled();
		case 7: // Anims have different Skeletons
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DifferentSkeletonsInProfileAnims", "Selected Profile has anims with different skeletons"));
			return FReply::Unhandled();
		case 8: // Anim has Num of Frames less than 1
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("AnimsInProfileWith0NumFrames", "Selected Profile has anim with Num Frames less than 1"));
			return FReply::Unhandled();
		default:
			break;
		};
	}

	//-----------------------------------------------------------------------------------------------------

	UserResponse = ButtonID;

	RequestDestroyWindow();
	
	return FReply::Handled();
}

/** Ensures supplied package name information is valid */
bool SPickAssetDialog::ValidatePackage() const
{
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(GetFullAssetPath().ToString(), false, &Reason)
		|| !FName(*AssetName.ToString()).IsValidObjectName(Reason))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Reason);
		return false;
	}

	if (FPackageName::DoesPackageExist(GetFullAssetPath().ToString()) || 
		FindObject<UObject>(nullptr, *(AssetPath.ToString() + "/" + AssetName.ToString() + "." + AssetName.ToString())) != nullptr)
	{
		if (GetSelectedProfile() != nullptr)
		{
			if (GetSelectedProfile()->StaticMesh == FindObject<UObject>(nullptr, *(AssetPath.ToString() + "/" + AssetName.ToString() + "." + AssetName.ToString())))
			{
				return true;
			}
		}

		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("AssetAlreadyExists", "Asset {0} already exists."), GetFullAssetPath()));
		return false;
	}

	return true;
}

EAppReturnType::Type SPickAssetDialog::ShowModal()
{
	GEditor->EditorAddModalWindow(SharedThis(this));
	return UserResponse;
}


const FText& SPickAssetDialog::GetAssetPath()
{
	return AssetPath;
}

const FText& SPickAssetDialog::GetAssetName()
{
	return AssetName;
}

FText SPickAssetDialog::GetFullAssetPath() const
{
	return FText::FromString(AssetPath.ToString() + "/" + AssetName.ToString());
}

void SPickAssetDialog::OnCheckedOnlyCreateStaticMesh(ECheckBoxState NewCheckedState)
{
	if (NewCheckedState == ECheckBoxState::Checked)
	{
		bOnlyCreateStaticMesh = true;
	}
	else
	{
		bOnlyCreateStaticMesh = false;
	}
}



UVertexAnimProfile * SPickAssetDialog::GetSelectedProfile() const
{
	TArray<FAssetData> SelectionArray = GetCurrentSelectionDelegate_Profile.Execute();

	if (SelectionArray.Num() > 0)
	{
		TArray <UVertexAnimProfile*> StoredProfiles;

		for (int i = 0; i < SelectionArray.Num(); i++)
		{
			SelectionArray[i].GetPackage()->FullyLoad();
			if (SelectionArray[i].IsAssetLoaded())
			{
				UVertexAnimProfile* NewSequence = Cast <UVertexAnimProfile>(SelectionArray[i].GetAsset());
				if (NewSequence)
				{
					StoredProfiles.Add(NewSequence);
				}
			}
		}

		return StoredProfiles[0];
	}

	return nullptr;
}



bool SPickAssetDialog::Filter(const FAssetData & AssetData)
{
	if (!AssetData.IsAssetLoaded())
	{
		AssetData.GetPackage()->FullyLoad();
	}

	return false;
}

int32 SPickAssetDialog::ValidateProfile() const
{
	UVertexAnimProfile* Profile = GetSelectedProfile();
	if (Profile != nullptr)
	{
		// Invalid Offsets or Normals Texture
		if ((!Profile->AutoSize) && (Profile->OverrideSize_Vert.GetMax() < 8)) 
		{
			return 4;
		}

		// Profile has not Anims
		if ((Profile->Anims_Vert.Num() == 0) && (Profile->Anims_Bone.Num() == 0)) 
		{
			return 5;
		}

		const USkeleton* Skeleton = nullptr;
		if (Profile->Anims_Vert.Num() && Profile->Anims_Vert[0].SequenceRef != nullptr) 
		{
			Skeleton = Profile->Anims_Vert[0].SequenceRef->GetSkeleton();
		}

		if (!Skeleton && Profile->Anims_Bone.Num() > 0 && Profile->Anims_Bone[0].SequenceRef != nullptr) 
		{
			Skeleton = Profile->Anims_Bone[0].SequenceRef->GetSkeleton();
		}

		
		for (int32 i = 0; i < Profile->Anims_Vert.Num(); i++)
		{
			// Invalid Sequence Ref
			if (Profile->Anims_Vert[i].SequenceRef == nullptr) return 6;
			
			if (Profile->Anims_Vert[i].SequenceRef->GetSkeleton() != Skeleton)
			{
				// Anims have different Skeletons
				return 7;
			}
			if ((Profile->Anims_Vert[i].NumFrames < 1))
			{
				// Anim has Num Frames less than 1
				return 8;
			}
		}
		
		
		for (int32 i = 0; i < Profile->Anims_Bone.Num(); i++)
		{
			// Invalid Sequence Ref
			if (Profile->Anims_Bone[i].SequenceRef == nullptr) 
			{
				return 6;
			}

			if (Profile->Anims_Bone[i].SequenceRef->GetSkeleton() != Skeleton)
			{
				// Anims have different Skeletons
				return 7;
			}

			if ((Profile->Anims_Bone[i].NumFrames < 1))
			{
				// Anim has Num Frames less than 1
				return 8;
			}
		}

		return 0;
	}

	// NULL PROFILE
	return 1;
}


#undef LOCTEXT_NAMESPACE
