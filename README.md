

## Changes 

Works for OG, NG, AE (thanks to commonlibf4 from Dear-Modding-FO4)

-Re-implemented/restored source code from original repo this was forked from

-Fixed bugs, made image generation more random

-Much more disk space efficient now.

-Allows "Bring Your Own Textures", you can place your own dds textures into Textures\Interface\ALR_Backgrounds if you turn off image generation

-ESL flagged ALR.esp

-No longer requires DLC or freebie CCS

-Dynamically Replaces all Loading Screens (including mod ones)

-Whitelist functionality

-No cap on how many images can be used.

-removed LoadingMenu.swf from the mod so the overlay text will show by default (option to hide it in FOMOD)



### Revamped ALR.INI

-cleaned up the INI so it uses industry standard INI syntax, as opposed to commas, semicolons, and 0s and 1s

-Removed High FPS Physics config logic, was too intrusive. Just make sure in your High FPS Physics Fix Config that `DisableBlackLoadingScreens=true`

-use `EnableLoadingScreenGeneration = false` to turn on or off random loading screen image generation from ALR_Image_DIR


-`ImageSourcePath` for optional custom image soure path. Default path to images now is F4SE/Plugins/ALR_Image_Dir in your data directory

-Whitelist Feature 

[Whitelist]
;To use the Whitelist, do formID:PluginName.esp = #
;formID is the loadscreen record you want to replace, do not use load order prefix 
;# is the dds file you want the load screen to use, in Textures\Interface\ALR_Backgrounds in your overwrite directory
;vanilla pipboy loading screen will use  Textures\Interface\ALR_Backgrounds\1.dds
;0x21E50:Fallout4.esm = 1
;0x1234:SomeMod.esp = 0
;0xABCD:AnotherMod.esl = 2

### Install

-Download from the release section in the repo, install it like any other mod in your mod manager

-Configure your ALR.ini setting correctly in F4SE/Plugins

-add images to F4SE/Plugins/ALR_Image_Dir 

-Image generation will now run  when you launch the game, depending on your ALR.ini configuration