# Tools to help you make mods

## UE4SS

### Installation

If you don't have UE4SS installed already:
1. Grab the `UE4SS_v3.0.1-xx.zip` file from [the bottom of this releases page](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest) under "Assets"
2. Navigate to your game directory:
`\steamapps\common\Wiskerwood\Wiskerwood\Binaries\Win64\`
3. Extract all files from the zip directly into the Win64 folder
4. Create a folder named `UE4SS_Signatures` inside `ue4ss/`, then download and place the 2 required `.lua` signature files (`FText_Constructor.lua` and `StaticConstructObject.lua`) from the [RE-UE4SS GitHub repository (`Assets/CustomGameConfigs/Whiskerwood/UE4SS_Signatures`)](https://github.com/UE4SS-RE/RE-UE4SS) into it
5. Launch game

If you get window flickering issues, disable discord overlay, they broke something recently.

### Uninstallation/disable 

If you want to uninstall or disable UE4SS:
1. Navigate to game directory
`\steamapps\common\Wiskerwood\Wiskerwood\Binaries\Win64\`
2. Delete or rename file `dwmapi.dll`

### Usage

Here are some docs to help you get started with UE4SS:
- [UE4SS Docs](https://docs.ue4ss.com/dev/)
- [UE4SS Live Viewer](https://docs.ue4ss.com/dev/feature-overview/live-view.html)

One thing that is very useful with UE4SS is its ability to dump information about the game's classes. You can do a C++ header dump which provides lots of juicy info about the game which you can access from the template.

## FModel

Download FModel from here: https://fmodel.app/

FModel is a fundamental tool for researching how to make a mod and how the game works. You typically need it open alongside Unreal Engine to cross reference and search for stuff.

Please follow these docs to get started with FModel:
- [Setting up FModel](https://github.com/Dmgvol/UE_Modding/blob/main/TheBasics/ExportingFModel.md)
- [Using FModel](https://github.com/Dmgvol/UE_Modding/blob/main/TheBasics/UsingFModel.md)

Please note that you can either [dump your own](https://github.com/Dmgvol/UE_Modding/blob/main/TheBasics/Extractingusmap.md#step-3-extract-usmap-file) `.usmap` file using UE4SS, or you can get the `.usmap` file from the `Automation` folder in this project repository.
