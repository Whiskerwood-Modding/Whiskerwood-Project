#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WWModToolsSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, meta=(DisplayName="WW Mod Tools"))
class UWWModToolsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("WWModTools"); }

	UPROPERTY(Config, EditAnywhere, Category="Build",
		meta=(DisplayName="RunUAT.bat Path (override)",
			ToolTip="Leave blank to use the engine this project was opened with."))
	FFilePath RunUATOverride;
};
