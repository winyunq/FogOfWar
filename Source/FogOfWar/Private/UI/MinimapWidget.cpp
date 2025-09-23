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
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	if (FogOfWarActor)
	{
		// 创建渲染目标和数据纹理
		if (!MinimapRenderTarget)
		{
			MinimapRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureResolution.X, TextureResolution.Y, ETextureRenderTargetFormat::RTF_RGBA8);
		}
		if (!VisionDataTexture)
		{
			VisionDataTexture = CreateDynamicDataTexture(this, 128, 1, TEXT("VisionDataTexture")); // 最多支持128个视野源
		}
		if (!IconDataTexture)
		{
			IconDataTexture = CreateDynamicDataTexture(this, 256, 1, TEXT("IconDataTexture")); // 最多支持256个图标
		}
		if (!IconColorTexture)
		{
			IconColorTexture = CreateDynamicDataTexture(this, 256, 1, TEXT("IconColorTexture")); // 与IconDataTexture对应
		}
		
		if (MinimapMaterial)
		{
			MinimapMaterialInstance = UMaterialInstanceDynamic::Create(MinimapMaterial, this);
			// 将数据纹理设置给材质
			MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionDataTexture"), VisionDataTexture);
			MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconDataTexture"), IconDataTexture);
			MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconColorTexture"), IconColorTexture);

			// 从AFogOfWar同步坐标系信息到材质
			MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(FogOfWarActor->GridBottomLeftWorldLocation.X, FogOfWarActor->GridBottomLeftWorldLocation.Y, 0));
			MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(FogOfWarActor->GridSize.X, FogOfWarActor->GridSize.Y, 0));
		
			UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized from AFogOfWar."));
		}
		else
		{
			UE_LOG(LogMinimapWidget, Warning, TEXT("MinimapMaterial is not set."));
		}

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
		
		// --- 6. Update Image Brush ---
		if (MinimapImage)
		{
			FSlateBrush Brush = MinimapImage->GetBrush();
			Brush.SetResourceObject(MinimapRenderTarget);
			MinimapImage->SetBrush(Brush);
		}
		return true;
	}
	UE_LOG(LogMinimapWidget, Error, TEXT("InitializeFromWorldFogOfWar failed: AFogOfWar actor not found in the level."));
	return false;
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
	UWorld* World = GetWorld();
	if (!World || !MinimapMaterialInstance || !MinimapRenderTarget || !VisionDataTexture || !IconDataTexture || !IconColorTexture || !MinimapDataSubsystem)
	{
		UE_LOG(LogMinimapWidget, Warning, TEXT("UpdateMinimapTexture skipped: A required component is null."));
		return;
	}

	// --- 1. Icon Data Collection ---
	TArray<FVector4> TempIconLocations;
	MinimapDataSubsystem->IconLocations.GenerateValueArray(TempIconLocations);

	TArray<FLinearColor> TempIconColors;
	MinimapDataSubsystem->IconColors.GenerateValueArray(TempIconColors);

	// --- 2. Vision Data Collection ---
	TArray<FVector4> TempVisionSources;
	MinimapDataSubsystem->VisionSources.GenerateValueArray(TempVisionSources);

	UE_LOG(LogMinimapWidget, Log, TEXT("Data fetched from Subsystem: %d vision sources, %d icons."), TempVisionSources.Num(), TempIconLocations.Num());

	// --- 3. Update Data Textures ---
	const int32 NumIcons = TempIconLocations.Num();
	TArray<FLinearColor> IconLocationPixelData;
	if (NumIcons > 0)
	{
		IconLocationPixelData.SetNumUninitialized(NumIcons);
		FMemory::Memcpy(IconLocationPixelData.GetData(), TempIconLocations.GetData(), NumIcons * sizeof(FVector4));
	}
	IconLocationPixelData.SetNumZeroed(IconDataTexture->GetSizeX()); // Pad array to match texture size
	UpdateDataTexture(IconDataTexture, IconLocationPixelData);

	TArray<FLinearColor> IconColorPixelData = TempIconColors;
	IconColorPixelData.SetNumZeroed(IconColorTexture->GetSizeX());
	UpdateDataTexture(IconColorTexture, IconColorPixelData);

	const int32 NumVision = TempVisionSources.Num();
	TArray<FLinearColor> VisionPixelData;
	if (NumVision > 0)
	{
		VisionPixelData.SetNumUninitialized(NumVision);
		FMemory::Memcpy(VisionPixelData.GetData(), TempVisionSources.GetData(), NumVision * sizeof(FVector4));
	}
	VisionPixelData.SetNumZeroed(VisionDataTexture->GetSizeX()); // Pad array to match texture size
	UpdateDataTexture(VisionDataTexture, VisionPixelData);

	// --- 4. Update Material Parameters ---
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumIcons"), NumIcons);
	MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumVisionSources"), NumVision);

	const FLinearColor OpaqueBackgroundColor = FLinearColor::Black;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, MinimapRenderTarget, OpaqueBackgroundColor);

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);

}