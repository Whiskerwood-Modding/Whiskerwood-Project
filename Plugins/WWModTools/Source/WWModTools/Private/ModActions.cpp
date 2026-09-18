#include "ModActions.h"
#include "WWModToolsSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/DataAsset.h"
#include "Engine/PrimaryAssetLabel.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/DataAssetFactory.h"
#include "WidgetBlueprintFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "TextInputDialog.h"

DEFINE_LOG_CATEGORY_STATIC(LogWWModTools, Log, All);

static void ShowNotification(const FString& Message, bool bSuccess)
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 5.f;
	Info.bFireAndForget = true;
	Info.Image = bSuccess
		? FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithColor"))
		: FCoreStyle::Get().GetBrush(TEXT("Icons.ErrorWithColor"));
	FSlateNotificationManager::Get().AddNotification(Info);
}

static int32 FindNextChunkId()
{
	TSet<int32> Used;

	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Chunk IDs from Primary Asset Labels anywhere in the project.
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UPrimaryAssetLabel::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Labels;
		AR.GetAssets(Filter, Labels);

		for (const FAssetData& Data : Labels)
		{
			UPrimaryAssetLabel* Label = Cast<UPrimaryAssetLabel>(Data.GetAsset());
			if (Label && Label->Rules.ChunkId > 0)
			{
				Used.Add(Label->Rules.ChunkId);
			}
		}
	}

	// Chunk IDs assigned directly to assets (Asset Actions > Assign to Chunk), e.g. mods without a PAL.
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		for (const FAssetData& Data : Assets)
		{
			for (const int32 ChunkId : Data.GetChunkIDs())
			{
				if (ChunkId > 0) Used.Add(ChunkId);
			}
		}
	}

	// Anything already present in the cook output.
	{
		const FString PaksDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())
			/ TEXT("Windows/Whiskerwood/Content/Paks");
		TArray<FString> PakFiles;
		IFileManager::Get().FindFiles(PakFiles, *(PaksDir / TEXT("pakchunk*-Windows.pak")), true, false);
		for (const FString& File : PakFiles)
		{
			const FString Digits = File.Mid(8, File.Find(TEXT("-")) - 8);
			if (Digits.IsNumeric())
			{
				const int32 ChunkId = FCString::Atoi(*Digits);
				if (ChunkId > 0) Used.Add(ChunkId);
			}
		}
	}

	for (int32 Id = 1; Id <= 300; ++Id)
	{
		if (!Used.Contains(Id)) return Id;
	}

	return -1;
}

// Where the game loads mods from: %LOCALAPPDATA%/Whiskerwood/Saved/mods
static FString GetModsInstallRoot()
{
	const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (LocalAppData.IsEmpty()) return FString();
	return LocalAppData / TEXT("Whiskerwood/Saved/mods");
}

// The project-side copy of the .uplugin, kept next to the mod's assets so it can be edited.
static FString GetSourceUPluginPath(const FString& ModName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())
		/ TEXT("Mods") / ModName / (ModName + TEXT(".uplugin"));
}

static bool WriteUPlugin(const FModInfo& Info, const FString& Path)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("Name"),
		Info.DisplayName.IsEmpty() ? Info.FolderName : Info.DisplayName);
	Json->SetStringField(TEXT("Description"), Info.Description);
	Json->SetStringField(TEXT("Version"), Info.Version.IsEmpty() ? TEXT("1.0") : Info.Version);
	Json->SetStringField(TEXT("CreatedBy"), Info.CreatedBy);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Json, Writer)) return false;

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ModActions::CreateMod(const FModInfo& Info)
{
	const FString& ModName = Info.FolderName;
	const FString PackageBase = FString::Printf(TEXT("/Game/Mods/%s"), *ModName);

	{
		IAssetRegistry& AR =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Existing;
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*PackageBase));
		AR.GetAssets(Filter, Existing);
		if (Existing.Num() > 0)
		{
			ShowNotification(
				FString::Printf(TEXT("Mod '%s' already exists."), *ModName), false);
			return false;
		}
	}

	const int32 ChunkId = FindNextChunkId();
	if (ChunkId < 0)
	{
		ShowNotification(TEXT("No free ChunkID available in range 1-300."), false);
		return false;
	}

	FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	bool bAllOk = true;

	{
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UPrimaryAssetLabel::StaticClass();
		const FString PALName = FString::Printf(TEXT("PAL_%s"), *ModName);
		UObject* Asset = AssetTools.CreateAsset(
			PALName, PackageBase, UPrimaryAssetLabel::StaticClass(), Factory);
		if (UPrimaryAssetLabel* Label = Cast<UPrimaryAssetLabel>(Asset))
		{
			Label->Rules.ChunkId = ChunkId;
			Label->Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;
			Label->bLabelAssetsInMyDirectory = true;
			Label->MarkPackageDirty();

			UPackage* Pkg = Label->GetOutermost();
			const FString DiskPath = FPackageName::LongPackageNameToFilename(
				Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(Pkg, Label, *DiskPath, SaveArgs);
		}
		else
		{
			UE_LOG(LogWWModTools, Warning, TEXT("Failed to create PAL_%s"), *ModName);
			bAllOk = false;
		}
	}

	if (bAllOk && !WriteUPlugin(Info, GetSourceUPluginPath(ModName)))
	{
		UE_LOG(LogWWModTools, Warning, TEXT("Failed to write %s"), *GetSourceUPluginPath(ModName));
		bAllOk = false;
	}

	if (bAllOk)
	{
		ShowNotification(
			FString::Printf(TEXT("Mod '%s' created (ChunkID %d)."), *ModName, ChunkId), true);
	}
	return bAllOk;
}

static FString GetRunUATPath()
{
	const UWWModToolsSettings* Settings = GetDefault<UWWModToolsSettings>();
	if (!Settings->RunUATOverride.FilePath.IsEmpty()
		&& FPaths::FileExists(Settings->RunUATOverride.FilePath))
	{
		return Settings->RunUATOverride.FilePath;
	}

	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString Candidate = EngineDir / TEXT("Build/BatchFiles/RunUAT.bat");
	if (FPaths::FileExists(Candidate)) return Candidate;

	return FString();
}

static int32 GetChunkIdForMod(const FString& ModName)
{
	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	const FString PALPath = FString::Printf(
		TEXT("/Game/Mods/%s/PAL_%s.PAL_%s"), *ModName, *ModName, *ModName);
	FAssetData Data = AR.GetAssetByObjectPath(FSoftObjectPath(PALPath));
	if (UPrimaryAssetLabel* Label = Cast<UPrimaryAssetLabel>(Data.GetAsset()))
	{
		return Label->Rules.ChunkId;
	}
	return -1;
}

static bool CopyInstalledFile(const FString& Src, const FString& Dest, FString& OutError)
{
	IFileManager& FM = IFileManager::Get();
	FString NormalizedSrc = FPaths::ConvertRelativePathToFull(Src);
	FString NormalizedDest = FPaths::ConvertRelativePathToFull(Dest);
	FPaths::MakePlatformFilename(NormalizedSrc);
	FPaths::MakePlatformFilename(NormalizedDest);

	if (!FPaths::FileExists(NormalizedSrc))
	{
		OutError = FString::Printf(TEXT("Source file not found: %s"), *NormalizedSrc);
		return false;
	}

	const FString DestDir = FPaths::GetPath(NormalizedDest);
	FM.MakeDirectory(*DestDir, true);

	const uint32 CopyResult = FM.Copy(*NormalizedDest, *NormalizedSrc, true, true, false);
	if (CopyResult == COPY_OK)
	{
		return true;
	}

	// Fallback to cmd copy /Y
	// bit nasty but does the job if fm can't do it
	const FString CopyArgs = FString::Printf(
		TEXT("/C copy /Y \"%s\" \"%s\" >nul"),
		*NormalizedSrc, *NormalizedDest);

	int32 ExitCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bRan = FPlatformProcess::ExecProcess(
		TEXT("cmd.exe"), *CopyArgs, &ExitCode, &StdOut, &StdErr);

	if (bRan && ExitCode == 0)
	{
		return true;
	}

	OutError = FString::Printf(
		TEXT("Copy failed (IFileManager=%u, cmd exit=%d)."),
		CopyResult, ExitCode);
	if (!StdErr.IsEmpty())
	{
		OutError += TEXT(" ") + StdErr.TrimStartAndEnd();
	}

	return false;
}

// Installs to %LOCALAPPDATA%/Whiskerwood/Saved/mods/<ModName>/:
//   pakchunk<Id>-Windows.pak -> <ModName>.pak
//   Content/Mods/<ModName>/<ModName>.uplugin -> <ModName>.uplugin (generated with defaults if missing)
static bool InstallCookedChunkFiles(
	const FString& ModName,
	const FString& PaksDir,
	int32 ChunkId,
	int32& OutInstalled,
	FString& OutError)
{
	OutInstalled = 0;
	OutError.Reset();

	const FString ModsRoot = GetModsInstallRoot();
	if (ModsRoot.IsEmpty())
	{
		OutError = TEXT("Could not resolve %LOCALAPPDATA%.");
		return false;
	}

	const FString PakName = FString::Printf(TEXT("pakchunk%d-Windows.pak"), ChunkId);
	const FString PakSrc = PaksDir / PakName;
	if (!FPaths::FileExists(PakSrc))
	{
		OutError = FString::Printf(
			TEXT("No cooked %s found for '%s' in %s"), *PakName, *ModName, *PaksDir);
		return false;
	}

	const FString ModInstallDir = ModsRoot / ModName;
	IFileManager::Get().MakeDirectory(*ModInstallDir, true);

	const FString PakDest = ModInstallDir / (ModName + TEXT(".pak"));
	if (!CopyInstalledFile(PakSrc, PakDest, OutError))
	{
		UE_LOG(LogWWModTools, Warning,
			TEXT("Failed to copy: %s -> %s (%s)"), *PakSrc, *PakDest, *OutError);
		return false;
	}
	++OutInstalled;
	UE_LOG(LogWWModTools, Display, TEXT("Installed: %s"), *PakDest);

	const FString UPluginSrc = GetSourceUPluginPath(ModName);
	if (!FPaths::FileExists(UPluginSrc))
	{
		FModInfo Defaults;
		Defaults.FolderName = ModName;
		if (!WriteUPlugin(Defaults, UPluginSrc))
		{
			OutError = FString::Printf(TEXT("Failed to write %s"), *UPluginSrc);
			return false;
		}
		UE_LOG(LogWWModTools, Display, TEXT("Generated default %s"), *UPluginSrc);
	}

	const FString UPluginDest = ModInstallDir / (ModName + TEXT(".uplugin"));
	if (!CopyInstalledFile(UPluginSrc, UPluginDest, OutError))
	{
		UE_LOG(LogWWModTools, Warning,
			TEXT("Failed to copy: %s -> %s (%s)"), *UPluginSrc, *UPluginDest, *OutError);
		return false;
	}
	++OutInstalled;
	UE_LOG(LogWWModTools, Display, TEXT("Installed: %s"), *UPluginDest);

	return true;
}

void ModActions::CookAndInstallMod(const FString& ModName)
{
	const FString RunUAT = GetRunUATPath();
	if (RunUAT.IsEmpty())
	{
		ShowNotification(TEXT("RunUAT.bat not found. Check Whiskerwood Mod Tools settings."), false);
		return;
	}

	const int32 ChunkId = GetChunkIdForMod(ModName);
	if (ChunkId < 0)
	{
		ShowNotification(
			FString::Printf(TEXT("Cannot find PAL_%s or its ChunkID."), *ModName), false);
		return;
	}

	const FString ProjectPath =
		FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString OutputDir =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	const FString Args = FString::Printf(
		TEXT("BuildCookRun"
			" -project=\"%s\""
			" -platform=Win64"
			" -clientconfig=Shipping"
			" -build -cook -stage -pak"
			" -archive -archivedirectory=\"%s\""
			" -nocompileeditor -installed -iterativecooking -cookincremental"
			" -nop4 -utf8output -unattended"),
		*ProjectPath, *OutputDir);

	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> bCancelRequested =
		MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);

	FNotificationInfo Info(
		FText::FromString(FString::Printf(TEXT("Cooking '%s'"), *ModName)));
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.bUseSuccessFailIcons = true;
	Info.FadeOutDuration = 3.f;
	Info.ExpireDuration = 6.f;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		FText::FromString(TEXT("Cancel")),
		FText::FromString(TEXT("Cancel cooking and installation.")),
		FSimpleDelegate::CreateLambda([bCancelRequested]()
		{
			bCancelRequested->Store(true);
		}),
		SNotificationItem::CS_Pending));

	TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info);
	Notification->SetCompletionState(SNotificationItem::CS_Pending);

	TWeakPtr<SNotificationItem> WeakNotif = Notification;

	auto ParseUATLine = [](const FString& Line, FString& OutStatus) -> bool
	{
		if (Line.StartsWith(TEXT("[")))
		{
			int32 Slash, Close;
			if (Line.FindChar('/', Slash) && Line.FindChar(']', Close) && Close > Slash)
			{
				const FString Rest = Line.Mid(Close + 1).TrimStart();
				if (!Rest.IsEmpty())
				{
					OutStatus = Line.Left(Close + 1) + TEXT(" ") + Rest.Left(60);
					return true;
				}
			}
		}
		if (Line.Contains(TEXT("LogCook:")) && Line.Contains(TEXT("Cooking")))
		{
			OutStatus = TEXT("Cooking assets...");
			return true;
		}
		if (Line.Contains(TEXT("LogCook:")) && Line.Contains(TEXT("Finish")))
		{
			OutStatus = TEXT("Finalising cook...");
			return true;
		}
		if (Line.Contains(TEXT("Stage:")) || Line.Contains(TEXT("Staging")))
		{
			OutStatus = TEXT("Staging files...");
			return true;
		}
		if (Line.Contains(TEXT("Pak:")) || Line.Contains(TEXT("UnrealPak")))
		{
			OutStatus = TEXT("Building pak...");
			return true;
		}
		if (Line.Contains(TEXT("Archive:")))
		{
			OutStatus = TEXT("Archiving...");
			return true;
		}
		return false;
	};

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[RunUAT, Args, OutputDir, ModName, ChunkId,
		 WeakNotif, ParseUATLine, bCancelRequested]()
	{
		auto UpdateNotif = [&WeakNotif](const FString& Text)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakNotif, Text]()
			{
				if (TSharedPtr<SNotificationItem> Pin = WeakNotif.Pin())
				{
					Pin->SetText(FText::FromString(Text));
				}
			});
		};

		auto FinishNotif = [&WeakNotif](const FString& Text, bool bSuccess)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakNotif, Text, bSuccess]()
			{
				if (TSharedPtr<SNotificationItem> Pin = WeakNotif.Pin())
				{
					Pin->SetText(FText::FromString(Text));
					Pin->SetCompletionState(
						bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
					Pin->ExpireAndFadeout();
				}
			});
		};

		void* PipeRead  = nullptr;
		void* PipeWrite = nullptr;
		FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

		FProcHandle Proc = FPlatformProcess::CreateProc(
			*RunUAT, *Args,
			false, true, true,
			nullptr, 0, nullptr, PipeWrite, PipeRead);

		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			FinishNotif(TEXT("Failed to launch RunUAT."), false);
			return;
		}

		FString LastStatus;
		while (FPlatformProcess::IsProcRunning(Proc))
		{
			if (bCancelRequested->Load())
			{
				FPlatformProcess::TerminateProc(Proc, true);
				break;
			}

			const FString Chunk = FPlatformProcess::ReadPipe(PipeRead);
			if (!Chunk.IsEmpty())
			{
				UE_LOG(LogWWModTools, Log, TEXT("[UAT] %s"), *Chunk);

				TArray<FString> Lines;
				Chunk.ParseIntoArrayLines(Lines);
				for (const FString& Line : Lines)
				{
					FString Status;
					if (ParseUATLine(Line.TrimStartAndEnd(), Status) && Status != LastStatus)
					{
						LastStatus = Status;
						UpdateNotif(FString::Printf(
							TEXT("Cooking '%s'\n%s"), *ModName, *Status));
					}
				}
			}
			FPlatformProcess::Sleep(0.2f);
		}

		int32 ExitCode = 0;
		FPlatformProcess::GetProcReturnCode(Proc, &ExitCode);
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		FPlatformProcess::CloseProc(Proc);

		if (bCancelRequested->Load())
		{
			FinishNotif(FString::Printf(TEXT("Cancelled '%s'."), *ModName), false);
			UE_LOG(LogWWModTools, Warning, TEXT("Cook cancelled by user."));
			return;
		}

		if (ExitCode != 0)
		{
			FinishNotif(
				FString::Printf(TEXT("Cook failed (exit %d). See Output Log."), ExitCode), false);
			return;
		}

		UpdateNotif(FString::Printf(TEXT("Cooking '%s'\nInstalling..."), *ModName));

		const FString PaksDir = OutputDir / TEXT("Windows/Whiskerwood/Content/Paks");

		int32 Installed = 0;
		FString InstallError;
		if (!InstallCookedChunkFiles(ModName, PaksDir, ChunkId, Installed, InstallError))
		{
			FinishNotif(
				FString::Printf(TEXT("'%s' install failed: %s"), *ModName, *InstallError),
				false);
			return;
		}

		FinishNotif(FString::Printf(TEXT("'%s' installed (%d file(s))"), *ModName, Installed), true);
	});
}

void ModActions::InstallMod(const FString& ModName)
{
	const int32 ChunkId = GetChunkIdForMod(ModName);
	if (ChunkId < 0)
	{
		ShowNotification(
			FString::Printf(TEXT("Cannot find PAL_%s or its ChunkID."), *ModName), false);
		return;
	}

	const FString OutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString PaksDir = OutputDir / TEXT("Windows/Whiskerwood/Content/Paks");

	int32 Installed = 0;
	FString InstallError;
	if (!InstallCookedChunkFiles(ModName, PaksDir, ChunkId, Installed, InstallError))
	{
		ShowNotification(
			FString::Printf(TEXT("Install failed for '%s': %s"), *ModName, *InstallError), false);
		return;
	}

	ShowNotification(
		FString::Printf(TEXT("'%s' installed (%d file(s))"), *ModName, Installed), true);
}

void ModActions::UninstallMod(const FString& ModName)
{
	const FString ModsRoot = GetModsInstallRoot();
	if (ModsRoot.IsEmpty())
	{
		ShowNotification(TEXT("Could not resolve %LOCALAPPDATA%."), false);
		return;
	}

	const FString ModInstallDir = ModsRoot / ModName;

	if (!IFileManager::Get().DirectoryExists(*ModInstallDir))
	{
		ShowNotification(
			FString::Printf(TEXT("No installed files found for '%s'."), *ModName), false);
		return;
	}

	const bool bRemoved =
		IFileManager::Get().DeleteDirectory(*ModInstallDir, false, true);

	ShowNotification(
		bRemoved
			? FString::Printf(TEXT("'%s' uninstalled."), *ModName)
			: FString::Printf(TEXT("Failed to remove '%s'. Is the game running?"), *ModName),
		bRemoved);
}

void ModActions::OpenInstalledModsDir()
{
	const FString ModsRoot = GetModsInstallRoot();
	if (ModsRoot.IsEmpty())
	{
		ShowNotification(TEXT("Could not resolve %LOCALAPPDATA%."), false);
		return;
	}

	IFileManager::Get().MakeDirectory(*ModsRoot, true);
	FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(ModsRoot));
}

static TSharedPtr<FJsonObject> ReadUPlugin(const FString& Path)
{
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *Path)) return nullptr;

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid()) return nullptr;
	return Json;
}

// Sets "Version" in an existing .uplugin, keeping its other fields.
static bool SetUPluginVersion(const FString& Path, const FString& Version)
{
	TSharedPtr<FJsonObject> Json = ReadUPlugin(Path);
	if (!Json.IsValid()) return false;

	Json->SetStringField(TEXT("Version"), Version);

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Json.ToSharedRef(), Writer)) return false;
	return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void ModActions::UpdateModVersion(const FString& ModName)
{
	const FString SourcePath = GetSourceUPluginPath(ModName);

	FString CurrentVersion;
	if (TSharedPtr<FJsonObject> Json = ReadUPlugin(SourcePath))
	{
		Json->TryGetStringField(TEXT("Version"), CurrentVersion);
	}
	if (CurrentVersion.IsEmpty())
	{
		CurrentVersion = TEXT("1.0");
	}

	FString NewVersion;
	if (!STextInputDialog::ShowModal(
			FText::FromString(FString::Printf(TEXT("Update '%s' Version"), *ModName)),
			FText::FromString(TEXT("Mod version:")),
			CurrentVersion,
			NewVersion))
	{
		return;
	}

	if (!FPaths::FileExists(SourcePath))
	{
		FModInfo Defaults;
		Defaults.FolderName = ModName;
		Defaults.Version = NewVersion;
		if (!WriteUPlugin(Defaults, SourcePath))
		{
			ShowNotification(FString::Printf(TEXT("Failed to write %s"), *SourcePath), false);
			return;
		}
	}
	else if (!SetUPluginVersion(SourcePath, NewVersion))
	{
		ShowNotification(FString::Printf(TEXT("Failed to update %s"), *SourcePath), false);
		return;
	}

	// Keep an already-installed copy in sync.
	const FString ModsRoot = GetModsInstallRoot();
	const FString InstalledPath = ModsRoot / ModName / (ModName + TEXT(".uplugin"));
	if (!ModsRoot.IsEmpty() && FPaths::FileExists(InstalledPath))
	{
		FString CopyError;
		if (!CopyInstalledFile(SourcePath, InstalledPath, CopyError))
		{
			UE_LOG(LogWWModTools, Warning, TEXT("Failed to update installed %s (%s)"),
				*InstalledPath, *CopyError);
		}
	}

	ShowNotification(
		FString::Printf(TEXT("'%s' version set to %s."), *ModName, *NewVersion), true);
}

FString ModActions::ModNameFromFolderPath(const FString& FolderPath)
{
	const FString Prefix = TEXT("/Game/Mods/");
	if (!FolderPath.StartsWith(Prefix)) return FString();
	const FString Remainder = FolderPath.Mid(Prefix.Len());
	if (Remainder.Contains(TEXT("/"))) return FString();
	return Remainder;
}
