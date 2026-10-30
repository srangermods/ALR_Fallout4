#pragma once
#include "pch.h"
#include "cmd.cpp"
#include "PathData.h"

struct WhitelistEntry
{
    int formID;
    std::string plugin;
    std::string filename;
    int value;
};
class PathBuilderParent {
public:
	virtual ~PathBuilderParent() {}
	virtual bool IsImageRD() const = 0; 
};

// Reads ALR.ini, and when generation is enabled lays out the source -> DDS conversion
// in pathData (each output named after its source). All work happens in the constructor.
class PathBuilder : public PathBuilderParent {

private:
	PathDataParent* pathData = NULL;  // not owned
	vector<string> inputFiles;
	string inputPath, outputPath, textureSwapFitMode;
	ostringstream dataPathSS;

	void readInis(),
		findInputFiles(),
		cleanOutputPathFiles(),
		generateInputPaths(),
		correctFiles(),
		generateOutputPaths(),
		findDLLPath(),
		verifyFiles(),
		findImgDir(),
		buildTextureDir();

	std::vector<std::pair<string, float>> loadScreenLights;  // fLoadingMenuSceneLight* overrides from ALR.ini

	bool dImageRD = false,
		dTextureSwapDebugRD = false;

public:
	std::vector<WhitelistEntry> whitelist;
	PathBuilder(PathDataParent& _pathData);
	bool IsImageRD() const override { return dImageRD; }

	// Optional ALR.ini tuning for the runtime texture swap
	bool TextureSwapDebug() const { return dTextureSwapDebugRD; }
	const string& TextureSwapFitMode() const { return textureSwapFitMode; }
	const string& OutputPath() const { return outputPath; }

	// Load-screen light settings from ALR.ini: { "fLoadingMenuSceneLight3DimmerValue", 2.13 }, ...
	const std::vector<std::pair<string, float>>& LoadScreenLights() const { return loadScreenLights; }
	// Output file name used for a source file: "My Pipboy.png" -> "My Pipboy.dds"
	static string TextureSwapFileName(const string& sourceFileName);

};