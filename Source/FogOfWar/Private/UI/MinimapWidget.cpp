// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/MinimapWidget.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/MinimapDataSubsystem.h"
// #include "FogOfWar.h" // Removed for strict decoupling
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

bool UMinimapWidget::InitializeMinimapSystem()
{
	bIsSuccessfullyInitialized = false;

	if (!MinimapDataSubsystem)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: MinimapDataSubsystem not found."));
		return false;
	}

	MinimapDataSubsystem->SetMinimapResolution(TextureResolution);

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
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: MinimapMaterial is not set."));
		return false;
	}

	MinimapMaterialInstance = UMaterialInstanceDynamic::Create(MinimapMaterial, this);

	if (!MinimapRenderTarget || !VisionDataTexture || !IconDataTexture || !IconColorTexture || !MinimapMaterialInstance)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("InitializeMinimapSystem failed: A required resource could not be created."));
		return false;
	}
	
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionDataTexture"), VisionDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconDataTexture"), IconDataTexture);
	MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconColorTexture"), IconColorTexture);

	// Use Subsystem Data for Bounds
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(MinimapDataSubsystem->GridBottomLeftWorldLocation.X, MinimapDataSubsystem->GridBottomLeftWorldLocation.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(MinimapDataSubsystem->GridSize.X, MinimapDataSubsystem->GridSize.Y, 0));
	MinimapMaterialInstance->SetVectorParameterValue(TEXT("UnitSize"), FLinearColor(MinimapDataSubsystem->GridSize.X/TextureResolution.X, MinimapDataSubsystem->GridSize.Y/TextureResolution.Y, 0));

	// Configure Mass Queries once.
	UMassEntitySubsystem* EntitySubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		CountQuery = FMassEntityQuery(EntityManager.AsShared());
		CountQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);

		DrawQuery = FMassEntityQuery(EntityManager.AsShared());
		DrawQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
		DrawQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
		DrawQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	}
	
	UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized Minimap System."));

	UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized Minimap System."));

	if (MinimapImage)
	{
		FSlateBrush Brush = MinimapImage->GetBrush();
		Brush.SetResourceObject(MinimapRenderTarget);
		MinimapImage->SetBrush(Brush);
	}
	bIsSuccessfullyInitialized = true;
	return true;
}

void UMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Get Subsystem reference generally
	MinimapDataSubsystem = UMinimapDataSubsystem::Get();

	// Initialize
	InitializeMinimapSystem();
}

FVector UMinimapWidget::ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const
{
	if (!MinimapDataSubsystem)
	{
		return FVector::ZeroVector;
	}
	// Coordinate System: X is Up (North), Y is Right (East)
	// UI: X is Right, Y is Down.
	// Map:
	// UI X (Right) -> World Y (East)
	// UI Y (Down)  -> World X (South/Down) => Invert for North

	const FVector2D& Origin = MinimapDataSubsystem->GridBottomLeftWorldLocation;
	const FVector2D& Size = MinimapDataSubsystem->GridSize;

	// UV.X (0..1 Right) -> Delta Y (0..SizeY)
	float WorldY = Origin.Y + UVPosition.X * Size.Y;
	
	// UV.Y (0..1 Down) -> Delta X (SizeX..0)
	// Top (0) -> Max X. Bottom (1) -> Min X.
	float WorldX = Origin.X + (1.0f - UVPosition.Y) * Size.X;

	return FVector(WorldX, WorldY, 0.0f);
}

void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsSuccessfullyInitialized)
	{
		return;
	}

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval && UpdateInterval > 0.0f)
	{
		return;
	}

	TimeSinceLastUpdate = 0.0f;

	// [Path B] 每一帧触发 HashGrid 的直接查询和调试绘制
	// 以后这里会移动到 Mass 的 Processor 中，但为了立即见效，我们在 UI Tick 中驱动。
	if (MinimapDataSubsystem && GetOwningPlayer())
	{
		FVector CenterLocation = FVector::ZeroVector;
		if (APawn* Pawn = GetOwningPlayerPawn())
		{
			CenterLocation = Pawn->GetActorLocation();
		}
		
		// 触发子系统的逻辑 (含 DebugDrawing)
		MinimapDataSubsystem->UpdateMinimapFromHashGrid(CenterLocation, 16); // 半径 16 Block = 范围 32x32 Block
	}

	UpdateMinimapTexture();
}

void UMinimapWidget::UpdateMinimapTexture()
{
	if (!ensure(bIsSuccessfullyInitialized && MinimapDataSubsystem))
	{
		return;
	}

	// Always use the optimized Tile-based Rendering (Path B)
	// This relies on the Subsystem populating MinimapTiles from the HashGrid each frame.
	DrawInMassSize();
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
	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;

	if (!DrawQuery.IsInitialized())
	{
		return;
	}

	// The FMassExecutionContext is now created via the EntityManager and passed to the query.
	FMassExecutionContext Context = EntityManager.CreateExecutionContext(0.f);
	DrawQuery.ForEachEntityChunk(Context, [this, &UnitCount, &VisionSourceCount, IconDataPtr, IconColorPtr, VisionDataPtr](FMassExecutionContext& Context)
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
	const FIntPoint GridResolution = MinimapDataSubsystem->MinimapGridResolution;
	const TArray<FMinimapTile>& Tiles = MinimapDataSubsystem->MinimapTiles;
	int32 UnitCount = 0;
	int32 VisionSourceCount = 0;

	// Calculate Minimum Visible Size (e.g. 2.5 pixels wide) to avoid sub-pixel filtering
	// Scale Factor relative to World Units
	const float WorldPerPixelX = MinimapDataSubsystem->GridSize.X / (float)GridResolution.X;
	const float MinimumVisibleSize = WorldPerPixelX * 2.5f; 

	int32 MaxUnitsInSingleTile = 0;
	int32 TotalRealUnits = 0;

	for (int32 i = 0; i < Tiles.Num(); ++i)
	{
		if (UnitCount >= MaxUnits) break;

		const FMinimapTile& Tile = Tiles[i];
		if (Tile.UnitCount > 0)
		{
			MaxUnitsInSingleTile = FMath::Max(MaxUnitsInSingleTile, Tile.UnitCount);
			TotalRealUnits += Tile.UnitCount;

			const FIntPoint TileIJ(i / GridResolution.Y, i % GridResolution.Y);
			const FVector2D WorldLocation = UMinimapDataSubsystem::ConvertMinimapTileIJToWorldLocation_Static(TileIJ);

			// Smart Sizing: Ensure at least Minimum, but preserve larger if defined.
			// This fixes the "Invisible Icon" issue when resolution is low (e.g. 256x256).
			const float FinalSize = FMath::Max(Tile.MaxIconSize, MinimumVisibleSize);
			
			// Use the tile color (which came from the unit)
			IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, FinalSize, 1.0f);
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

	// Log Debug Info to help user understand "1 icon" vs "1000 units"
	// Only log if something changed to avoid spam, or log every few seconds (here we rely on log suppression or manual observation)
	if (UnitCount > 0)
	{
		UE_LOG(LogMinimapWidget, Log, TEXT("DrawInMassSize: %d Active Tiles (Icons), %d Total Unknown Units in Grid. Max Stack: %d."), UnitCount, TotalRealUnits, MaxUnitsInSingleTile);
	}

	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfUnits"), UnitCount);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumberOfVisionSources"), VisionSourceCount);

	const FLinearColor OpaqueBackgroundColor = FLinearColor::Black;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, OpaqueBackgroundColor);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);
}