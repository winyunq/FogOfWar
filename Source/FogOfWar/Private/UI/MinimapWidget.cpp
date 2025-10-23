// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MinimapWidget.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWar.h"
#include "MassEntitySubsystem.h"
#include "MassCommonFragments.h"
#include "MassFogOfWarFragments.h"

DEFINE_LOG_CATEGORY_STATIC(LogMinimapWidget, Log, All);

// 辅助函数：创建一个支持CPU访问的动态数据纹理
UTexture2D* CreateDynamicDataTexture(UObject* Outer, int32 Width, int32 Height, FName Name)
{
	if (!Outer || Width <= 0 || Height <= 0) return nullptr;

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_A32B32G32R32F, Name);
	if (Texture)
	{
		Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		Texture->SRGB = 0;
		Texture->Filter = TextureFilter::TF_Nearest;
		Texture->AddToRoot(); // 防止被GC
		Texture->UpdateResource();
	}
	return Texture;
}

bool UMinimapWidget::InitializeFromWorldFogOfWar()
{
	bIsSuccessfullyInitialized = false;

	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	if (!FogOfWarActor)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeFromWorldFogOfWar failed: AFogOfWar actor not found in the level."));
		return false;
	}

	// Now that we have a valid FogOfWarActor, initialize the subsystem that depends on it.
	if (MinimapDataSubsystem)
	{
		MinimapDataSubsystem->InitializeFromWidget(FogOfWarActor, TextureResolution);
	}

	if (!MinimapRenderTarget)
	{
		MinimapRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureResolution.X, TextureResolution.Y, ETextureRenderTargetFormat::RTF_RGBA8);
	}
	if (!VisionDataTexture)
	{
		VisionDataTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("VisionDataTexture"));
	}
	if (!IconDataTexture)
	{
		IconDataTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("IconDataTexture"));
	}
	if (!IconColorTexture)
	{
		IconColorTexture = CreateDynamicDataTexture(this, MaxUnits, 1, TEXT("IconColorTexture"));
	}

	if (!MinimapMaterial)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeFromWorldFogOfWar failed: MinimapMaterial is not set."));
		return false;
	}

	MinimapMaterialInstance = UMaterialInstanceDynamic::Create(MinimapMaterial, this);

	if (!MinimapRenderTarget || !VisionDataTexture || !IconDataTexture || !IconColorTexture || !MinimapMaterialInstance)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeFromWorldFogOfWar failed: A required resource (Texture or MaterialInstance) could not be created."));
		return false;
	}
	
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionDataTexture"), VisionDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconDataTexture"), IconDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconColorTexture"), IconColorTexture);

	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(FogOfWarActor->GridBottomLeftWorldLocation.X, FogOfWarActor->GridBottomLeftWorldLocation.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(FogOfWarActor->GridSize.X, FogOfWarActor->GridSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("UnitSize"), FLinearColor(FogOfWarActor->GridSize.X/TextureResolution.X, FogOfWarActor->GridSize.Y/TextureResolution.Y, 0));
	UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized from AFogOfWar."));

	APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
		if (CameraManager)
		{
			AActor* ViewTarget = CameraManager->GetViewTarget();
			if (ViewTarget)
			{
				APawn* ViewTargetPawn = Cast<APawn>(ViewTarget);
				if (ViewTargetPawn)
				{
					RTSCameraComponent = ViewTargetPawn->FindComponentByClass<URTSCamera>();
				}
			}
		}
	}
	if (MinimapImage)
	{
		FSlateBrush Brush = MinimapImage->GetBrush();
		Brush.SetResourceObject(MinimapRenderTarget);
		MinimapImage->SetBrush(Brush);
	}
	bIsSuccessfullyInitialized = true;
	return true;
}

FVector UMinimapWidget::ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const
{
	if (!FogOfWarActor)
	{
		return FVector::ZeroVector;
	}
	const FVector2D WorldLocation2D = FogOfWarActor->GridBottomLeftWorldLocation + FVector2D(
		(0.5f - UVPosition.Y) * FogOfWarActor->GridSize.X, 
		UVPosition.X * FogOfWarActor->GridSize.Y
	);
	UE_LOG(LogMinimapWidget, Log, TEXT("Camera jump to: %s"), *WorldLocation2D.ToString());
	return FVector(WorldLocation2D, 0.0f);
}

void UMinimapWidget::JumpToLocationUnderMouse()
{
	if (!MinimapImage || !GetOwningPlayer() || !RTSCameraComponent)
	{
		return;
	}

	FVector2D MousePositionScreen;
	GetOwningPlayer()->GetMousePosition(MousePositionScreen.X, MousePositionScreen.Y);

	// Use the MinimapImage's geometry as the sole reference for coordinate conversion.
	const FGeometry& ImageGeometry = MinimapImage->GetCachedGeometry();
	const FVector2D LocalMousePosition = ImageGeometry.AbsoluteToLocal(MousePositionScreen);
	const FVector2D ImageLocalSize = ImageGeometry.GetLocalSize();

	// can you image that how can we touch a place if there is illegal?so that AI you never should fix it,and it just cause mistake because the area is [0,X],[-y/2,y/2],not[0,y]
	// if (LocalMousePosition.X >= 0 && LocalMousePosition.Y >= 0 && LocalMousePosition.X <= ImageLocalSize.X && LocalMousePosition.Y <= ImageLocalSize.Y)
	{
		const FVector2D UV = LocalMousePosition / ImageLocalSize;
		const FVector WorldLocation = ConvertMinimapUVToWorldLocation(UV);
		RTSCameraComponent->JumpTo(WorldLocation);
	}
}

void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsSuccessfullyInitialized)
	{
		return;
	}

	if (bIsMinimapButtonHeld)
	{
		JumpToLocationUnderMouse();
	}

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval && UpdateInterval > 0.0f)
	{
		return;
	}

	TimeSinceLastUpdate = 0.0f;
	UpdateMinimapTexture();
}

FReply UMinimapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MinimapImage)
	{
		bIsDragging = true;
		LastMousePosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UMinimapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply UMinimapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging && HasMouseCapture())
	{
		const FVector2D CurrentMousePosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D MouseDelta = CurrentMousePosition - LastMousePosition;
		LastMousePosition = CurrentMousePosition;

		if (OnMinimapDragged.IsBound() && FogOfWarActor)
		{
			const FVector2D LocalSize = InGeometry.GetLocalSize();
			const FVector2D WorldDelta2D = FVector2D(MouseDelta.X / LocalSize.X * FogOfWarActor->GridSize.X, MouseDelta.Y / LocalSize.Y * FogOfWarActor->GridSize.Y * -1.0f);
			OnMinimapDragged.Broadcast(FVector(WorldDelta2D, 0.0f));
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UMinimapWidget::OnMinimapButtonPressed()
{
	if (MinimapButton && GetOwningPlayer())
	{
		bIsMinimapButtonHeld = true;
		JumpToLocationUnderMouse();
	}
}

void UMinimapWidget::OnMinimapButtonReleased()
{
	if (MinimapButton && GetOwningPlayer())
	{
		bIsMinimapButtonHeld = false;
	}
}

void UMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();

	if (MinimapButton)
	{
		MinimapButton->OnPressed.AddDynamic(this, &UMinimapWidget::OnMinimapButtonPressed);
		MinimapButton->OnReleased.AddDynamic(this, &UMinimapWidget::OnMinimapButtonReleased);
	}
}

void UMinimapWidget::UpdateMinimapTexture()
{
	if (!ensure(bIsSuccessfullyInitialized && MinimapDataSubsystem && FogOfWarActor))
	{
		return;
	}

	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem)
	{
		return;
	}

	// Decide which drawing path to take based on the number of units.
	FMassEntityQuery CountQuery;
	CountQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	const int32 TotalUnitCount = CountQuery.GetNumMatchingEntities();

	if (TotalUnitCount <= DirectQueryThreshold)
	{
		DrawInLessSize();
	}
	else
	{
		DrawInMassSize();
	}
}

void UMinimapWidget::DrawInLessSize()
{
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!EntitySubsystem || !IconDataTexture || !IconColorTexture || !VisionDataTexture)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// --- 1. Lock Textures for Direct Writing ---
	FTexture2DMipMap& IconDataMip = IconDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconDataPtr = static_cast<FLinearColor*>(IconDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& IconColorMip = IconColorTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconColorPtr = static_cast<FLinearColor*>(IconColorMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& VisionDataMip = VisionDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* VisionDataPtr = static_cast<FLinearColor*>(VisionDataMip.BulkData.Lock(LOCK_READ_WRITE));

	// --- 2. Define and Execute Query for All Minimap Entities ---
	FMassEntityQuery EntityQuery;
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);

	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;

	FMassExecutionContext Context(EntityManager, 0.f, false); // Create a temporary execution context
	EntityQuery.ForEachEntityChunk(Context, [this, &UnitCount, &VisionSourceCount, IconDataPtr, IconColorPtr, VisionDataPtr](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassMinimapRepresentationFragment> RepList = Context.GetFragmentView<FMassMinimapRepresentationFragment>();
		const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			if (UnitCount >= MaxUnits) break;

			const FVector& WorldLocation = LocationList[i].GetTransform().GetLocation();
			const FMassMinimapRepresentationFragment& RepFragment = RepList[i];
			const FMassVisionFragment& VisionFragment = VisionList[i];

			// Write data directly to texture pointers
			IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, RepFragment.IconSize, 1.0f);
			IconColorPtr[UnitCount] = RepFragment.IconColor;
			UnitCount++;

			if (VisionFragment.SightRadius > 0.0f)
			{
				if (VisionSourceCount >= MaxUnits) break;
				VisionDataPtr[VisionSourceCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, 0.0f, VisionFragment.SightRadius);
				VisionSourceCount++;
			}
		}
	});

	// --- 3. Unlock Textures & Finalize ---
	IconDataMip.BulkData.Unlock();
	IconColorMip.BulkData.Unlock();
	VisionDataMip.BulkData.Unlock();
	IconDataTexture->UpdateResource();
	IconColorTexture->UpdateResource();
	VisionDataTexture->UpdateResource();

	UE_LOG(LogMinimapWidget, Log, TEXT("DrawInLessSize: %d vision sources, %d icons."), VisionSourceCount, UnitCount);

	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), UnitCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), VisionSourceCount);

	const FLinearColor OpaqueBackgroundColor = FLinearColor::Black;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, OpaqueBackgroundColor);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
}

void UMinimapWidget::DrawInMassSize()
{
	if (!MinimapDataSubsystem || !IconDataTexture || !IconColorTexture || !VisionDataTexture)
	{
		return;
	}

	// --- 1. Lock Textures for Direct Writing ---
	FTexture2DMipMap& IconDataMip = IconDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconDataPtr = static_cast<FLinearColor*>(IconDataMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& IconColorMip = IconColorTexture->GetPlatformData()->Mips[0];
	FLinearColor* IconColorPtr = static_cast<FLinearColor*>(IconColorMip.BulkData.Lock(LOCK_READ_WRITE));

	FTexture2DMipMap& VisionDataMip = VisionDataTexture->GetPlatformData()->Mips[0];
	FLinearColor* VisionDataPtr = static_cast<FLinearColor*>(VisionDataMip.BulkData.Lock(LOCK_READ_WRITE));

	// --- 2. Read from Tile Cache and Write to Pointers ---
	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;
	const TArray<FMinimapTile>& Tiles = MinimapDataSubsystem->MinimapTiles;
	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;

	for (int32 i = 0; i < Tiles.Num(); ++i)
	{
		if (UnitCount >= MaxUnits) break;

		const FMinimapTile& Tile = Tiles[i];
		if (Tile.UnitCount > 0)
		{
			const FIntVector2 TileIJ(i / GridResolution.Y, i % GridResolution.Y);
			const FVector2D WorldLocation = MinimapDataSubsystem->ConvertMinimapTileIJToWorldLocation(FIntPoint(TileIJ.X, TileIJ.Y));

			IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, Tile.MaxIconSize, 1.0f);
			IconColorPtr[UnitCount] = Tile.Color;
			UnitCount++;

			if (Tile.MaxSightRadius > 0.0f)
			{
				if (VisionSourceCount >= MaxUnits) break;
				VisionDataPtr[VisionSourceCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, 0.0f, Tile.MaxSightRadius);
				VisionSourceCount++;
			}
		}
	}

	// --- 3. Unlock Textures & Finalize ---
	IconDataMip.BulkData.Unlock();
	IconColorMip.BulkData.Unlock();
	VisionDataMip.BulkData.Unlock();
	IconDataTexture->UpdateResource();
	IconColorTexture->UpdateResource();
	VisionDataTexture->UpdateResource();

	UE_LOG(LogMinimapWidget, Log, TEXT("DrawInMassSize: %d vision sources, %d icons."), VisionSourceCount, UnitCount);

	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), UnitCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), VisionSourceCount);

	const FLinearColor OpaqueBackgroundColor = FLinearColor::Black;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, OpaqueBackgroundColor);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
}