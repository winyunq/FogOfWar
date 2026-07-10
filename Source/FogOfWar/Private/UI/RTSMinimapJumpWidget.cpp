// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/RTSMinimapJumpWidget.h"

#include "RTSCamera.h"
#include "Components/ActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Subsystems/MinimapDataSubsystem.h"

URTSMinimapJumpWidget::URTSMinimapJumpWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::Visible);
	ForceVolatile(true);
}

void URTSMinimapJumpWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeJumpWidget();
}

void URTSMinimapJumpWidget::InitializeJumpWidget()
{
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;
	}

	CachedJumpComponent = FindRTSCameraJumpComponent();
	BindRTSCameraFrustumUpdates();
}

void URTSMinimapJumpWidget::NativeDestruct()
{
	if (URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get()))
	{
		RTSCamera->onMinimapFrustumUpdated.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void URTSMinimapJumpWidget::BindRTSCameraFrustumUpdates()
{
	URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get());
	if (!RTSCamera)
	{
		return;
	}

	RTSCamera->onMinimapFrustumUpdated.RemoveAll(this);
	RTSCamera->onMinimapFrustumUpdated.AddUObject(this, &URTSMinimapJumpWidget::HandleMinimapFrustumUpdated);
	RTSCamera->updateMinimapFrustum();
}

void URTSMinimapJumpWidget::HandleMinimapFrustumUpdated()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 URTSMinimapJumpWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bDrawCameraFrustum || FrustumLineThickness <= 0.0f)
	{
		return MaxLayerId;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return MaxLayerId;
	}

	if (!CachedJumpComponent.IsValid())
	{
		URTSMinimapJumpWidget* MutableThis = const_cast<URTSMinimapJumpWidget*>(this);
		MutableThis->CachedJumpComponent = MutableThis->FindRTSCameraJumpComponent();
		MutableThis->BindRTSCameraFrustumUpdates();
	}

	const URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get());
	if (!RTSCamera)
	{
		return MaxLayerId;
	}

	TArray<FVector2D> Points;
	Points.Reserve(5);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector& WorldPoint = RTSCamera->minimapFrustumPoints[Index];
		Points.Add(ConvertWorldToWidgetLocal(FVector2D(WorldPoint.X, WorldPoint.Y), LocalSize));
	}
	const FVector2D FirstPoint = Points[0];
	Points.Add(FirstPoint);

	const int32 FrustumLayerId = MaxLayerId + 1;
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		FrustumLayerId,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		FrustumLineColor,
		true,
		FrustumLineThickness);

	return FrustumLayerId;
}

FVector2D URTSMinimapJumpWidget::ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const
{
	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	if (GetCurrentBounds(BoundsOrigin, BoundsExtent) &&
		BoundsExtent.X >= KINDA_SMALL_NUMBER &&
		BoundsExtent.Y >= KINDA_SMALL_NUMBER)
	{
		float NormalizedX = (WorldPos.X - (BoundsOrigin.X - BoundsExtent.X)) / (2.0f * BoundsExtent.X);
		float NormalizedY = (WorldPos.Y - (BoundsOrigin.Y - BoundsExtent.Y)) / (2.0f * BoundsExtent.Y);
		NormalizedX = FMath::Clamp(NormalizedX, 0.0f, 1.0f);
		NormalizedY = FMath::Clamp(NormalizedY, 0.0f, 1.0f);

		return FVector2D(NormalizedY * WidgetSize.X, (1.0f - NormalizedX) * WidgetSize.Y);
	}

	const UMinimapDataSubsystem* Subsystem = UMinimapDataSubsystem::Get();
	if (!Subsystem || Subsystem->GridSize.X <= 0.0f || Subsystem->GridSize.Y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D& Origin = Subsystem->GridBottomLeftWorldLocation;
	const FVector2D& Size = Subsystem->GridSize;
	const float U = FMath::Clamp((WorldPos.Y - Origin.Y) / Size.Y, 0.0f, 1.0f);
	const float V = FMath::Clamp(1.0f - (WorldPos.X - Origin.X) / Size.X, 0.0f, 1.0f);
	return FVector2D(U * WidgetSize.X, V * WidgetSize.Y);
}

FVector2D URTSMinimapJumpWidget::ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const
{
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	if (GetCurrentBounds(BoundsOrigin, BoundsExtent) &&
		BoundsExtent.X >= KINDA_SMALL_NUMBER &&
		BoundsExtent.Y >= KINDA_SMALL_NUMBER)
	{
		const float UParam = FMath::Clamp(LocalPos.X / WidgetSize.X, 0.0f, 1.0f);
		const float VParam = FMath::Clamp(LocalPos.Y / WidgetSize.Y, 0.0f, 1.0f);
		const float NormalizedX = 1.0f - VParam;
		const float NormalizedY = UParam;

		return FVector2D(
			(BoundsOrigin.X - BoundsExtent.X) + NormalizedX * (2.0f * BoundsExtent.X),
			(BoundsOrigin.Y - BoundsExtent.Y) + NormalizedY * (2.0f * BoundsExtent.Y));
	}

	const UMinimapDataSubsystem* Subsystem = UMinimapDataSubsystem::Get();
	if (!Subsystem || Subsystem->GridSize.X <= 0.0f || Subsystem->GridSize.Y <= 0.0f ||
		WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D& Origin = Subsystem->GridBottomLeftWorldLocation;
	const FVector2D& Size = Subsystem->GridSize;
	const float U = FMath::Clamp(LocalPos.X / WidgetSize.X, 0.0f, 1.0f);
	const float V = FMath::Clamp(LocalPos.Y / WidgetSize.Y, 0.0f, 1.0f);
	return FVector2D(
		Origin.X + (1.0f - V) * Size.X,
		Origin.Y + U * Size.Y);
}

bool URTSMinimapJumpWidget::GetCurrentBounds(FVector& OutOrigin, FVector& OutExtent) const
{
	if (!CachedJumpComponent.IsValid())
	{
		URTSMinimapJumpWidget* MutableThis = const_cast<URTSMinimapJumpWidget*>(this);
		MutableThis->CachedJumpComponent = MutableThis->FindRTSCameraJumpComponent();
		if (MutableThis->CachedJumpComponent.IsValid())
		{
			MutableThis->BindRTSCameraFrustumUpdates();
		}
	}

	const URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get());
	return RTSCamera ? RTSCamera->getResolvedMovementBounds(OutOrigin, OutExtent) : false;
}

UActorComponent* URTSMinimapJumpWidget::FindRTSCameraJumpComponent() const
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return nullptr;
	}

	auto FindOnActor = [this](AActor* Actor) -> UActorComponent*
	{
		return Actor ? Actor->FindComponentByClass<URTSCamera>() : nullptr;
	};

	if (UActorComponent* Component = FindOnActor(PlayerController->GetViewTarget()))
	{
		return Component;
	}

	return FindOnActor(PlayerController->GetPawn());
}

bool URTSMinimapJumpWidget::TryJumpToWorldLocation(const FVector& WorldLocation)
{
	if (!bAutoJumpToRTSCamera)
	{
		return false;
	}

	UActorComponent* JumpComponent = CachedJumpComponent.Get();
	URTSCamera* RTSCamera = Cast<URTSCamera>(JumpComponent);
	if (!RTSCamera)
	{
		JumpComponent = FindRTSCameraJumpComponent();
		CachedJumpComponent = JumpComponent;
		RTSCamera = Cast<URTSCamera>(JumpComponent);
		if (RTSCamera)
		{
			BindRTSCameraFrustumUpdates();
		}
	}

	if (!RTSCamera)
	{
		return false;
	}

	RTSCamera->jumpTo(WorldLocation);
	return true;
}

void URTSMinimapJumpWidget::RequestWorldLocation(const FVector2D& WorldPos)
{
	const FVector WorldLocation(WorldPos, 0.0f);
	OnWorldLocationRequested.Broadcast(WorldLocation);
	TryJumpToWorldLocation(WorldLocation);
}

FReply URTSMinimapJumpWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		RequestWorldLocation(WorldPos);
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply URTSMinimapJumpWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply URTSMinimapJumpWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging && HasMouseCapture())
	{
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		RequestWorldLocation(WorldPos);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
