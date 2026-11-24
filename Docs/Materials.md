# Materials that work on Mac (and in heat view)

Whiskerwood has a native Mac build, which draws with Metal. The modkit only cooks for Windows, so it only compiles Windows (DirectX) shaders. If your mod has its own Material, that Material's shaders are cooked into your mod for Windows only. On a Mac there is nothing to draw it with, so the game falls back to the engine's default material and your building shows up **flat grey**.

As far as I have seen, only shaders are affected. One of my buildings, cooked with this kit (textures, a Nanite mesh and Blueprints), showed in full colour on a player's Mac once its material was fixed. The game's own textures are byte-for-byte the same in the Mac and Windows builds.

There is no native Linux build. Linux and Steam Deck players run the Windows build through Proton, so they get the Windows shaders and see what Windows players see.

The fix is to not ship any shaders of your own. Make a **Material Instance of one of the game's own materials** and only change its parameters. Your instance then draws with the game's shaders, which the game ships for both Windows and Mac.

There is a bonus: the game's building materials carry the **heat overlay**. If your building uses one of them, it shows up in heat view like the vanilla buildings do. A Material you build yourself doesn't have it, unless you rebuild the game's heat logic in it.

## The rules

1. Create a **Material Instance**, not a Material, and set its **Parent** to one of the game materials in the table below.
2. Only change **texture, scalar and vector parameters**.
   - Do **not** tick anything under *Static Switch Parameters*.
   - Do **not** touch *Material Property Overrides* (blend mode, shading model, two sided and so on).
   - Either one makes a new shader permutation that gets compiled into your mod, for Windows only.
3. Pick a parent whose **usage flags** cover how your mesh is drawn: static mesh, Nanite, instanced by the game, skeletal mesh or Niagara. The game only ships shaders for the usages ticked on each material, and a Material Instance can't add one. The table below lists each parent's usages.
   - The game instances the static mesh components of its grid buildings, unless the component or the actor has the tag `gridactor_no_instancing`. If your part doesn't have that tag, its parent needs the *instanced* usage.
4. Move your own Materials out of your mod folder so they aren't cooked. Then re-save your mod's PrimaryAssetLabel (PAL), so it stops listing them. The check below finds any you missed.
5. Changing parameters at runtime is fine. *Create Dynamic Material Instance* and *Set Scalar Parameter Value on Materials* reuse the parent's shaders.

> [!TIP]
> If you are replacing a material, keep the old asset name and path where you can: move the old one out first, then create the instance in its place. Meshes and Blueprints that point at that path then pick up the new instance with no edits. If one of your Blueprints compares material **names** as text, change those strings too.

## Which parent to use

| What you are drawing | Parent | Usages | Parameters to set |
|---|---|---|---|
| Opaque building, prop or hull with a roughness map | `/Game/Assets/Meshes/Kitbash_new/SM_Bonfire/MI_VividBasis_Bonfire` | static, instanced, Nanite | `Albedio` (the game's spelling), `Normal`, `ORM`. Optionally `SpecularScale` |
| The same, without a roughness map | `/Game/Material/VividBasis/MI_VividBasis_Building` | static, instanced, Nanite | `Albedio`, `Normal`. This version ignores `ORM` |
| Stone ground or floor tile (takes winter snow and a colour tint) | `/Game/Assets/Meshes/Kitbash_new/SM_StoneSlab/MI_VividBasis_Stoneslab` | static, instanced, Nanite | `Albedio`, `Normal`, `ORM`, `Tint` |
| Two-sided opaque part, such as a thin hull seen from inside | `/Game/Assets/Meshes/Kitbash_new/SM_Barge/M_BargeMat` | static, instanced, Nanite | `Albedio`, `Normal`, `ORM` |
| Skinned (skeletal) mesh, such as a creature | `/Game/ThirdParty/GreenwoodFantasyVillage/Materials/MasterMaterials/M_Opaque` | skeletal, static, instanced, Nanite | `Base Color`, `Normal`, `AO(R) Roughness(G) Metallic(B)`. Optionally `Color` (a tint) |
| Additive glow (spirits, magic) | `/Game/ThirdParty/GreenwoodFantasyVillage/Materials/MasterMaterials/M_Fire` | static only (not instanced, not Nanite), Cascade sprites | `Base Color`, `Emission Intensity`. Set `Rows` 1, `Columns` 1 and `Speed` 0 for a still image |
| Unlit single-colour marker | `/Game/Material/Nautical/N_NauticalSymbol_Base` | static, Nanite, spline (not instanced) | `Color` |
| See-through lit overlay (range rings, ghosts) | `/Game/Material/StatusGhosts/M_Ghost_Base` | static, instanced, Nanite | `Param` (the colour; its alpha is ignored), `Opacity` |
| Niagara sprite or ribbon glow | `/Game/ThirdParty/UltraDynamicSky/Materials/Weather/LightningBolt_ParticleMat` | Niagara sprites and ribbons, static | `Texture`. See the note below |

Some notes on these:

- **Start with the Bonfire parent if you can.** So far it is the only one a Mac player has confirmed, on a Nanite mesh. Every parent in the table has Metal shaders in the Mac build, but I haven't seen the others on a Mac yet.
- **How far each row is tested:** in the game on Windows I've seen the Bonfire, `M_Opaque` (skinned), `M_BargeMat` and `LightningBolt_ParticleMat` working, and Bonfire children showing heat view. `MI_VividBasis_Building`, `MI_VividBasis_Stoneslab`, `M_Fire`, `N_NauticalSymbol_Base` and `M_Ghost_Base` I've only checked in the editor so far.
- **`M_Opaque`** has the skeletal usage, but the game never draws it on a skinned mesh itself. My creatures draw fine with it in the game on Windows, but check yours before you rely on it.
  - It has snow turned on, so creatures that use it may get a snow layer in winter.
  - Its `Roughness` scalar made no visible difference for me. Use the G channel instead.
  - If you have no normal map, use `/Engine/EngineMaterials/FlatNormal`, which is in the game.
- **`M_Fire`** and **`N_NauticalSymbol_Base`** have no instanced usage, and `M_Fire` has no Nanite usage either. Use them on a plain mesh with Nanite off, on a part the game doesn't instance.
- **`M_Ghost_Base`** is lit, so it gets lighter and darker with the sun.

### ORM textures

`ORM` packs three maps into one texture. Going by the names of the game's own textures (`*_OcclusionRoughnessMetallic`), the layout is **R = ambient occlusion, G = roughness, B = metallic**. If you only have a roughness map, put it in G, fill R with white and B with black.

The shader uses the value it reads after the texture is decoded, so the sRGB box matters:

- The Bonfire's and the stone slab's own ORM textures are imported with **sRGB on and Default compression**. I import mine the same way.
- With sRGB on, the stored pixel isn't the value the shader reads. A roughness of 0.5 is stored as 188, not 128. So if your roughness map was made for a linear texture, convert each channel to sRGB before you import it. Otherwise the surface comes out shinier than you meant.
- Not every vanilla ORM is sRGB (the wooden bridge's is linear), so either setting works as long as the stored values match it.

Normal maps keep the usual settings: sRGB off, `Normalmap` compression.

### Matching your old look

- **Brightness:** a plain instance of the Bonfire parent renders a little darker than a default-lit Material of your own. In my renders it was between a few percent and about 15 percent darker.
  - `Metalic Override` and `Roughness Override` (the game's spellings) do nothing in that parent.
  - `SpecularScale` does. I used values from 1.5 to 4, and 7 once, for a dark part that sits mostly in shade.
- **Very rough surfaces:** `SpecularScale` barely helps there. If your parent has its tint switched on, `Tint` multiplies the base colour, and values above 1 brighten it. `MI_VividBasis_Stoneslab` has the tint on; the Bonfire doesn't. `TintScaler` made no difference for me.
- All the brightness numbers here come from renders in the editor, so give your result a look in the game too.
- **Lost effects:** tricks in your own material graph don't come along. That includes World Position Offset, Fresnel, panners, glows divided by eye adaptation and anything driven by `Time`.
  - Bake what you can into the textures. For example, I painted roof shading straight into the base colour.
  - Move movement into Blueprint. For a spinning wheel, make the wheel its own Movable component and set its rotation from Blueprint. I use a looping timer that sets the relative rotation from *Get Game Time Since Creation*, so the wheel pauses with the game.
  - Mind the sign. A positive `Roll` turns the opposite way to a positive angle in the material's *RotateAboutAxis*.
  - Give the moving part the tag `gridactor_no_instancing`, so the game doesn't fold it into its instanced meshes. The game does this itself: the wheel of the mouse wheel and the saw of the lumber mill are their own components with that tag. Tagging the whole actor works too.
- **Niagara glow:** `LightningBolt_ParticleMat` draws about `43.7 x saturate(16.5 x Texture x Particle Color)`. In its useful range that multiplies your texture by about 700, and above that it stops getting brighter.
  - So a glow texture needs very dark values. I store the mask divided by 721, which comes out as sRGB codes 0 to 5.
  - Import it uncompressed (`UserInterface2D (RGBA)`), because normal compression destroys values that small.
  - The texture's alpha is ignored. The particle colour's RGB tints the glow, and its alpha scales it (alpha 0 draws nothing).
  - It dims when the sky's cloud bottom (the Ultra Dynamic Sky `Cloud Bottom Altitude`) is far below the particle.

## Heat view

The game's building material, `M_VividBasis`, has a static switch called `AllowThermalVision`. It is on in every VividBasis version I checked: over 60 of the game's instances, including all the VividBasis parents above. With it on, the material reads the game's heat texture, its heat colour tables and the `MP_TerrainMetaDataParameters` collection, which holds the heat-view toggle.

A Material Instance inherits the switch from its parent. So any part drawn with the Bonfire, Building, Stoneslab or BargeMat parent should show heat like a vanilla building, with no extra work. `M_Opaque` and the game's creature materials read the same heat inputs.

- The parents above have `UseObjectHeat` off, so they take their colour from the world's heat map at that spot, like the Bonfire does. Some vanilla walls and furnaces have `UseObjectHeat` on. I haven't looked into what that one needs.
- `M_Fire`, `N_NauticalSymbol_Base`, `M_Ghost_Base` and `LightningBolt_ParticleMat` have no heat overlay, so parts drawn with them look the same in heat view.

## Check before you upload

Cooking (*Cook & Install* from Mod Tools, or *Platforms > Windows > Package Project*) leaves your cooked files in `Saved\Cooked\Windows\Whiskerwood\Content\Mods\<YourMod>`. Search them for the names of the Windows shader formats. In PowerShell, from the project folder (replace `MyMod`):

```powershell
Get-ChildItem "Saved\Cooked\Windows\Whiskerwood\Content\Mods\MyMod" -Recurse -Include *.uexp | Select-String -Pattern PCD3D_SM -SimpleMatch -List | Select-Object Filename
```

- **No output:** your mod has no shaders of its own.
- **A file is listed:** that asset carries its own shaders and will be grey on Mac. It is either your own Material or an instance with a switch or property override.
- **A file you already moved out is listed:** it is left over from an earlier cook. Move that cooked mod folder out of the way and cook again.

The file size is a quick hint too. A parameter-only instance cooks to a `.uexp` of a few hundred bytes at most, while one with its own shaders is many kilobytes.

This check can't tell whether your parent has shaders for your kind of mesh. That is what the usage column in the table is for.

## If a Mac player still sees grey after your update

First ask them to fully quit and restart Steam. In the one case I've had, the player's Steam hadn't downloaded the update until it was restarted.

They can check which version they have. Your mod's `.uplugin` file is in `~/Library/Application Support/Steam/steamapps/workshop/content/2489330/<workshop id>/` (or the same folder in another Steam library, if they use one). Opening it in a text editor shows the `Version` line.

## Worked example: PrettierPath

The kit's PrettierPath example puts its cobblestone mesh, `SM_PrettierPath` (a Nanite mesh), on the tile of the stone path and on two floor slabs of the rail station. Three of its assets carried Windows-only shaders, so on a Mac it would draw grey:

- `M_Cobblestone`, the mesh's own Material;
- `M_Default`, a glTF import material;
- `MI_Cobblestone`, an instance of `MI_Default_Opaque` (itself an instance of `M_Default`) with static switches set, which gives it a shader map of its own.

Before the change, `BP_Startup` put `SM_PrettierPath` on the path tile and on both station slabs. `BP_MapLoad` then set `MI_Cobblestone` on each stone path as it loads or is built. The station slabs kept the mesh's own `M_Cobblestone`.

This is how I converted it.

1. **Find what the game draws there.** The stone path is `FastFloor`. Its tile component (`SM_Bridge_Wooden_01_A3`) uses `SM_StoneSlab` with `MI_VividBasis_Stoneslab`, and so do the two station slabs the mod replaces (`LegRight7` and `LegRight9` in `Rail_Station`). None of them has the `gridactor_no_instancing` tag, so the game instances them. The parent has the instanced and Nanite usages. So `/Game/Assets/Meshes/Kitbash_new/SM_StoneSlab/MI_VividBasis_Stoneslab` is a parent the game already draws on exactly these components.
2. **Replace the instance, keep its name.** I replaced the old `MI_Cobblestone` with a fresh Material Instance of that parent, at the same path. `BP_MapLoad`, `BP_Startup` and `SM_CobblestoneSlab` point at that path, so they picked it up without any edits. It only sets `Albedio`, `Normal`, `ORM` and `Tint`. Nothing is ticked under *Static Switch Parameters* or *Material Property Overrides*.
3. **Point the mesh at it.** I set `SM_PrettierPath`'s material slot to the new `MI_Cobblestone`, instead of `M_Cobblestone`.
4. **Fix the ORM texture.** The old ORM was imported as linear (sRGB off). I converted every channel to its sRGB value and imported it with sRGB on, like the stone slab's own ORM, so the shader still reads the same numbers (within 0.004).
5. **Match the brightness.** I compared kit renders against the old path tiles:
   - With the parent's own `Tint`, the path came out warm yellow and about 21% darker.
   - A white `Tint` removed the yellow, but the path was still about 19% darker.
   - `SpecularScale` hardly helped on such a rough surface: even 4 only got it to about 14% darker.
   - `Tint` multiplies the base colour, so values above 1 brighten it. 1.45 came out about 2% darker, and with `Tint` = (1.6, 1.6, 1.6, 1) my renders came out within 2% of the old path tiles.
6. **Clean up.** I moved `M_Cobblestone`, `M_Default` and `MI_Default_Opaque` out of the project and re-saved `PAL_PrettierPath`. The mod no longer needs the InterchangeAssets glTF material functions.
7. **Check.** After cooking, the PrettierPath cooked folder has no `PCD3D_SM` string, and the cooked `MI_Cobblestone.uexp` is 157 bytes.

What changed:

- The path tiles and the station slabs now draw the same instance. It comes from the mesh itself, so the look no longer depends on `BP_MapLoad`'s swap reaching every tile.
- Against the old path tiles, the new look is within 2% in my renders, with slightly darker grout.
- The station slabs used `M_Cobblestone`, which rendered about 6% darker than the path tiles, so they are now about 7% brighter.
- `M_Cobblestone` also added a soft glow (its base colour times 0.5, divided by the eye adaptation). That doesn't come along, so the station slabs no longer glow at night.
- The new parent has the game's snow and heat-view switches on, and the instance inherits them. The path should now take winter snow and show in heat view, like the vanilla stone path.

I checked this conversion in the editor (renders and the cooked files), not in the game, so it's worth a look there before the next release of the example.
