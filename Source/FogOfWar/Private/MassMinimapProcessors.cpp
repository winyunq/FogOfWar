// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassMinimapProcessors.h"
#include "MassFogOfWarFragments.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "MassCommonFragments.h"
#include "Kismet/GameplayStatics.h"

//----------------------------------------------------------------------//
//  UMinimapAddProcessor
//----------------------------------------------------------------------//
UMinimapAddProcessor::UMinimapAddProcessor()
	: EntityQuery(*this)
{
	ObservedType = FMassMinimapRepresentationFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapAddProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
}

void UMinimapAddProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	}
	if (!MinimapDataSubsystem)
	{
		return;
	}

	const FIntPoint MinimapResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(Context, [this, MinimapResolution](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassMinimapRepresentationFragment> RepList = Context.GetFragmentView<FMassMinimapRepresentationFragment>();
		const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();
		const TArrayView<FMassPreviousMinimapCellFragment> PrevCellList = Context.GetMutableFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FVector& WorldLocation = LocationList[i].GetTransform().GetLocation();
			const FMassMinimapRepresentationFragment& RepFragment = RepList[i];
			const FMassVisionFragment& VisionFragment = VisionList[i];
			FMassPreviousMinimapCellFragment& PrevCellFragment = PrevCellList[i];

			const FIntPoint MinimapTileIJ = MinimapDataSubsystem->ConvertWorldLocationToMinimapTileIJ(FVector2D(WorldLocation));

			if (MinimapTileIJ.X >= 0 && MinimapTileIJ.Y >= 0 && MinimapTileIJ.X < MinimapResolution.X && MinimapTileIJ.Y < MinimapResolution.Y)
			{
				const int32 Index = MinimapTileIJ.X * MinimapResolution.Y + MinimapTileIJ.Y;
				FMinimapTile& Tile = MinimapDataSubsystem->MinimapTiles[Index];
				
				Tile.UnitCount++;
				Tile.Color = RepFragment.IconColor;
				Tile.MaxSightRadius = FMath::Max(Tile.MaxSightRadius, VisionFragment.SightRadius);
				Tile.MaxIconSize = FMath::Max(Tile.MaxIconSize, RepFragment.IconSize);
				
				PrevCellFragment.PrevCellCoords = MinimapTileIJ;
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMinimapRemoveProcessor
//----------------------------------------------------------------------//
UMinimapRemoveProcessor::UMinimapRemoveProcessor()
	: EntityQuery(*this)
{
	ObservedType = FMassMinimapRepresentationFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapRemoveProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadOnly);
}

void UMinimapRemoveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	}
	if (!MinimapDataSubsystem)
	{
		return;
	}

	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(Context, [this, GridResolution](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassPreviousMinimapCellFragment> PrevCellList = Context.GetFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FIntPoint& PrevCellCoords = PrevCellList[i].PrevCellCoords;

			if (PrevCellCoords.X >= 0 && PrevCellCoords.Y >= 0 && PrevCellCoords.X < GridResolution.X && PrevCellCoords.Y < GridResolution.Y)
			{
				const int32 Index = PrevCellCoords.X * GridResolution.Y + PrevCellCoords.Y;
				FMinimapTile& Tile = MinimapDataSubsystem->MinimapTiles[Index];

				Tile.UnitCount--;
				if (Tile.UnitCount <= 0)
				{
					Tile.Color = FLinearColor::Black;
					Tile.UnitCount = 0; // Clamp at 0
					Tile.MaxSightRadius = 0.0f;
					Tile.MaxIconSize = 0.0f;
				}
			}
		}
	});
}


//----------------------------------------------------------------------//
//  UMinimapUpdateProcessor (Refactored with Logging)
//----------------------------------------------------------------------//
UMinimapUpdateProcessor::UMinimapUpdateProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapUpdateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMinimapCellChangedTag>(EMassFragmentPresence::All);
}

void UMinimapUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	}
	if (!MinimapDataSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UMinimapUpdateProcessor: MinimapDataSubsystem is null."));
		return;
	}

	const FIntPoint MinimapResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(Context, [this, MinimapResolution](FMassExecutionContext& Context)
	{
		const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FMassMinimapRepresentationFragment> RepList = Context.GetFragmentView<FMassMinimapRepresentationFragment>();
		const TConstArrayView<FMassVisionFragment> VisionList = Context.GetFragmentView<FMassVisionFragment>();
		const TArrayView<FMassPreviousMinimapCellFragment> PrevCellList = Context.GetMutableFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			const FVector& WorldLocation = LocationList[i].GetTransform().GetLocation();
			FMassPreviousMinimapCellFragment& PrevCellFragment = PrevCellList[i];
			
			const FIntPoint PrevCellCoords = PrevCellFragment.PrevCellCoords;
			const FIntPoint CurrentMinimapTileIJ = MinimapDataSubsystem->ConvertWorldLocationToMinimapTileIJ(FVector2D(WorldLocation));

			UE_LOG(LogTemp, Log, TEXT("Entity [%d]: WorldLoc=(%.1f, %.1f), PrevCell=(%d, %d), CurrentCell=(%d, %d)"),
				Entity.Index, WorldLocation.X, WorldLocation.Y, PrevCellCoords.X, PrevCellCoords.Y, CurrentMinimapTileIJ.X, CurrentMinimapTileIJ.Y);

			if (CurrentMinimapTileIJ != PrevCellCoords)
			{
				UE_LOG(LogTemp, Warning, TEXT("Entity [%d]: CELL CHANGED!"), Entity.Index);

				// Decrement old tile
				if (PrevCellCoords.X != INT_MIN) // Don't decrement on the first run
				{
					if (PrevCellCoords.X >= 0 && PrevCellCoords.Y >= 0 && PrevCellCoords.X < MinimapResolution.X && PrevCellCoords.Y < MinimapResolution.Y)
					{
						const int32 OldIndex = PrevCellCoords.X * MinimapResolution.Y + PrevCellCoords.Y;
						FMinimapTile& OldTile = MinimapDataSubsystem->MinimapTiles[OldIndex];
						const int32 OldUnitCount = OldTile.UnitCount;
						OldTile.UnitCount--;
						UE_LOG(LogTemp, Log, TEXT("  > Decremented Old Tile (%d, %d), Index: %d, Count: %d -> %d"), PrevCellCoords.X, PrevCellCoords.Y, OldIndex, OldUnitCount, OldTile.UnitCount);

						if (OldTile.UnitCount <= 0)
						{
							OldTile.Color = FLinearColor::Black;
							OldTile.UnitCount = 0;
							OldTile.MaxSightRadius = 0.0f;
							OldTile.MaxIconSize = 0.0f;
							UE_LOG(LogTemp, Log, TEXT("  > Old Tile is now empty."));
						}
					}
				}

				// Increment new tile
				if (CurrentMinimapTileIJ.X >= 0 && CurrentMinimapTileIJ.Y >= 0 && CurrentMinimapTileIJ.X < MinimapResolution.X && CurrentMinimapTileIJ.Y < MinimapResolution.Y)
				{
					const int32 NewIndex = CurrentMinimapTileIJ.X * MinimapResolution.Y + CurrentMinimapTileIJ.Y;
					FMinimapTile& NewTile = MinimapDataSubsystem->MinimapTiles[NewIndex];
					const int32 OldUnitCount = NewTile.UnitCount;
					NewTile.UnitCount++;
					const FMassMinimapRepresentationFragment& RepFragment = RepList[i];
					const FMassVisionFragment& VisionFragment = VisionList[i];
					NewTile.Color = RepFragment.IconColor;
					NewTile.MaxSightRadius = FMath::Max(NewTile.MaxSightRadius, VisionFragment.SightRadius);
					NewTile.MaxIconSize = FMath::Max(NewTile.MaxIconSize, RepFragment.IconSize);
					UE_LOG(LogTemp, Log, TEXT("  > Incremented New Tile (%d, %d), Index: %d, Count: %d -> %d"), CurrentMinimapTileIJ.X, CurrentMinimapTileIJ.Y, NewIndex, OldUnitCount, NewTile.UnitCount);
				}

				// Update the previous location fragment
				UE_LOG(LogTemp, Log, TEXT("  > Updating PrevCell from (%d, %d) to (%d, %d)"), PrevCellCoords.X, PrevCellCoords.Y, CurrentMinimapTileIJ.X, CurrentMinimapTileIJ.Y);
				PrevCellFragment.PrevCellCoords = CurrentMinimapTileIJ;
			}
			else
			{
				UE_LOG(LogTemp, Verbose, TEXT("Entity [%d]: Cell NOT changed. Skipping update."), Entity.Index);
			}
		}

		// Consume the tag now that the update is processed.
		const TArrayView<const FMassEntityHandle> Entities = Context.GetEntities();
		for (const FMassEntityHandle& Entity : Entities)
		{
			Context.Defer().RemoveTag<FMinimapCellChangedTag>(Entity);
		}
	});
}