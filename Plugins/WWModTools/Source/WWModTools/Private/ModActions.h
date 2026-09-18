#pragma once

#include "CoreMinimal.h"

// Metadata written to <ModName>.uplugin.
struct FModInfo
{
	FString FolderName;   // Mod folder under /Game/Mods, also the .pak/.uplugin file name
	FString DisplayName;  // "Name" field in the .uplugin
	FString Description;
	FString Version = TEXT("1.0");
	FString CreatedBy;
};

namespace ModActions
{
	bool CreateMod(const FModInfo& Info);

	void CookAndInstallMod(const FString& ModName);

	void InstallMod(const FString& ModName);

	void UninstallMod(const FString& ModName);

	// Opens %LOCALAPPDATA%/Whiskerwood/Saved/mods in Explorer.
	void OpenInstalledModsDir();

	// Prompts for a new version and writes it to the mod's .uplugin (project and installed copies).
	void UpdateModVersion(const FString& ModName);

	// "/Game/Mods/MyMod" -> "MyMod"; empty for anything else.
	FString ModNameFromFolderPath(const FString& FolderPath);
}
