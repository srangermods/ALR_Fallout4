![testbuild](https://github.com/ianpatt/f4se/workflows/testbuild/badge.svg)

## Building (oldgen via cmake)

```
git clone https://github.com/ianpatt/common
git clone https://github.com/ianpatt/f4se
cmake -B common/build -S common -DCMAKE_INSTALL_PREFIX=extern common
cmake --build common/build --config Release --target install
cmake -B f4se/build -S f4se -DCMAKE_INSTALL_PREFIX=extern f4se
cmake --build f4se/build --config Release

cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Changes 

Works for Old gen ( v163) and AE (1191)

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
