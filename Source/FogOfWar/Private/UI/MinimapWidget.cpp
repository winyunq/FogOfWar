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

DEFINE_LOG_CATEGORY_STATIC(LogMinimapWidget, Log, All);

// 辅助函数：创建一个支持CPU访问的动态数据纹理
UTexture2D* CreateDynamicDataTexture(UObject* Outer, int32 Width, int32 Height)
{
	if (!Outer || Width <= 0 || Height <= 0) return nullptr;

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_A32B32G32R32F);
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

// 辅助函数：更新数据纹理的内容 (正确版本)
void UpdateDataTexture(UTexture2D* Texture, const TArray<FLinearColor>& Data)
{
	if (!Texture || Data.Num() == 0 || !Texture->GetPlatformData())
	{
		return;
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	const int32 DataSize = Data.Num() * sizeof(FLinearColor);
	FMemory::Memcpy(TextureData, Data.GetData(), DataSize);
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}

bool UMinimapWidget::InitializeFromWorldFogOfWar()
{
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
	if (FogOfWarActor)
	{
		if (MinimapMaterialInstance)
		{
			// 从AFogOfWar同步坐标系信息到材质
			MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridBottomLeftWorldLocation"), FLinearColor(FogOfWarActor->GridBottomLeftWorldLocation.X, FogOfWarActor->GridBottomLeftWorldLocation.Y, 0));
			MinimapMaterialInstance->SetVectorParameterValue(TEXT("GridSize"), FLinearColor(FogOfWarActor->GridSize.X, FogOfWarActor->GridSize.Y, 0));
		
						UE_LOG(LogMinimapWidget, Log, TEXT("Successfully initialized from AFogOfWar. GridBottomLeft: %s, GridSize: %s"), *FogOfWarActor->GridBottomLeftWorldLocation.ToString(), *FogOfWarActor->GridSize.ToString());
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
								// If the ViewTarget is a Pawn, try to find the RTSCameraComponent on it.
								APawn* ViewTargetPawn = Cast<APawn>(ViewTarget);
								if (ViewTargetPawn)
								{
									RTSCameraComponent = ViewTargetPawn->FindComponentByClass<URTSCamera>();
									if (!RTSCameraComponent)
									{
										UE_LOG(LogMinimapWidget, Warning, TEXT("UMinimapWidget: Could not find URTSCamera component on ViewTarget Pawn (%s)."), *ViewTargetPawn->GetName());
									}
								}
								else
								{
									UE_LOG(LogMinimapWidget, Warning, TEXT("UMinimapWidget: ViewTarget (%s) is not a Pawn. Cannot find URTSCamera component."), *ViewTarget->GetName());
								}
							}
							else
							{
								UE_LOG(LogMinimapWidget, Warning, TEXT("UMinimapWidget: PlayerCameraManager has no ViewTarget."));
							}
						}
						else
						{
							UE_LOG(LogMinimapWidget, Warning, TEXT("UMinimapWidget: Could not get PlayerCameraManager."));
						}
					}
					else
					{
						UE_LOG(LogMinimapWidget, Warning, TEXT("UMinimapWidget: Could not get owning PlayerController."));
					}

						// 创建渲染目标和数据纹理						if (!MinimapRenderTarget)
						{
							MinimapRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureResolution.X, TextureResolution.Y, ETextureRenderTargetFormat::PF_A32B32G32R32F);
						}
						if (!VisionDataTexture)
						{
							VisionDataTexture = CreateDynamicDataTexture(this, 128, 1); // 最多支持128个视野源
						}
						if (!IconDataTexture)
						{
							IconDataTexture = CreateDynamicDataTexture(this, 256, 1); // 最多支持256个图标
						}
			
						if (MinimapMaterial)
						{
							MinimapMaterialInstance = UMaterialInstanceDynamic::Create(MinimapMaterial, this);
							// 将数据纹理设置给材质
							MinimapMaterialInstance->SetTextureParameterValue(TEXT("VisionDataTexture"), VisionDataTexture);
							MinimapMaterialInstance->SetTextureParameterValue(TEXT("IconDataTexture"), IconDataTexture);
						}
						else
						{
							UE_LOG(LogMinimapWidget, Warning, TEXT("MinimapMaterial is not set."));
						}
			
					return true;	}
	UE_LOG(LogMinimapWidget, Error, TEXT("InitializeFromWorldFogOfWar failed: AFogOfWar actor not found in the level."));
	return false;
}

FVector UMinimapWidget::ConvertMinimapUVToWorldLocation(const FVector2D& UVPosition) const
{
	if (!FogOfWarActor)
	{
		return FVector::ZeroVector;
	}
	// 坐标转换现在完全依赖于AFogOfWar的属性
	// 根据用户反馈：世界坐标系中 X 是“上”，Y 是“右”。
	// UI UV 坐标系中 U (X) 是“右”，V (Y) 是“下”。
	// 因此，需要将 UI 的 U 映射到世界的 Y，将 UI 的 V (反转后) 映射到世界的 X。
	const FVector2D WorldLocation2D = FogOfWarActor->GridBottomLeftWorldLocation + FVector2D(
		(0.5f - UVPosition.Y) * FogOfWarActor->GridSize.X, // UI V (反转后) -> World X
		UVPosition.X * FogOfWarActor->GridSize.Y           // UI U -> World Y
	);
	UE_LOG(LogMinimapWidget, Log, TEXT("Camera jump to: %s"), WorldLocation2D.ToString());
	return FVector(WorldLocation2D, 0.0f);
}



void UMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Continuous jump logic should always run if button is held
	if (bIsMinimapButtonHeld && GetOwningPlayer())
	{
		FVector2D MousePosition;
		GetOwningPlayer()->GetMousePosition(MousePosition.X, MousePosition.Y);
		JumpToMousePointOnMinimap(MousePosition, MyGeometry); // Use MyMimimapWidget's geometry)
	}

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval && UpdateInterval > 0.0f)
	{
		return;
	}

	TimeSinceLastUpdate = 0.0f;
	UpdateMinimapTexture();
}

// Helper function to jump camera to mouse point on minimap
void UMinimapWidget::JumpToMousePointOnMinimap(const FVector2D& ScreenPosition, const FGeometry& WidgetGeometry)
{
	// 将绝对屏幕坐标转换为此控件的局部坐标
	const FVector2D LocalPosition = WidgetGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = WidgetGeometry.GetLocalSize();

	// 确保点击在图片范围内
	// if (LocalPosition.X >= 0 && LocalPosition.Y >= 0 && LocalPosition.X <= LocalSize.X && LocalPosition.Y <= LocalSize.Y)
	{
		const FVector2D UV = LocalPosition / LocalSize;
		const FVector WorldLocation = ConvertMinimapUVToWorldLocation(UV);
		
		if (RTSCameraComponent)
		{
			RTSCameraComponent->JumpTo(WorldLocation);
		}
	}
}

FReply UMinimapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 只响应左键
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MinimapImage)
	{
		bIsDragging = true;
		LastMousePosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		
		// 返回Handled，表示我们已处理此事件，并捕获鼠标以备后续的OnMouseButtonUp和OnMouseMove
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UMinimapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;

		// 释放鼠标捕获并返回Handled
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
		bIsMinimapButtonHeld = true; // Set flag
		FVector2D MousePosition;
		GetOwningPlayer()->GetMousePosition(MousePosition.X, MousePosition.Y);
		JumpToMousePointOnMinimap(MousePosition, this->GetCachedGeometry()); // Use UMinimapWidget's geometry
	}
}

void UMinimapWidget::OnMinimapButtonReleased()
{
	if (MinimapButton && GetOwningPlayer())
	{
		bIsMinimapButtonHeld = false; // Clear flag
		FVector2D MousePosition;
		GetOwningPlayer()->GetMousePosition(MousePosition.X, MousePosition.Y);
		JumpToMousePointOnMinimap(MousePosition, this->GetCachedGeometry()); // Use UMinimapWidget's geometry
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
	// 注意：我们不再在这里设置笔刷
}
void UMinimapWidget::UpdateMinimapTexture()
{
	UE_LOG(LogMinimapWidget, Log, TEXT("UpdateMinimapTexture: Tick received, attempting update."));

	if (!MinimapDataSubsystem || !MinimapMaterialInstance || !MinimapRenderTarget)
	{
		UE_LOG(LogMinimapWidget, Error, TEXT("Update failed: A required component (Subsystem, MID, or RenderTarget) is null."));
		return;
	}

	// 1. 从子系统获取原始数据
	const TArray<FVector4>& VisionSourceData = MinimapDataSubsystem->VisionSources;
	const TArray<FVector4>& IconLocationData = MinimapDataSubsystem->IconLocations;
	const TArray<FLinearColor>& IconColorData = MinimapDataSubsystem->IconColors;
	UE_LOG(LogMinimapWidget, Log, TEXT("Data fetched: %d vision sources, %d icons."), VisionSourceData.Num(), IconLocationData.Num());

	// 2. 将数据打包到像素缓冲区中
	TArray<FLinearColor> VisionPixelData;
	VisionPixelData.Init(FLinearColor::Black, VisionDataTexture->GetSizeX());
	for(int32 i = 0; i < VisionSourceData.Num(); ++i)
	{
		if(VisionPixelData.IsValidIndex(i))
		{
			const FVector4& Data = VisionSourceData[i];
			VisionPixelData[i] = FLinearColor(Data.X, Data.Y, Data.Z, Data.W);
		}
	}

	TArray<FLinearColor> IconPixelData;
	IconPixelData.Init(FLinearColor::Black, IconDataTexture->GetSizeX());
	for(int32 i = 0; i < IconLocationData.Num(); ++i)
	{
		if(IconPixelData.IsValidIndex(i) && IconColorData.IsValidIndex(i))
		{
			const FVector4& LocData = IconLocationData[i];
			// const FLinearColor& ColorData = IconColorData[i]; // 颜色信息需要用另一个纹理或方法传递
			IconPixelData[i] = FLinearColor(LocData.X, LocData.Y, LocData.Z, LocData.W); 
		}
	}

	// 3. 更新数据纹理
	UpdateDataTexture(VisionDataTexture, VisionPixelData);
	UpdateDataTexture(IconDataTexture, IconPixelData);

	// 4. 将单位数量传递给材质
    MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumVisionSources"), VisionSourceData.Num());
    MinimapMaterialInstance->SetScalarParameterValue(TEXT("NumIcons"), IconLocationData.Num());

	// 5. 绘制
	UE_LOG(LogMinimapWidget, Log, TEXT("All checks passed. Drawing to Render Target..."));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, MinimapRenderTarget, MinimapMaterialInstance);

	// 6. 【新逻辑】在绘制完成后，再更新Image的笔刷
	if (MinimapImage)
	{
		FSlateBrush Brush = MinimapImage->GetBrush();
		Brush.SetResourceObject(MinimapRenderTarget);
		MinimapImage->SetBrush(Brush);
	}
}