// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryPlayerController.h"

void AFactoryPlayerController::BeginPlay()
{
	//��ʾ���ָ��
	bShowMouseCursor = true;
	
	//��������ģʽ
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(true);
	SetInputMode(InputMode);
}
