// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassMinimapProcessors.h"
#include "MassFogOfWarFragments.h"
#include "Subsystems/MinimapDataSubsystem.h"
#include "FogOfWar.h"
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
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
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
	if (!MinimapDataSubsystem || !FogOfWarActor)
	{
		return;
	}

	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, GridResolution](FMassExecutionContext& Context)
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

			const FIntVector2 TileIJ = FogOfWarActor->ConvertWorldLocationToTileIJ(FVector2D(WorldLocation));

			if (TileIJ.X >= 0 && TileIJ.Y >= 0 && TileIJ.X < GridResolution.X && TileIJ.Y < GridResolution.Y)
			{
				const int32 Index = TileIJ.X * GridResolution.Y + TileIJ.Y;
				FMinimapTile& Tile = MinimapDataSubsystem->MinimapTiles[Index];
				
				Tile.UnitCount++;
				Tile.Color = RepFragment.IconColor;
				Tile.MaxSightRadius = FMath::Max(Tile.MaxSightRadius, VisionFragment.SightRadius);
				
				PrevCellFragment.PrevCellCoords = FIntPoint(TileIJ.X, TileIJ.Y);
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
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
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
	if (!MinimapDataSubsystem || !FogOfWarActor)
	{
		return;
	}

	const FIntPoint GridResolution = MinimapDataSubsystem->GridResolution;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, GridResolution](FMassExecutionContext& Context)
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

			const FIntVector2 CurrentTileIJ = FogOfWarActor->ConvertWorldLocationToTileIJ(FVector2D(WorldLocation));

			if (CurrentTileIJ != FIntVector2(PrevCellFragment.PrevCellCoords.X, PrevCellFragment.PrevCellCoords.Y))
			{
				// Decrement old tile
				const FIntPoint& PrevCellCoords = PrevCellFragment.PrevCellCoords;
				if (PrevCellCoords.X >= 0 && PrevCellCoords.Y >= 0 && PrevCellCoords.X < GridResolution.X && PrevCellCoords.Y < GridResolution.Y)
				{
					const int32 OldIndex = PrevCellCoords.X * GridResolution.Y + PrevCellCoords.Y;
					FMinimapTile& OldTile = MinimapDataSubsystem->MinimapTiles[OldIndex];
					OldTile.UnitCount--;
					if (OldTile.UnitCount <= 0)
					{
						OldTile.Color = FLinearColor::Black;
						OldTile.UnitCount = 0;
						OldTile.MaxSightRadius = 0.0f;
					}
				}

				// Increment new tile
				if (CurrentTileIJ.X >= 0 && CurrentTileIJ.Y >= 0 && CurrentTileIJ.X < GridResolution.X && CurrentTileIJ.Y < GridResolution.Y)
				{
					const int32 NewIndex = CurrentTileIJ.X * GridResolution.Y + CurrentTileIJ.Y;
					FMinimapTile& NewTile = MinimapDataSubsystem->MinimapTiles[NewIndex];
					NewTile.UnitCount++;
					NewTile.Color = RepFragment.IconColor;
					NewTile.MaxSightRadius = FMath::Max(NewTile.MaxSightRadius, VisionFragment.SightRadius);
				}

				// Update the previous location fragment
				PrevCellFragment.PrevCellCoords = FIntPoint(CurrentTileIJ.X, CurrentTileIJ.Y);
			}
		}
	});
}
