#pragma once
#include "pch.h"
#include "PathData.h"
#include "cmd.cpp"

class ImageConvertParent {
public:
	// Virtual so `delete` through the base pointer (main.cpp) destroys the full object
	virtual ~ImageConvertParent() = default;
};

class ImageConvert : public ImageConvertParent {
public:
	explicit ImageConvert(PathDataParent& _pathData);

private:
	void convert(const wstring& _inputFilePath, const wstring& _outputFilePath);

	PathDataParent* pathData = nullptr;  // not owned
};
