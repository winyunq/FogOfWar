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

void UMinimapAddProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapAddProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapAddProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	const FIntPoint MinimapResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, MinimapResolution](FMassExecutionContext& Context)
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

void UMinimapRemoveProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapRemoveProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapRemoveProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, GridResolution](FMassExecutionContext& Context)
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
//  UMinimapUpdateProcessor
//----------------------------------------------------------------------//
UMinimapUpdateProcessor::UMinimapUpdateProcessor()
	: EntityQuery(*this)
{
	ObservedType = FTransformFragment::StaticStruct();
	Operation = EMassObservedOperation::MAX;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapUpdateProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapUpdateProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	const FIntPoint MinimapResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, MinimapResolution](FMassExecutionContext& Context)
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

			const FIntPoint CurrentMinimapTileIJ = MinimapDataSubsystem->ConvertWorldLocationToMinimapTileIJ(FVector2D(WorldLocation));

			if (CurrentMinimapTileIJ != PrevCellFragment.PrevCellCoords)
			{
				// Decrement old tile
				const FIntPoint& PrevCellCoords = PrevCellFragment.PrevCellCoords;
				if (PrevCellCoords.X >= 0 && PrevCellCoords.Y >= 0 && PrevCellCoords.X < MinimapResolution.X && PrevCellCoords.Y < MinimapResolution.Y)
				{
					const int32 OldIndex = PrevCellCoords.X * MinimapResolution.Y + PrevCellCoords.Y;
					FMinimapTile& OldTile = MinimapDataSubsystem->MinimapTiles[OldIndex];
					OldTile.UnitCount--;
					if (OldTile.UnitCount <= 0)
					{
						OldTile.Color = FLinearColor::Black;
						OldTile.UnitCount = 0;
						OldTile.MaxSightRadius = 0.0f;
						OldTile.MaxIconSize = 0.0f;
					}
				}

				// Increment new tile
				if (CurrentMinimapTileIJ.X >= 0 && CurrentMinimapTileIJ.Y >= 0 && CurrentMinimapTileIJ.X < MinimapResolution.X && CurrentMinimapTileIJ.Y < MinimapResolution.Y)
				{
					const int32 NewIndex = CurrentMinimapTileIJ.X * MinimapResolution.Y + CurrentMinimapTileIJ.Y;
					FMinimapTile& NewTile = MinimapDataSubsystem->MinimapTiles[NewIndex];
					NewTile.UnitCount++;
					NewTile.Color = RepFragment.IconColor;
					NewTile.MaxSightRadius = FMath::Max(NewTile.MaxSightRadius, VisionFragment.SightRadius);
					NewTile.MaxIconSize = FMath::Max(NewTile.MaxIconSize, RepFragment.IconSize);
				}

				// Update the previous location fragment
				PrevCellFragment.PrevCellCoords = CurrentMinimapTileIJ;
			}
		}
	});
}