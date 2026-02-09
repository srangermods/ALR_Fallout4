#pragma once
#include "pch.h"
#include "cmd.cpp"
#include "PathData.h"

class PathBuilderParent {
public:
	virtual ~PathBuilderParent() {}
	virtual bool IsImageRD() const = 0;  // New interface
};

class PathBuilder : public PathBuilderParent {

private:
	PathDataParent* pathData = NULL;
	vector<string> inputFiles;
	string inputPath, overlayPath, outputPath, backgroundPath, prefPath;
	ostringstream dataPathSS, prefPathSS;

	void readInis(),
		findInputFiles(),
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
		dOverlayRD,
		rebuildIniFlag;

public:
	std::vector<std::string> whitelistFileNames;
	PathBuilder(PathDataParent& _pathData);
	~PathBuilder();
	bool IsImageRD() const override { return dImageRD; }  // Implementation
};