// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryPlayerController.h"

void AFactoryPlayerController::BeginPlay()
{
	//显示鼠标指针
	bShowMouseCursor = true;
	
	//设置输入模式
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(true);
	SetInputMode(InputMode);
}
