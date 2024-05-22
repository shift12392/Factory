// Copyright Epic Games, Inc. All Rights Reserved.

#include "FactoryGameMode.h"
//#include "FactoryCharacter.h"
#include "FactoryPlayerController.h"
#include "UObject/ConstructorHelpers.h"

AFactoryGameMode::AFactoryGameMode()
{
	// set default pawn class to our Blueprinted character
	 
	
	//static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	//if (PlayerPawnBPClass.Class != NULL)
	//{
	//	DefaultPawnClass = PlayerPawnBPClass.Class;
	//}
	PlayerControllerClass = AFactoryPlayerController::StaticClass();
	
}
