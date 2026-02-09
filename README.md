![testbuild](https://github.com/ianpatt/f4se/workflows/testbuild/badge.svg)

## Building

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
Currently only works for FO4 Old-Gen

-Re-implemented/restored source code from original repo this was forked from
-Fixed bugs, made image generation more random
-Removed High FPS Physics config logic, was too intrusive. 
-ESL flagged ALR - ALL DLC.esp plugin
-Added loading screen replacers for all loading screens + the free CCs given in the NG update (this is a new ESL-flagged plugin, CC_ALR.esp)
-Default path to images now is F4SE/Plugins/ALR_Image_Dir in your data directory
-added WhitelistImageFilesFromRegeneration in ALR.ini, lets you whitelist what loading screen images to regenerate.  Each Loading screen is mapped to a number, so you can whitelist an image number so that specific image mapping always corresponds to the loading screen. For example, if you like having an institute-related image always mapped to an institute related loading screen, you can add the number to the whitelist. The whitelist is delimited by commas. Here's how to use it:
WhitelistImageFilesFromRegeneration = 0,1,3,11,12,13,23,29,31,35,36,37,40