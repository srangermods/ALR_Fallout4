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

    // Every output is now a fixed 2048x2048 canvas, regardless of the game's
    // actual display resolution or aspect ratio. AR(1,1) + res4k=false resolves
    // to RES(2048, 2048) via the AR(1,1) entry in ARtupleArray (res2kMul = 2048).
    outputAR = AR(1, 1);
    maxDisplayImageData = imageData(outputAR, false);

    if (!std::filesystem::exists(path::to_wstring(pathData->overlayPath))) {
        createOverlay(path::to_wstring(pathData->overlayPath));
    }
    else {
        //_MESSAGE("Overlay already exists, not creating ");
    }

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
        // Background now uses the same fixed 2048x2048 canvas as everything else.
        convert(path::to_wstring(pathData->inputFilePaths.at(0)), path::to_wstring(pathData->backgroundPath));
    }
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

    // Every input is assumed 16:9. Fit it into the fixed 2048x2048 canvas by
    // matching the canvas width and scaling height to preserve 16:9, then
    // center it vertically (letterboxed) since 16:9 doesn't fill a square.
    const long canvasSize = maxDisplayImageData.res.width; // 2048
    const long fitWidth = canvasSize;
    const long fitHeight = static_cast<long>(std::lround(canvasSize * 9.0 / 16.0)); // 1152
    const long offsetX = 0;
    const long offsetY = (canvasSize - fitHeight) / 2; // 448

    ScratchImage resizedImage;
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