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

// 辅助函数：更新数据纹理的内容
void UpdateDataTexture(UTexture2D* Texture, const TArray<FLinearColor>& Data)
{
	if (!Texture || !Texture->GetPlatformData() || !Texture->GetPlatformData()->Mips.IsValidIndex(0))
	{
		return;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	const int32 DataSize = FMath::Min(Data.Num(), Texture->GetSizeX() * Texture->GetSizeY()) * sizeof(FLinearColor);
	FMemory::Memcpy(TextureData, Data.GetData(), DataSize);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
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

	if (!MinimapRenderTarget)
	{
		MinimapRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureResolution.X, TextureResolution.Y, ETextureRenderTargetFormat::RTF_RGBA8);
	}
	if (!VisionDataTexture)
	{
		VisionDataTexture = CreateDynamicDataTexture(this, 128, 1, TEXT("VisionDataTexture"));
	}
	if (!IconDataTexture)
	{
		IconDataTexture = CreateDynamicDataTexture(this, 256, 1, TEXT("IconDataTexture"));
	}
	if (!IconColorTexture)
	{
		IconColorTexture = CreateDynamicDataTexture(this, 256, 1, TEXT("IconColorTexture"));
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

void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsSuccessfullyInitialized)
	{
		return;
	}

	if (bIsMinimapButtonHeld && GetOwningPlayer())
	{
		FVector2D MousePosition;
		GetOwningPlayer()->GetMousePosition(MousePosition.X, MousePosition.Y);
		JumpToMousePointOnMinimap(MousePosition, MyGeometry);
	}

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval && UpdateInterval > 0.0f)
	{
		return;
	}

	TimeSinceLastUpdate = 0.0f;
	UpdateMinimapTexture();
}

void UMinimapWidget::JumpToMousePointOnMinimap(const FVector2D& ScreenPosition, const FGeometry& WidgetGeometry)
{
	const FVector2D LocalPosition = WidgetGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = WidgetGeometry.GetLocalSize();
	const FVector2D UV = LocalPosition / LocalSize;
	const FVector WorldLocation = ConvertMinimapUVToWorldLocation(UV);
		
	if (RTSCameraComponent)
	{
		RTSCameraComponent->JumpTo(WorldLocation);
	}
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
		FVector2D MousePosition;
		GetOwningPlayer()->GetMousePosition(MousePosition.X, MousePosition.Y);
		JumpToMousePointOnMinimap(MousePosition, this->MinimapButton->GetCachedGeometry());
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

	// --- 1. Data Collection from new Tile-based Subsystem ---
	TArray<FVector4> TempIconLocations;
	TArray<FLinearColor> TempIconColors;
	TArray<FVector4> TempVisionSources; 

	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;
	const TArray<FMinimapTile>& Tiles = MinimapDataSubsystem->MinimapTiles;

	for (int32 i = 0; i < Tiles.Num(); ++i)
	{
		const FMinimapTile& Tile = Tiles[i];
		if (Tile.UnitCount > 0)
		{
			// This tile has units, convert its 1D index back to 2D grid coordinates
			const FIntVector2 TileIJ(i / GridResolution.Y, i % GridResolution.Y);
			
			// Convert grid coordinates to world location for the shader
			const FVector2D WorldLocation = FogOfWarActor->ConvertTileIJToTileCenterWorldLocation(TileIJ);

			// Add icon data
			TempIconLocations.Add(FVector4(WorldLocation.X, WorldLocation.Y, 25.0f, 1.0f));
			TempIconColors.Add(Tile.Color);

			// If the tile has a sight radius, add it to the vision sources list for the GPU.
			if (Tile.MaxSightRadius > 0.0f)
			{
				// The shader expects FVector4(X, Y, 0, Radius)
				TempVisionSources.Add(FVector4(WorldLocation.X, WorldLocation.Y, 0.0f, Tile.MaxSightRadius));
			}
		}
	}

	UE_LOG(LogMinimapWidget, Log, TEXT("Data fetched from Subsystem: %d vision sources, %d icons."), TempVisionSources.Num(), TempIconLocations.Num());

	// --- 3. Update Data Textures --- 
	const int32 NumberOfUnits = TempIconLocations.Num();
	TArray<FLinearColor> IconLocationPixelData;
	// SAFE CONVERSION: Manually and explicitly convert FVector4 to FLinearColor.
	IconLocationPixelData.Reserve(NumberOfUnits);
	for (const FVector4& Location : TempIconLocations)
	{
		IconLocationPixelData.Add(FLinearColor(Location.X, Location.Y, Location.Z, Location.W));
	}
	
	IconLocationPixelData.SetNumZeroed(IconDataTexture->GetSizeX()); // Pad array to match texture size
	UpdateDataTexture(IconDataTexture, IconLocationPixelData);

	TArray<FLinearColor> IconColorPixelData = TempIconColors;
	IconColorPixelData.SetNumZeroed(IconColorTexture->GetSizeX());
	UpdateDataTexture(IconColorTexture, IconColorPixelData);

	const int32 NumVision = TempVisionSources.Num();
	TArray<FLinearColor> VisionPixelData;
	// SAFE CONVERSION for Vision Sources as well
	VisionPixelData.Reserve(NumVision);
	for (const FVector4& VisionSource : TempVisionSources)
	{
		VisionPixelData.Add(FLinearColor(VisionSource.X, VisionSource.Y, VisionSource.Z, VisionSource.W));
	}
	VisionPixelData.SetNumZeroed(VisionDataTexture->GetSizeX()); // Pad array to match texture size
	UpdateDataTexture(VisionDataTexture, VisionPixelData);

	// --- 4. Update Material Parameters ---
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), NumberOfUnits);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), NumVision);

	// const FLinearColor OpaqueBackgroundColor = FLinearColor::Black;
	// UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, OpaqueBackgroundColor);

	//UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
}