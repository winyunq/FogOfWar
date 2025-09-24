// Copyright Winyunq, 2025. All Rights Reserved.

#include "MassMinimapProcessors.h"
#include "MassFogOfWarFragments.h"
#include "MassCommonFragments.h"
#include "FogOfWar.h"
#include "MassExecutionContext.h"
#include "Kismet/GameplayStatics.h"

//----------------------------------------------------------------------//
//  UMinimapAddProcessor
//----------------------------------------------------------------------//
UMinimapAddProcessor::UMinimapAddProcessor()
{
	ObservedType = FMassMinimapRepresentationFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapAddProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapAddProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMinimapDataSubsystem* MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
	if (!MinimapDataSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [MinimapDataSubsystem](FMassExecutionContext& Context)
	{
		const auto& LocationList = Context.GetFragmentView<FTransformFragment>();
		const auto& RepList = Context.GetFragmentView<FMassMinimapRepresentationFragment>();
		
		for (int32 i = 0; i < Context.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = Context.GetEntity(i);
			const FVector& Location = LocationList[i].GetTransform().GetLocation();
			const auto& RepFragment = RepList[i];

			MinimapDataSubsystem->IconLocations.Add(Entity, FVector4(Location.X, Location.Y, RepFragment.IconSize, RepFragment.Intensity));
			
		}
	});
}


//----------------------------------------------------------------------//
//  UMinimapUpdateProcessor
//----------------------------------------------------------------------//

UMinimapUpdateProcessor::UMinimapUpdateProcessor()
	: RepresentationQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	// 在观察者之后运行，确保Tag已经被正确更新
	ExecutionOrder.ExecuteAfter.Add(UMinimapObserverProcessor::StaticClass()->GetFName());
}

void UMinimapUpdateProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MinimapDataSubsystem = GetWorld()->GetSubsystem<UMinimapDataSubsystem>();
}

void UMinimapUpdateProcessor::ConfigureQueries()
{
    // 只查询那些需要在小地图上表示，并且其格子位置已发生变化的单位
    RepresentationQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
    RepresentationQuery.AddRequirement<FMassMinimapRepresentationFragment>(EMassFragmentAccess::ReadOnly);
    RepresentationQuery.AddRequirement<FMassVisionFragment>(EMassFragmentAccess::ReadOnly);
	RepresentationQuery.AddTagRequirement<FMinimapCellChangedTag>(EMassFragmentPresence::All); // 核心优化
    RepresentationQuery.RegisterWithProcessor(*this);
}

void UMinimapUpdateProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!MinimapDataSubsystem)
	{
		return;
	}

	RepresentationQuery.ForEachEntityChunk(EntityManager, Context, [this, &Context](FMassExecutionContext& ChunkContext)
	{
		const auto& LocationList = ChunkContext.GetFragmentView<FTransformFragment>();
		const auto& RepList = ChunkContext.GetFragmentView<FMassMinimapRepresentationFragment>();
		const auto& VisionList = ChunkContext.GetFragmentView<FMassVisionFragment>();
		
		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(i);
			const FVector& Location = LocationList[i].GetTransform().GetLocation();
			const auto& RepFragment = RepList[i];
			const auto& VisionFragment = VisionList[i];

			// 更新或添加数据到TMap
			MinimapDataSubsystem->IconLocations.Add(Entity, FVector4(Location.X, Location.Y, RepFragment.IconSize, RepFragment.Intensity));
			
			MinimapDataSubsystem->VisionSources.Add(Entity, FVector4(Location.X, Location.Y, 0.0f, VisionFragment.SightRadius));

			// 处理完后移除Tag，避免重复处理
			ChunkContext.Defer().RemoveTag<FMinimapCellChangedTag>(Entity);
		}
	});
}




//----------------------------------------------------------------------//
//  UMinimapObserverProcessor
//----------------------------------------------------------------------//
UMinimapObserverProcessor::UMinimapObserverProcessor()
{
	ObservedType = FTransformFragment::StaticStruct();
	Operation = EMassObservedOperation::MAX;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMinimapObserverProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	FogOfWarActor = Cast<AFogOfWar>(UGameplayStatics::GetActorOfClass(GetWorld(), AFogOfWar::StaticClass()));
}

void UMinimapObserverProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassPreviousMinimapCellFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMinimapObserverProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (!FogOfWarActor)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this, &Context](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FTransformFragment> Locations = ChunkContext.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassPreviousMinimapCellFragment> PrevCells = ChunkContext.GetMutableFragmentView<FMassPreviousMinimapCellFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FVector& WorldLocation = Locations[i].GetTransform().GetLocation();
			FMassPreviousMinimapCellFragment& PrevCellFragment = PrevCells[i];

			// Convert world location to grid location, then to tile IJ coordinates.
			// This logic must be consistent with how the FOW grid is structured.
			const FVector2f GridLocation = FogOfWarActor->ConvertWorldSpaceLocationToGridSpace(FVector2D(WorldLocation.X, WorldLocation.Y));
			const FIntVector2 CurrentCellVec = FogOfWarActor->ConvertGridLocationToTileIJ(GridLocation);
			const FIntPoint CurrentCell(CurrentCellVec.X, CurrentCellVec.Y);

			if (CurrentCell != PrevCellFragment.PrevCellCoords)
			{
				// The entity has moved to a new cell. Add a tag to mark it as "dirty"
				// for the UMinimapDataCollectorProcessor to process later.
				Context.Defer().AddTag<FMinimapCellChangedTag>(ChunkContext.GetEntity(i));
				
				// Update the last known cell coordinate.
				PrevCellFragment.PrevCellCoords = CurrentCell;
			}
		}
	});
}
