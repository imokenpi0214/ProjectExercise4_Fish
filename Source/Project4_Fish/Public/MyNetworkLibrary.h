// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyNetworkLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT4_FISH_API UMyNetworkLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "Network")
	static FString GetLocalIPv4();
};
