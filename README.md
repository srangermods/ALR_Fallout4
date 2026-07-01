
##  Build
```
xmake build
```

## Changes 

Works for OG, NG, AE (thanks to commonlibf4 from Dear-Modding-FO4)

-Re-implemented/restored source code from original repo this was forked from

-Fixed bugs, made image generation more random

-ESL flagged ALR - ALL DLC.esp plugin

-Added loading screen replacers for all loading screens + the free CCs given in the NG update (this is a new ESL-flagged plugin, CC_ALR.esp)

-removed LoadingMenu.swf from the mod so the overlay text will show by default.



### Revamped ALR.INI

-cleaned up the INI so it uses industry standard INI syntax, as opposed to commas, semicolons, and 0s and 1s

-Removed High FPS Physics config logic, was too intrusive. Just make sure in your High FPS Physics Fix Config that `DisableBlackLoadingScreens=true`

-use `EnableLoadingScreenGeneration = false` to turn on or off random loading screen image generation from ALR_Image_DIR

-keep this setting default `EnableOverrideDefaultBackgroundImage = false`

-Default path to images now is F4SE/Plugins/ALR_Image_Dir in your data directory

-added WhitelistImageFilesFromRegeneration in ALR.ini, lets you whitelist what loading screen images to regenerate.  Each Loading screen is mapped to a number, so you can whitelist an image number so that specific image mapping always corresponds to the loading screen. For example, if you like having an institute-related image always mapped to an institute related loading screen, you can add the number to the whitelist. The whitelist is delimited by commas. Here's how to use it:

-WhitelistImageFilesFromRegeneration = 0,1,3,11,12,13,23,29,31,35,36,37,40

### Install

-Download from the release section in the repo, install it like any other mod in your mod manager

-Configure your ALR.ini setting correctly in F4SE/Plugins

-add images to F4SE/Plugins/ALR_Image_Dir (preferabbly .dds files and in 16:9 aspect ratio)

-Image generation will now run  when you launch the game, depending on your ALR.ini configuration