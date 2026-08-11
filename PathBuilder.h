#pragma once
#include "pch.h"
#include "cmd.cpp"
#include "PathData.h"

struct WhitelistEntry
{
    int formID;
    std::string plugin;
    int value;
};
class PathBuilderParent {
public:
	virtual ~PathBuilderParent() {}
	virtual bool IsImageRD() const = 0; 
	virtual bool RandomizeModLoadScreens() const = 0;
};

class PathBuilder : public PathBuilderParent {

private:
	PathDataParent* pathData = NULL;
	vector<string> inputFiles;
	string inputPath, overlayPath, outputPath, backgroundPath, prefPath;
	ostringstream dataPathSS, prefPathSS;

	void readInis(),
		findInputFiles(),
		cleanOutputPathFiles(),
		generateInputPaths(),
		correctFiles(),
		generateOutputPaths(),
		firstTimeCheck(),
		findDLLPath(),
		findPrefPath(),
		rebuildIni(),
		verifyFiles(),
		findImgDir();

	void buildTextureDir();

	bool verifyImages(),
		dImageRD,
		dRandomizeModLoadScreensRD,
		dOverlayRD,
		rebuildIniFlag;

public:
	std::vector<WhitelistEntry> whitelist;
	PathBuilder(PathDataParent& _pathData);
	~PathBuilder();
	bool IsImageRD() const override { return dImageRD; }
	int countOutputPathFiles();
	bool RandomizeModLoadScreens() const override {return dRandomizeModLoadScreensRD; }

};