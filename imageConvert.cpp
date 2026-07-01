#include "pch.h"
#include "ImageConvert.h"
#include <cmath>

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return std::string();

    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], sizeNeeded, nullptr, nullptr);

    // Remove the null terminator
    if (!result.empty() && result.back() == '\0')
        result.pop_back();

    return result;
}

ImageConvert::ImageConvert(PathDataParent& _pathData, const std::vector<std::string>& whitelistFileNames)
    : pathData(&_pathData), whitelistFileNames(whitelistFileNames)
{
    message::checkForError(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

    sizeImageData = imageData(pathData->outputWidth, pathData->outputHeight);
	maxDisplayImageData = imageData(sizeImageData.ar, sizeImageData.arMul, sizeImageData.resMul);
	

    createOverlay(path::to_wstring(pathData->overlayPath));

    for (int i = 0; i < MAX_INPUTS; i++) {
        std::wstring outputFilePathW = path::to_wstring(pathData->outputPaths.at(i));
        std::string filename = std::filesystem::path(outputFilePathW).filename().string();

        // Skip conversion if filename is in whitelist
        if (std::find(whitelistFileNames.begin(), whitelistFileNames.end(), filename) != whitelistFileNames.end()) {
            //_MESSAGE("Skipping conversion for whitelisted file: %s", filename.c_str());
            continue;
        }
        convert(path::to_wstring(pathData->inputFilePaths.at(i)), path::to_wstring(pathData->outputPaths.at(i)));
    }

    if (pathData->backgroundReplace) {
        maxDisplayImageData = imageData(AR(2, 1), sizeImageData.res4k);
        outputAR = AR(2, 1);
        convertBackgroundReplace(path::to_wstring(pathData->inputFilePaths.at(0)), path::to_wstring(pathData->backgroundPath));
    }
}

void ImageConvert::convertBackgroundReplace(wstring _inputFilePath, wstring _outputFilePath) {

	TexMetadata inImageInfo;
	ScratchImage inImage, outImage, deCompressedImage;
	imageData inImageData, outImageData;

	if (path::getExtension(_inputFilePath) == L"dds") 
		message::checkForError(LoadFromDDSFile(_inputFilePath.c_str(), DDS_FLAGS_NONE, &inImageInfo, inImage));
	else 
		message::checkForError(LoadFromWICFile(_inputFilePath.c_str(), WIC_FLAGS_NONE, &inImageInfo, inImage));

	if (IsCompressed(inImage.GetMetadata().format)) {
		////_MESSAGE("image is compressed");
		//DXGI_FORMAT_R8G8B8A8_UNORM
	   	//ScratchImage decompressedImage;
	    // Decompress to a specific uncompressed format like DXGI_FORMAT_R8G8B8A8_UNORM
	    message::checkForError(Decompress(*inImage.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, deCompressedImage));
	    inImage = (std::move(deCompressedImage));
	}
	inImageData = imageData(inImage.GetMetadata().width, inImage.GetMetadata().height);
	
	if (!(inImageData.ar == maxDisplayImageData.ar) || inImageData.resMul > maxDisplayImageData.resMul) {
		////_MESSAGE("Right before Resize call ");
		////_MESSAGE("%d width", maxDisplayImageData.res.width);
		////_MESSAGE("%d height", maxDisplayImageData.res.height);
		ScratchImage tmpImage;
		message::checkForError(Resize(*inImage.GetImage(0, 0, 0), maxDisplayImageData.res.width, maxDisplayImageData.res.height, TEX_FILTER_DEFAULT, tmpImage));
		////_MESSAGE("Right after Resize call ");
		//inImage.reset();  // is this needed
		inImage = (std::move(tmpImage));
		inImageData = imageData(maxDisplayImageData.res.width, maxDisplayImageData.res.height);
	}
	
	outImageData = imageData(inImageData.arMul, inImageData.resMul, inImageData.off, outputAR);
	message::checkForError(outImage.Initialize2D(inImage.GetMetadata().format, outImageData.res.width, outImageData.res.height, 1, 0, CP_FLAGS_NONE));

	Rect r0(0, 0, inImageData.res.width, inImageData.res.height);
	message::checkForError(CopyRectangle(*inImage.GetImage(0, 0, 0), r0, *outImage.GetImage(0, 0, 0), TEX_FILTER_DEFAULT, outImageData.off.width, outImageData.off.height));

	message::checkForError(SaveToDDSFile(*outImage.GetImage(0, 0, 0), DDS_FLAGS_NONE, _outputFilePath.c_str()));
}

ImageConvert::~ImageConvert()
{
    delete pathData;
}

void ImageConvert::createOverlay(wstring _outputFilePath)
{
    ScratchImage overlayImage;
    message::checkForError(overlayImage.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, maxDisplayImageData.res.width, maxDisplayImageData.res.height, 1, 0, CP_FLAGS_NONE));
    message::checkForError(SaveToDDSFile(*overlayImage.GetImage(0, 0, 0), DDS_FLAGS_NONE, _outputFilePath.c_str()));
}

void ImageConvert::convert(wstring _inputFilePath, wstring _outputFilePath) {

    TexMetadata inImageInfo;
    ScratchImage inImage, outImage, deCompressedImage;

    if (path::getExtension(_inputFilePath) == L"dds")
        message::checkForError(LoadFromDDSFile(_inputFilePath.c_str(), DDS_FLAGS_NONE, &inImageInfo, inImage));
    else
        message::checkForError(LoadFromWICFile(_inputFilePath.c_str(), WIC_FLAGS_NONE, &inImageInfo, inImage));

    if (IsCompressed(inImage.GetMetadata().format)) {
        message::checkForError(Decompress(*inImage.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, deCompressedImage));
        inImage = std::move(deCompressedImage);
    }

    // Detect a 4K-resolution input and bump the output canvas up accordingly
    // so quality isn't lost by downscaling into a 2K canvas.
    const auto& meta = inImage.GetMetadata();
    const bool isFourK = (meta.width == 3840 && meta.height == 2160);
    const long canvasSize = isFourK ? 4096 : 2048;

    const double srcWidth = static_cast<double>(meta.width);
    const double srcHeight = static_cast<double>(meta.height);
    const double srcRatio = srcWidth / srcHeight;
    const double ultrawideThreshold = 16.0 / 9.0;

    ScratchImage resizedImage;
    long fitWidth, fitHeight, offsetX, offsetY;

	    offsetX = canvasSize * 64 / 2048;
	    offsetY = canvasSize * 484 / 2048;
	    fitWidth = canvasSize - 2 * offsetX;
	    fitHeight = canvasSize - 2 * offsetY;


    message::checkForError(Resize(*inImage.GetImage(0, 0, 0), fitWidth, fitHeight, TEX_FILTER_DEFAULT, resizedImage));

    message::checkForError(outImage.Initialize2D(inImage.GetMetadata().format, canvasSize, canvasSize, 1, 0, CP_FLAGS_NONE));

    Rect r0(0, 0, fitWidth, fitHeight);
    message::checkForError(CopyRectangle(*resizedImage.GetImage(0, 0, 0), r0, *outImage.GetImage(0, 0, 0), TEX_FILTER_DEFAULT, offsetX, offsetY));

    message::checkForError(SaveToDDSFile(*outImage.GetImage(0, 0, 0), DDS_FLAGS_NONE, _outputFilePath.c_str()));
}

AR ClosestSupportedAR(short _width, short _height)
{
    double targetRatio = static_cast<double>(_width) / static_cast<double>(_height);
    AR best = AR(16, 9);
    double bestDiff = (std::numeric_limits<double>::max)();
    for (auto& pair : ARtupleArray) {
        const AR& candidate = get<0>(pair);
        double candidateRatio = static_cast<double>(candidate.width) / static_cast<double>(candidate.height);
        double diff = std::abs(candidateRatio - targetRatio);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = candidate;
        }
    }
    return best;
}

AR_MUL::AR_MUL(AR _ar)
{
    for(auto& pair : ARtupleArray)
        if (get<0>(pair) == _ar) {
            res2kMul = get<1>(pair).res2kMul;
            res4kMul = get<1>(pair).res4kMul;
            break;
        }
}

OFF_CON::OFF_CON(AR _ar)
{
    for (auto& pair : ARtupleArray)
        if (get<0>(pair) == _ar) {
            off2k = get<2>(pair).off2k;
            off4k = get<2>(pair).off4k;
            break;
        }
};