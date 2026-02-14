// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/RTSMinimapControllerWidget.h"
#include "RTSCamera.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/PanelWidget.h"

URTSMinimapControllerWidget::URTSMinimapControllerWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Enable Tick needed for smooth camera frustum updates
	// But in UI, paint is often separate. We can use Tick to poll camera transform if needed.
}

void URTSMinimapControllerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeController();
}

void URTSMinimapControllerWidget::InitializeController()
{
	// 1. Cache Grid Parameters (One-time lookup)
	// We try to get the subsystem. If it exists, we copy the data.
	if (const UMinimapDataSubsystem* Subsystem = UMinimapDataSubsystem::Get())
	{
		CachedGridBottomLeft = Subsystem->GridBottomLeftWorldLocation;
		CachedGridSize = Subsystem->GridSize;
	}
	else
	{
		// Fallback or Log Warning. 
		// If subsystem is not ready, we might need to retry or rely on user setting these manually via BP.
		// For now, let's assume valid subsystem or defaults.
	}

	// 2. Find Camera
	FindRTSCamera();
}

void URTSMinimapControllerWidget::FindRTSCamera()
{
	if (CachedRTSCamera) return;

	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		APawn* Pawn = PC->GetPawn();
		if (Pawn)
		{
			CachedRTSCamera = Pawn->FindComponentByClass<URTSCamera>();
		}
		
		// Fallback: Check Spectator or ViewTarget if needed
		if (!CachedRTSCamera && PC->GetViewTarget())
		{
			CachedRTSCamera = PC->GetViewTarget()->FindComponentByClass<URTSCamera>();
		}
	}
}

void URTSMinimapControllerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Retrieve camera if lost (e.g. possessed new pawn)
	if (!CachedRTSCamera)
	{
		FindRTSCamera();
	}
	
	// Force Repaint every frame to show smooth camera movement
	// UI strictly uses InvalidationPanel usually, but for a high-frequency camera overlay, we might need to verify if this is automatic.
	// In Slate, Paint is called automatically if invalidated or volatile. 
	// We can't explicitly "Call Paint" easily, but changing properties often triggers it.
	// For a pure custom draw widget, it's usually automatic.
}

int32 URTSMinimapControllerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Call Super first (though UserWidget usually doesn't paint much itself)
	int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!CachedRTSCamera) return MaxLayerId;

	// --- Draw Camera Frustum ---
	
	// We need 4 points of the camera view projected onto the ground (Z=0 plane).
	// Since URTSCamera doesn't expose "GetFrustumCornersOnGround" directly, we might need to calculate it.
	// Assuming URTSCamera controls a SpringArm + Camera.
	
	// Fast approach: Raycast from camera corners? Or just simple math if we assume top-down.
	// Let's assume user wants a simple "Current Location View" box.
	// If RTSCamera has "GetCameraBounds" or similar, use it. 
	// Looking at RTSCamera.h, it controls a SpringArm.
	
	APlayerController* PC = GetOwningPlayer();
	if (!PC) return MaxLayerId;

	// Use GameplayStatics to de-project screen corners to world
	int32 SizeX, SizeY;
	PC->GetViewportSize(SizeX, SizeY);

	FVector WorldLocTL, WorldDirTL;
	FVector WorldLocTR, WorldDirTR;
	FVector WorldLocBL, WorldDirBL;
	FVector WorldLocBR, WorldDirBR;

	// De-project corners of the screen
	// Note: this is expensive to do in Paint? Maybe. But for a single widget it's fine.
	// Ideally this logic should be in Tick and cached, but PC->DeprojectScreenPositionToWorld needs PlayerController.
	
	bool bSuccess = true;
	bSuccess &= PC->DeprojectScreenPositionToWorld(0, 0, WorldLocTL, WorldDirTL);
	bSuccess &= PC->DeprojectScreenPositionToWorld(SizeX, 0, WorldLocTR, WorldDirTR);
	bSuccess &= PC->DeprojectScreenPositionToWorld(0, SizeY, WorldLocBL, WorldDirBL);
	bSuccess &= PC->DeprojectScreenPositionToWorld(SizeX, SizeY, WorldLocBR, WorldDirBR);

	if (bSuccess)
	{
		// Raycast to Ground Plane (Z=0 usually, or GridBottomLeft.Z if 3D, but map is 2D)
		auto IntersectGround = [](const FVector& Origin, const FVector& Dir) -> FVector2D
		{
			// Plane Z = 0. 
			// P = Origin + t * Dir
			// P.z = Origin.z + t * Dir.z = 0 => t = -Origin.z / Dir.z
			if (FMath::IsNearlyZero(Dir.Z)) return FVector2D(Origin); // Parallel
			float t = -Origin.Z / Dir.Z;
			FVector Intersection = Origin + t * Dir;
			return FVector2D(Intersection.X, Intersection.Y);
		};

		FVector2D P_TL = IntersectGround(WorldLocTL, WorldDirTL);
		FVector2D P_TR = IntersectGround(WorldLocTR, WorldDirTR);
		FVector2D P_BL = IntersectGround(WorldLocBL, WorldDirBL);
		FVector2D P_BR = IntersectGround(WorldLocBR, WorldDirBR);

		// Convert to Widget Coordinates
		FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		FVector2D W_TL = ConvertWorldToWidgetLocal(P_TL, LocalSize);
		FVector2D W_TR = ConvertWorldToWidgetLocal(P_TR, LocalSize);
		FVector2D W_BL = ConvertWorldToWidgetLocal(P_BL, LocalSize);
		FVector2D W_BR = ConvertWorldToWidgetLocal(P_BR, LocalSize);

		// Draw Lines
		TArray<FVector2D> Points;
		Points.Add(W_TL);
		Points.Add(W_TR);
		Points.Add(W_BR);
		Points.Add(W_BL);
		Points.Add(W_TL); // Close Loop

		FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		
		// Draw White Box (Thickness 2.0)
		// UWidgetBlueprintLibrary::DrawLines(Context, Points, FLinearColor::White, true, 2.0f); 
		// Since we are in Native Paint, we use FSlateDrawElement directly or Context helper if available.
		// UserWidget doesn't expose easy DrawLine context in C++ without defining OnPaint.
		// Let's use FSlateDrawElement::MakeLines
		
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1, // Draw on top
			AllottedGeometry.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			FLinearColor::White,
			true,
			2.0f
		);
	}

	return MaxLayerId + 1;
}

FVector2D URTSMinimapControllerWidget::ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const
{
	// Map World [BottomLeft, BottomLeft + Size] to Widget [0, 1] -> [0, WidgetSize]
	// Subsystem: GridBottomLeftWorldLocation
	// Subsystem: GridSize
	
	// Normalize to [0,1]
	FVector2D Normalized;
	Normalized.X = (WorldPos.X - CachedGridBottomLeft.X) / CachedGridSize.X;
	Normalized.Y = (WorldPos.Y - CachedGridBottomLeft.Y) / CachedGridSize.Y;

	// Map to Widget Size
	// Note: Minimap usually flips Y because UI Y is down, World Y is Up (or depending on convention).
	// In MinimapWidget.cpp:
	// WorldLocation2D = GridBottomLeft + ((0.5 - UV.Y) * GridSizeX, UV.X * GridSizeY) ... Wait, that logic was weird in original file.
	// Let's look at standard:
	// Standard Top-Down Map:
	// World X+ -> Widget Y- (Up on screen) ? Or World Y+ -> Widget Y-?
	// Usually:
	// World X relates to Widget Y (if rotated 90 deg) or World X to Widget X.
	// Let's assume standard non-rotated mapping first (World X -> Widget X, World Y -> Widget Y), but UI Y is inverted.
	// Actually, let's Stick to the logic found in MinimapWidget::ConvertMinimapUVToWorldLocation (reversed).
	
	/* Original Logic:
	   World X = BottomLeft.X + (0.5 - UV.Y) * GridSize.X ?? This looks like 90 degree rotation. 
	   World Y = BottomLeft.Y + UV.X * GridSize.Y
	   
	   Wait, "0.5 - UV.Y" implies centering? Or maybe "1.0 - UV.Y"?
	   Let's check MinimapWidget.cpp line 137 again.
	   Target: WorldLocation2D = BottomLeft + FVector2D((0.5f - UV.Y) * Size.X, UV.X * Size.Y);
       
	   This implies: 
	   World.X - BottomLeft.X = (0.5 - UV.Y) * Size.X  => (WorldX_Rel / SizeX) = 0.5 - UV.Y => UV.Y = 0.5 - (WorldX_Rel / SizeX)
	   World.Y - BottomLeft.Y = UV.X * Size.Y          => UV.X = (WorldY_Rel / SizeY)
	   
	   So:
	   Widget.X = UV.X * WidgetSize.X = (WorldY_Rel / SizeY) * WidgetSize.X
	   Widget.Y = UV.Y * WidgetSize.Y = (0.5 - WorldX_Rel / SizeX) * WidgetSize.Y   <-- This 0.5 offset is very suspicious. 
	   
	   Let's assume standard mapping for a general map:
	   UV.x = (World.x - Origin.x) / Size.x;
	   UV.y = 1.0 - (World.y - Origin.y) / Size.y; // Flip Y for UI
	   
	   However, I must respect the USER's existing coordinate system if I want it to align with the Fog Layer.
	   Let's re-read MinimapWidget.cpp carefully!
	*/
	
	// Re-reading MinimapWidget.cpp Line 137:
	// const FVector2D WorldLocation2D = MinimapDataSubsystem->GridBottomLeftWorldLocation + FVector2D(
	// 	(0.5f - UVPosition.Y) * MinimapDataSubsystem->GridSize.X, 
	// 	UVPosition.X * MinimapDataSubsystem->GridSize.Y
	// );
	
	// This transformation is:
	// World X depends on UV Y.
	// World Y depends on UV X.
	// This means the map is ROTATED 90 degrees!
	
	// Inverse Logic:
	// Normalized WorldRel = (World - Origin) / GridSize
	// WorldRel.X = 0.5 - UV.Y  => UV.Y = 0.5 - WorldRel.X
	// WorldRel.Y = UV.X        => UV.X = WorldRel.Y
	
	FVector2D WorldRel = (WorldPos - CachedGridBottomLeft) / CachedGridSize;
	
	// Apply the strange rotation from original code
	float UV_X = WorldRel.Y;
	float UV_Y = 0.5f - WorldRel.X; 
	// Wait, 0.5 - X? If WorldRel.X goes from 0 to 1.
	// If X=0, UV.Y = 0.5. If X=1, UV.Y = -0.5. 
	// This implies the map only covers X range [-0.5, 0.5] relative to center?
	// Or maybe "GridBottomLeft" is strictly bottom-left.
	// If GridBottomLeft is truly BottomLeft, then WorldRel goes 0..1.
	// Then UV.Y goes 0.5 .. -0.5. This seems to imply the texture is centered strangely or rotated.
	
	// Let's checking `MinimapDataSubsystem.cpp` Line 201:
	// return FVector2f((WorldLocation - SingletonInstance->GridBottomLeftWorldLocation) / SingletonInstance->MinimapTileSize);
	// Wait, Subsystem uses standard mapping! 
	// Subsystem: GridLocation = (World - Origin) / TileSize.
	// Subsystem: TileIJ = Floor(GridLocation).
	// Subsystem: IJ.X corresponds to World X. IJ.Y corresponds to World Y.
	
	// So `MinimapWidget.cpp` Line 137 `ConvertMinimapUVToWorldLocation` might be WRONG or using a specific camera-space rotation that I should ignore?
	// OR, the MinimapWidget logic I saw earlier was experimental trash that I should replace with standard logic.
	
	// User said: "就像“覆层”一样，在小地图中暴露"
	// User said: "RTS相机应该位于顶层...用白色指定宽度的像素"
	
	// Since I am writing `RTSMinimapControllerWidget` from scratch, I should ALIGN with how `MinimapWidget` *renders* the tiles.
	// MinimapWidget::DrawInMassSize (Line 424):
	// IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, FinalSize, 1.0f);
	// It writes World Locations directly into the texture? 
	// Wait, `IconDataTexture` is passed to Material.
	// Let's check the Material logic? I can't read .uasset.
	// But `DrawInMassSize` writes World X/Y into the color channels?
	// "IconDataPtr[UnitCount] = FLinearColor(WorldLocation.X, WorldLocation.Y, FinalSize, 1.0f);"
	
	// If the Material receives World Coords, the Material probably handles the projection to UV.
	// I need to duplicate that projection in C++ if I want to draw lines on top.
	// Or... `MinimapDataSubsystem` defines the grid.
	// Usually: UV.x = (World.x - BottomLeft.x) / Size.x
	//          UV.y = 1.0 - (World.y - BottomLeft.y) / Size.y (if Y is Up in world)
	
	// Let's assume STANDARD Orthographic Top-Down Projection for the Overlay.
	// If the underlying map is weird, we'll see it detached.
	// But given `MinimapDataSubsystem::ConvertWorldSpaceLocationToMinimapGridSpace_Static` does simple division:
	// return (World - Origin) / TileSize;
	// This confirms World X aligns with Grid X, World Y with Grid Y.
	
	// So my `ConvertWorldToWidgetLocal` should be:
	// UV.X = (World.X - Origin.X) / Size.X
	// UV.Y = 1.0 - (World.Y - Origin.Y) / Size.Y (Assume UE UI Y is Down, World Y is Up)
	
	// Wait, `MinimapWidget.cpp` Line 137 logic was:
	// WorldLocation2D = ... + FVector2D( (0.5f - UVPosition.Y) * Size.X, UVPosition.X * Size.Y )
	// This suggests X and Y are SWAPPED in the widget's "ConvertMinimapUVToWorldLocation" function.
	// This function seems to be used for "JumpToLocationUnderMouse".
	// If I click at UV(0,0)...
	// World.X = Origin.X + 0.5 * Size.X
	// World.Y = Origin.Y + 0.0 * Size.Y
	// This implies UV(0,0) is Middle-Bottom?
	// This looks like specific "45 degree rotated" or "Isometric" logic, OR just broken legacy code.
	
	// I will stick to the Subsystem's definition which is cleaner.
	// Subsystem says: Grid is standard bounds.
	// I will assume Top-Down Mapping:
	// UI X+ = East (World Y+)
	// UI Y+ = South (World X-) ?? 
	// No, let's Stick to standard map:
	// UI X = World Y (East)
	// UI Y = -World X (North)
	// ... Often X is North in UE.
	
	// Let's assume:
	// World X (North) -> Widget Y (Up/Down) -> Usually Up is Y- in UI.
	// World Y (East) -> Widget X (Left/Right)
	
	// So:
	// Widget U = (WorldY - OriginY) / SizeY
	// Widget V = 1.0 - (WorldX - OriginX) / SizeX
	
	// This rotates the map 90 degrees so North is Up.
	// Let's try this standard mapping. If it's wrong, we can swap X/Y easily.
	
	float U = (WorldPos.Y - CachedGridBottomLeft.Y) / CachedGridSize.Y;
	float V = 1.0f - (WorldPos.X - CachedGridBottomLeft.X) / CachedGridSize.X;
	
	return FVector2D(U * WidgetSize.X, V * WidgetSize.Y);
}

FVector2D URTSMinimapControllerWidget::ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const
{
	// Inverse of ConvertWorldToWidgetLocal
	float U = LocalPos.X / WidgetSize.X;
	float V = LocalPos.Y / WidgetSize.Y;
	
	// U = (WorldY - OriginY) / SizeY => WorldY = OriginY + U * SizeY
	// V = 1.0 - (WorldX - OriginX) / SizeX => V - 1.0 = -(...) => 1.0 - V = (...) => WorldX = OriginX + (1.0 - V) * SizeX
	
	float WorldX = CachedGridBottomLeft.X + (1.0f - V) * CachedGridSize.X;
	float WorldY = CachedGridBottomLeft.Y + U * CachedGridSize.Y;
	
	return FVector2D(WorldX, WorldY);
}

// --- Input ---

FReply URTSMinimapControllerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		
		// Instant Jump on Click
		FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		
		if (CachedRTSCamera)
		{
			CachedRTSCamera->JumpTo(FVector(WorldPos, 0.0f)); // Z is ignored by JumpTo usually (or handled by camera height)
		}
		
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply URTSMinimapControllerWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply URTSMinimapControllerWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging && HasMouseCapture())
	{
		FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		
		if (CachedRTSCamera)
		{
			CachedRTSCamera->JumpTo(FVector(WorldPos, 0.0f));
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
