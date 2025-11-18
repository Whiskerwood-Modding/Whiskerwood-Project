# Whiskerwood-Project
Template UE5.6 project for Whiskerwood, including reflected headers and example mods.

## How to open the project

If you haven't already, install Unreal Engine 5.6.

To use, [clone](https://docs.github.com/en/desktop/contributing-and-collaborating-using-github-desktop/adding-and-cloning-repositories/cloning-and-forking-repositories-from-github-desktop), fork or download the repository as .zip, and follow these steps, then double click on `Whiskerwood.uproject` to open the project!

**(Optional):** If you want to generate a `.sln` file to build the project from its source, right click the `Whiskerwood.uproject` file and click `Generate Visual Studio project files` (requires the correct build tools, there are plenty of docs online on how to build UE projects).

## Making mods

This project starts off with some example mod files inside of `Content/Mods`. 

As you can see, each mod has its own folder, then there are one or both of:

* `BP_Startup` - This blueprint is spawned by the game when the game loads into the main menu. This is the best place to register mod options or write values to a data table. See the "Some notes" below for more info.
* `BP_MapLoad` - This blueprint is spawned by the game while loading into a save. This is the place to do your game logic.

So, to make your own mod:

1. Inside of `Content/Mods`, make a new folder with your mod's name, ideally in UpperCamelCase. 
2. Create a new blueprint with base `Actor` in the mod's folder you created and call it it one of the above two names, depending on what you want to do.

> [!IMPORTANT]
> Although you can change your mod's folder name later, it is **critical** that your mod folder name is the same name as your installed mod's folder and `.pak` file as the game will look for the folder name in the `.pak` file when it loads it!

## ModAPI

The modding API provided by the game is pretty special, because the lead developer of Whiskerwood has added some awesome functions and delegates that help make modding easier. The game is also really moddable, because the game's architecture is using [data driven gameplay](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine?application_version=5.6) - much of the "hardcoded" values are actually in [Data Tables](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine?application_version=5.6#datatables)!

### Properties

These are available properties that allows you to get references to some of the core game systems.

```cpp
UPROPERTY(EditAnywhere)
    class UBackbone *backbone;
UPROPERTY(EditAnywhere)
    class UOptionManager *optMan;
UPROPERTY(EditAnywhere)
    class UMasterSyncManager *masterSync;
UPROPERTY(EditAnywhere)
    class UModManager *modMan;
UPROPERTY(EditAnywhere)
    FModApiState state;
```

### Functions

These are functions that can be called. This extract includes developer comments.

```cpp
// Access the mod api. Static call with cheat look up so you do not need to store
// a reference.
UFUNCTION(BlueprintPure, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    static UModAPI *GetModAPI(const class UObject *worldContext);

// Set the pool of possible whisker names from which new whiskers will be randomly named
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void SetWhiskerNamePool(TArray<FString> names);

// Add Data Table to the moddable table lookup
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void AddDataTable(class UObject *worldContext, FName datatableName, class UDataTable *table);

UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    bool HasDataTable(class UObject *worldContext, FName datatableName);

// Returns the list of all data tables accessible through the modapi's read and write calls
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    TArray<FName> ListDataTables(class UObject *worldContext);

// Outputs the entire data table to the mod log file as json
// Outputs the full table as JSON to %localappdata%/Whiskerwood/Logs/TABLENAME.json
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void DumpDataTableToLogFolder(class UObject *worldContext, FName datatableName);

// Outputs all modapi accessible datatables to the %localappdata%/Whiskerwood/Logs/ folder as jsons
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void DumpAllDataTablesToLogFolder(class UObject *worldContext);

// Read the value of a data table entry. See modapi's ListDataTables to get an updated listing of exposed data tables
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    FString ReadDataTableValue(class UObject *worldContext, FName datatableName, FName rowId, FName columnName);

// Write the value of a data table entry. See modapi's ListDataTables to get an updated listing of exposed data tables
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    bool WriteDataTableValue(class UObject *worldContext, FName datatableName, FName rowId, FName columnName, FString valueStringified);

// Add an empty row at the rowId. Does nothing if the row already exists
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    bool AddDataTableRow(class UObject *worldContext, FName datatableName, FName rowId);

// Add a new option to the Mod tab of the settings menu
// In the optionDisplayName make sure to prepend your mod's name so players know which option is yours
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    bool RegisterModOptions(class UObject *worldContext, FString optionId, FString optionDisplayName, TArray<FString> values, FString defaultValue, FString optionDescription = "");

// Read the current value of an option on the settings menu.
// This function can check non mod options as well.
// Avoid calling on tick(), if you need updated values register on the onOptionChanged delegate
// If players have not chosen a value from the settings menu, the fallbackValue will be returned to you
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    FString ReadModOptionValue(class UObject *worldContext, FString optionId, FString fallbackValue);

// Log a message to %localappdata%/Whiskerwood/Logs/modlog.txt
// The log file will be truncated to 1million chracters after writing.
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void LogMessage(class UObject *worldContext, FString msg);

// Read text file in location %localappdata%/Whiskerwood/Mods/MODNAME/FILENAME
// Read a text file into string. Mod Name must be under 100 chars and alphanumeric. Same limits apply to filename.
// The filename must point to a file within the modname folder within the mods folder.
// Avoid calling often as the file read is sync and will cause a frame stutter.
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    FString ReadModTextFile(class UObject *worldContext, FString modName, FString filename);

// Read a csv text file from %localappdata%/Whiskerwood/Mods/MODNAME/lang.csv
// The CSV should have the first row as a header. The first column should be the string id, the second column the translated text
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    bool AddNewTranslation(class UObject *worldContext, FString modId, FString translationName);
    
UFUNCTION(BlueprintCallable, meta = (WorldContext = "worldContext"), Category = "ModAPI")
    void DumpEnglishToLogFolder(class UObject *worldContext);
```

### Delegates

These are delegates that you can bind to in your mod, then fire an event from that.

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FModAPI_OnEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FModAPI_OnActorSpawned, AActor *, actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FModAPI_OnOptionChange, FString, optionId, FString, value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FModAPI_OnDayStart, int32, day);

UPROPERTY(BlueprintAssignable)
    FModAPI_OnEvent onLoadingFinished;
UPROPERTY(BlueprintAssignable)
    FModAPI_OnActorSpawned onConstructionSpawned;
UPROPERTY(BlueprintAssignable)
    FModAPI_OnActorSpawned onBuildingSpawned;
UPROPERTY(BlueprintAssignable)
    FModAPI_OnActorSpawned onWhiskerSpawned;
UPROPERTY(BlueprintAssignable)
    FModAPI_OnOptionChange onOptionChanged;
UPROPERTY(BlueprintAssignable)
    FModAPI_OnDayStart onDayStart;
```

For example, in `Content/Mods/CopperSlides/BP_MapLoad`, it is binding on two delegates:

1. `onLoadingFinished` - This is because the mod updates the material of all slide actors in the world, which depends on those actors having been spawned in (or loaded) during the loading screen first. `BP_MapLoad` itself spawns during the loading screen, so without this delegate, many or all slide actors would not spawn in time to get a reference to in the mod. *You may be tempted to use a delay node instead, but this is a bad idea because big saves take a long time to load.*
2. `onBuildingSpawned` - This is because when the player builds a new slide, we need to apply the copper material to the new slide. This delegate fires when a building is spawned and returns the reference to that actor, which we check if is a slide, and if it is (if the cast to slide is successful), then apply the copper material.

![Example-CopperSlides](Docs/Images/Example-CopperSlides-1.png)

`onOptionChanged` is required when your mod is changing a mod option value and you need to check if the user has changed a mod option during runtime, then do logic based on that. 

For example, in `Content/Mods/ShortNights/BP_MapLoad`, if the player changes the night speed multiplier option, the delegate will fire and then it updates the internally tracked time dilation variable:

![Example-ShortNight](Docs/Images/Example-ShortNight-1.png)

It simply checks if the returned `OptionId` is the one used by the mod, and if it is, then the user has changed that option value.

## Packaging your mod

In this project, we use pak chunks to package your mod files into `.pak` mods.

In your mod folder, right click and select **Miscellaneous > Data Asset**:

![Paking-Step-0](Docs/Images/Paking-Step-0.png)

Now search for and select `Primary Asset Label`.

![Paking-Step-1](Docs/Images/Paking-Step-1.png)

To follow standard UE naming conventions, call it `PAL_yourmodname` e.g. `PAL_DemoMod`.

![Paking-Step-2](Docs/Images/Paking-Step-2.png)

Open the asset, set `Cook Rule` to `Always Cook`, and check `Label Assets in My Directory`, like so.

![Paking-Step-3](Docs/Images/Paking-Step-3.png)

Enter a number between `1` and `300` in this chunk Id field and press Ok. **Do not enter 0 for the Id!**

> [!IMPORTANT]
> Each mod **must** use a seperate pak chunk number to get packaged seperately from each other! Take note of each pak chunk Id you are assigning to files in each mod.

![Paking-Step-4](Docs/Images/Paking-Step-4.png)

This PAL is a good pal, because it will automatically assign all files in your mod folder with the chunk Id you set for it. It does not get packaged into the game (unless you explicitly tell it to), as it's just a tool for the editor.

Now do `Ctrl` + `S` to save.

Simply click on to `Platforms` -> `Windows` -> `Package Project`:

![Paking-Step-5](Docs/Images/Paking-Step-5.png)

Now select the output folder location. It doesn't matter much where you put it, so I always just put it into the template project folder. It will create a `Windows` folder. You don't need to delete this folder between packages.

![Paking-Step-6](Docs/Images/Paking-Step-6.png)

The first time you package it might take a while, as it will likely need to compile some shaders.

Once it is done, you will hear a noise and it will say Packaging complete.

Now navigate to the `Windows/Whiskerwood/Content/Paks` folder, you should see al pakchunk files here. There will always be a `pakchunk0` which contains all other packaged editor assets, and it is quite large, so this is why you mustn't set your chunkId to 0.

![Paking-Step-7](Docs/Images/Paking-Step-7.png)

## Installing the packaged mod

First copy the pakchunk id file, for the number you entered for your mod files. E.g. you set your id to 42, so copy `pakchunk42-Windows.pak`. 

Navigate to `%localappdata\Whiskerwood\Saved\mods\` and create a folder for your mod. It should have the same name as the mod folder in the unreal engine project. 

Now paste your `.pak` file into the mod folder.

Rename the `.pak` file to the same name as the mod folder, keeping the `.pak` extension.

Now your mod is installed! You should also make a `<yourmodname>.uplugin` file and fill in the details, but this is not strictly necessary right now.

Here is an example one:
```json
{
    "Name" : "Prettier Path",
    "Description" : "Makes the stone path look prettier!",
    "Version" : "1.0",
    "CreatedBy" : "Buckminsterfullerene"
}
```

## Automating the installation

The above steps are too manual, so let's make a windows .bat script to automate the above (ish - an editor plugin to automate packaging and install of mod will be made eventually so you can do it all from inside the editor).

Let's say your mod is pakchunk-42 and your mod name is `DemoMod`. 

Replace `%localappdata%` and `%pathtoyourtemplateproject%` with the relevant full paths if needs be or make it even better!

```bat
@echo off
mkdir "%localappdata%\Whiskerwood\Saved\mods\DemoMod" 2>nul

copy /Y "%pathtoyourtemplateproject%\Whiskerwood\Windows\Whiskerwood\Content\Paks\pakchunk42-Windows.pak" "%localappdata%\Whiskerwood\Saved\mods\DemoMod\DemoMod.pak"

echo Copy completed.
start "" "steam://rungameid/2489330"
```

To explain:
1. It makes the directory if it doesn't exist yet
2. It copies the pakchunk file to the mods folder while simultaneously renaming it to the mod name
3. It loads the game from steam (if you don't have the game on steam remove this part)

You can add as many as you like here, though if you don't want to run the game with certain mods, you might want to comment out those lines temporarily.

This is my batch script now:

![Automation-1](Docs/Images/Automation-1.png)

So when packaging the mod is done, I run the bat and the game launches with all the mod paks installed!

## Some notes

If you want to run the same or similar logic in both `BP_Startup` and `BP_MapLoad`, it is recommended to create a third blueprint which the first two can spawn. 

### Mod Options

If changing a data table value, you must do it **as soon as possible in `BP_Startup`**, NOT in `BP_MapLoad`, otherwise the **changes may not take effect** - some things load values from the tables at runtime, some things only load values from them once at level initialisation before mods are loaded.

Please **register your mod options inside of `BP_Startup`**. They will still exist when the map loads even if you don't register in `BP_MapLoad`. You could register them in `BP_MapLoad` only, however the player then would not be able to configure the mod options until they enter the level.

When registering mod options, please **put your mod name at the start of Option Id and Option Display Name**. For option id, this is best practice as it massively reduces the possibility of conflicting with other mods. For display name, this will help show the user which mod the option is coming from.

It is recommended to **make variables for storing your Option Ids** because it reduces the chance of human error.

If you want to update your mod to **change the values of an option** (for example, seconds to minutes), you should **change the optionId to a new one**. This is because when an option Id is loaded in-game, the player's selection is loaded in from the previous option value, which would likely cause unintended behaviour. 

## Credits

Project headers are generated by [Suzie](https://github.com/trumank/Suzie) and [jmap_dumper](https://github.com/trumank/jmap). Thank you so much Archengius and trumank for working on these incredible tools!
