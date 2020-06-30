﻿#pragma once

#include "FINModuleBase.h"
#include "WidgetComponent.h"
#include "Computer/FINComputerScreen.h"
#include "FINModuleScreen.generated.h"

UCLASS()
class AFINModuleScreen : public AFINModuleBase, public IFINScreen {
	GENERATED_BODY()
private:
    UPROPERTY(SaveGame)
    UObject* GPU = nullptr;
	
public:
    TSharedPtr<SWidget> Widget;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UWidgetComponent* WidgetComponent;

	/**
	* This event gets triggered when a new widget got set by the GPU
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable)
    FScreenWidgetUpdate OnWidgetUpdate;

	/**
	* This event gets triggered when a new GPU got bound
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable)
    FScreenGPUUpdate OnGPUUpdate;

	AFINModuleScreen();

	// Begin AActor
	virtual void BeginPlay() override;
	// End AActor
	
	// Begin IFINScreen
	virtual void BindGPU(UObject* gpu) override;
	virtual UObject* GetGPU() const override;
	virtual void SetWidget(TSharedPtr<SWidget> widget) override;
	virtual TSharedPtr<SWidget> GetWidget() const override;
	// End IFINScreen	
};
