#pragma once
#include "pch.h"
#include "cmd.cpp"

class PathDataParent {

public:
	vector<string> inputFilePaths;  // source images
	vector<string> outputPaths;     // matching DDS paths in Textures\Interface\ALR_Backgrounds
	string iniPath;
};

class PathData : public PathDataParent {

public:
	PathData() {};
};
