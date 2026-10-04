#pragma once

#include "CoreMinimal.h"

// .uplugin file definition
namespace ModFields
{
	inline const FString Name = TEXT("Name");
	inline const FString Description = TEXT("Description");
	inline const FString Version = TEXT("Version");
	inline const FString CreatedBy = TEXT("CreatedBy");
	inline const FString EngineVersion = TEXT("EngineVersion");
}

// Defines the various fields per .uplugin file definition field, that is used by various parts of the mod tools to show to user
struct FModFieldDef
{
	FString Key; // .uplugin field key
	FString Label;
	FString DefaultValue; // default value if the field is missing
	FString ExampleValue; // hint value
	bool bUserEditable = true; // false that cannot be changed by user e.g. EngineVersion
};

struct FModInfo
{
	// Mod folder under /Game/Mods/ which is also the .pak and .uplugin file name
	FString FolderName;

	TMap<FString, FString> Values;

	FString Get(const FString& Key) const
	{
		const FString* Value = Values.Find(Key);
		return Value ? *Value : FString();
	}

	void Set(const FString& Key, const FString& Value) { Values.Add(Key, Value); }

	bool Has(const FString& Key) const { return Values.Contains(Key); }
};

namespace ModActions
{
	const TArray<FModFieldDef>& GetModFieldDefs();

	// Expands {ModName} in a field default
	FString ResolveFieldDefault(const FModFieldDef& Def, const FString& ModName);

	// Fills in missing defaults and forces the value of fields the user cannot edit
	void ApplyFieldDefaults(FModInfo& Info);

	// True when an editable field has no value recorded at all, which is what happens to a mod made before that field existed
	bool NeedsDetailsPrompt(const FModInfo& Info);

	bool CreateMod(const FModInfo& Info);

	void CookAndInstallMod(const FString& ModName);

	void InstallMod(const FString& ModName);

	void UninstallMod(const FString& ModName);

	void OpenInstalledModsDir();

	void UpdateModDetails(const FString& ModName);

	FString ModNameFromFolderPath(const FString& FolderPath);

	FString GetModsInstallRoot();

	FString GetSourceUPluginPath(const FString& ModName);

	void GetModFolderNames(TArray<FString>& OutModNames);

	bool LoadModInfo(const FString& ModName, FModInfo& OutInfo);

	bool SaveModInfo(const FModInfo& Info);

	bool EnsureModInfo(const FString& ModName, FModInfo& OutInfo);
}
