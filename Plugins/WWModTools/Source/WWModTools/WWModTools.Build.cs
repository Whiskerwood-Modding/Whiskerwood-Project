using UnrealBuildTool;

public class WWModTools : ModuleRules
{
	public WWModTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"ContentBrowserData",
			"DeveloperSettings",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"InputCore",
			"DesktopPlatform",
			"Kismet",
			"UMGEditor",
			"UMG",
			"Settings",
			"Json",
		});
	}
}
