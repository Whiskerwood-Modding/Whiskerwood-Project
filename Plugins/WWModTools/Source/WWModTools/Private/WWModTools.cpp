#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserDelegates.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISettingsModule.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"

#include "NewModDialog.h"
#include "ModActions.h"

class FWWModToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterStyle();

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(
				this, &FWWModToolsModule::RegisterMenus));

		FContentBrowserModule& CBModule =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		// No `this` capture, so the delegate survives module unload.
		auto Extender = FContentBrowserMenuExtender_SelectedPaths::CreateStatic(
			&FWWModToolsModule::ExtendPathContextMenu);
		PathExtenderHandle = Extender.GetHandle();
		CBModule.GetAllPathViewContextMenuExtenders().Add(MoveTemp(Extender));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnregisterOwner(this);

		if (FContentBrowserModule* CB =
			FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
		{
			CB->GetAllPathViewContextMenuExtenders().RemoveAll(
				[H = PathExtenderHandle](const FContentBrowserMenuExtender_SelectedPaths& D)
				{
					return D.GetHandle() == H;
				});
		}

		UnregisterStyle();
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* PlayToolbar =
			UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
		FToolMenuSection& PlaySection = PlayToolbar->FindOrAddSection(TEXT("Play"));

		FToolMenuEntry ToolbarEntry = FToolMenuEntry::InitComboButton(
			TEXT("WWModToolsToolbar"),
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FWWModToolsModule::BuildToolbarMenuWidget),
			FText::FromString(TEXT("Mod Tools")),
			FText::FromString(TEXT("Whiskerwood mod tools")),
			FSlateIcon(TEXT("WWModToolsStyle"), TEXT("WWModTools.ToolbarIcon")),
			false,
			TEXT("WWModToolsToolbar"));
		ToolbarEntry.StyleNameOverride = TEXT("CalloutToolbar");
		PlaySection.AddEntry(ToolbarEntry);
	}

	void RegisterStyle()
	{
		if (StyleSet.IsValid())
		{
			return;
		}

		const TSharedPtr<IPlugin> Plugin =
			IPluginManager::Get().FindPlugin(TEXT("WWModTools"));
		if (!Plugin.IsValid())
		{
			return;
		}

		StyleSet = MakeShared<FSlateStyleSet>(TEXT("WWModToolsStyle"));
		StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
		StyleSet->Set(
			TEXT("WWModTools.ToolbarIcon"),
			new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon200.png")), FVector2D(20.f, 20.f)));

		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
	}

	void UnregisterStyle()
	{
		if (!StyleSet.IsValid())
		{
			return;
		}

		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		StyleSet.Reset();
	}

	static void LaunchWhiskerwood()
	{
		const FString Args = TEXT("/C start \"\" \"steam://rungameid/2489330\"");
		FPlatformProcess::CreateProc(
			TEXT("cmd.exe"),
			*Args,
			true,
			false,
			false,
			nullptr,
			0,
			nullptr,
			nullptr,
			nullptr);
	}

	TSharedRef<SWidget> BuildToolbarMenuWidget()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		BuildModdingMenu(MenuBuilder);
		return MenuBuilder.MakeWidget();
	}

	void BuildModdingMenu(FMenuBuilder& Builder)
	{
		Builder.AddMenuEntry(
			FText::FromString(TEXT("Launch Whiskerwood (steam only)")),
			FText::FromString(TEXT("Run Steam game id 2489330")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FWWModToolsModule::LaunchWhiskerwood)));

		Builder.AddMenuEntry(
			FText::FromString(TEXT("Open Installed Mods Folder")),
			FText::FromString(TEXT("Open %LOCALAPPDATA%/Whiskerwood/Saved/mods in Explorer")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ModActions::OpenInstalledModsDir)));

		Builder.AddSeparator();

		Builder.AddMenuEntry(
			FText::FromString(TEXT("New Mod...")),
			FText::FromString(TEXT("Create a new mod folder with Primary Asset Label and .uplugin")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FModInfo Info;
				if (SNewModDialog::ShowModal(Info))
				{
					ModActions::CreateMod(Info);
				}
			})));

		Builder.AddMenuEntry(
			FText::FromString(TEXT("Plugin Settings...")),
			FText::FromString(TEXT("Open Whiskerwood Mod Tools settings (UAT path)")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"))
					.ShowViewer(TEXT("Editor"), TEXT("Plugins"), TEXT("WWModTools"));
			})));
	}

	static TSharedRef<FExtender> ExtendPathContextMenu(const TArray<FString>& SelectedPaths)
	{
		TSharedRef<FExtender> Extender = MakeShared<FExtender>();

		if (SelectedPaths.Num() != 1) return Extender;

		const FString ModName = ModActions::ModNameFromFolderPath(SelectedPaths[0]);
		if (ModName.IsEmpty()) return Extender;

		Extender->AddMenuExtension(
			TEXT("PathViewFolderOptions"),
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateLambda([ModName](FMenuBuilder& Builder)
			{
				Builder.BeginSection(TEXT("WWModActions"), FText::FromString(TEXT("Whiskerwood Mod Tools")));

				Builder.AddMenuEntry(
					FText::FromString(TEXT("Install")),
					FText::FromString(FString::Printf(
						TEXT("Install '%s' from existing cooked output (no cook)"), *ModName)),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("MainFrame.PackageProject")),
					FUIAction(FExecuteAction::CreateLambda([ModName]()
					{
						ModActions::InstallMod(ModName);
					})));

				Builder.AddMenuEntry(
					FText::FromString(TEXT("Cook & Install")),
					FText::FromString(FString::Printf(
						TEXT("Package '%s' and copy to %%LOCALAPPDATA%%/Whiskerwood/Saved/mods"), *ModName)),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("MainFrame.PackageProject")),
					FUIAction(FExecuteAction::CreateLambda([ModName]()
					{
						ModActions::CookAndInstallMod(ModName);
					})));

				Builder.AddMenuEntry(
					FText::FromString(TEXT("Uninstall")),
					FText::FromString(FString::Printf(
						TEXT("Remove '%s' from %%LOCALAPPDATA%%/Whiskerwood/Saved/mods"), *ModName)),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete")),
					FUIAction(FExecuteAction::CreateLambda([ModName]()
					{
						ModActions::UninstallMod(ModName);
					})));

				Builder.AddMenuEntry(
					FText::FromString(TEXT("Update Mod Version...")),
					FText::FromString(FString::Printf(
						TEXT("Change the Version in %s.uplugin"), *ModName)),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Edit")),
					FUIAction(FExecuteAction::CreateLambda([ModName]()
					{
						ModActions::UpdateModVersion(ModName);
					})));

				Builder.EndSection();
			}));

		return Extender;
	}

	FDelegateHandle PathExtenderHandle;
	TSharedPtr<FSlateStyleSet> StyleSet;
};

IMPLEMENT_MODULE(FWWModToolsModule, WWModTools)
