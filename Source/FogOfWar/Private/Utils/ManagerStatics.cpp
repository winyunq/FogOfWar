// Copyright 2024 zhmyh1337 (https://github.com/zhmyh1337/). All Rights Reserved.


#include "Utils/ManagerStatics.h"

#include "Utils/ManagerComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"

UManagerComponent* UManagerStatics::GetGameManager(const UObject* WorldContextObject)
{
	AGameStateBase* GameState = UGameplayStatics::GetGameState(WorldContextObject);
	return GameState ? GameState->GetComponentByClass<UManagerComponent>() : nullptr;
}
