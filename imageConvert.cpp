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
    sizeImageData = imageData(pathData->outputWidth, pathData->outputHeight);
    maxDisplayImageData = imageData(sizeImageData.ar, sizeImageData.arMul, sizeImageData.resMul);

    createOverlay(path::to_wstring(pathData->overlayPath));
    // Kicks off the parallel thread team

    #pragma omp parallel
    {
        // Set up COM Multi-Threaded Apartment for this specific worker thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        // Every thread gets its own unique pre-allocated canvases on its own stack frame
        ScratchImage threadCanvas2k, threadCanvas4k;
        threadCanvas2k.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 2048, 2048, 1, 0, CP_FLAGS_NONE);
        threadCanvas4k.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4096, 4096, 1, 0, CP_FLAGS_NONE);

        // Safely distribute the iterations across the initialized threads
        #pragma omp for
        for (int i = 0; i < MAX_INPUTS; i++) {
            std::wstring outputFilePathW = path::to_wstring(pathData->outputPaths.at(i));
            std::string filename = std::filesystem::path(outputFilePathW).filename().string();

            if (std::find(whitelistFileNames.begin(), whitelistFileNames.end(), filename) != whitelistFileNames.end()) {
                continue;
            }

            // Pass the thread-safe local canvases
            convert(path::to_wstring(pathData->inputFilePaths.at(i)), outputFilePathW, threadCanvas2k, threadCanvas4k);
        }

        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }

    // Background replacement and overlay happen sequentially after threads join
    //if (pathData->backgroundReplace) {
    //    maxDisplayImageData = imageData(AR(2, 1), sizeImageData.res4k);
    //    outputAR = AR(2, 1);
    //    convertBackgroundReplace(path::to_wstring(pathData->inputFilePaths.at(0)), path::to_wstring(pathData->backgroundPath));
    //}
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

void ImageConvert::convert(wstring _inputFilePath, wstring _outputFilePath, ScratchImage& reusable2kCanvas, 
    ScratchImage& reusable4kCanvas) {

    TexMetadata inImageInfo;
    ScratchImage inImage;

    // 1. Disk I/O: Load file
    if (path::getExtension(_inputFilePath) == L"dds") {
        message::checkForError(LoadFromDDSFile(_inputFilePath.c_str(), DDS_FLAGS_NONE, &inImageInfo, inImage));
    } else {
        // Add WIC_FLAGS_IGNORE_SRGB here to prevent the dark shift!
        message::checkForError(LoadFromWICFile(_inputFilePath.c_str(), WIC_FLAGS_IGNORE_SRGB, &inImageInfo, inImage));
    }

    const Image* workingImage = inImage.GetImage(0, 0, 0);
    ScratchImage deCompressedImage;

    // 2. Decompress only if needed
    if (IsCompressed(inImageInfo.format)) {
        message::checkForError(Decompress(*workingImage, DXGI_FORMAT_R8G8B8A8_UNORM, deCompressedImage));
        workingImage = deCompressedImage.GetImage(0, 0, 0);
        inImageInfo = deCompressedImage.GetMetadata();
    }

// 3. Size Calculations (Aspect Ratio Aware)
    const bool isFourK = (inImageInfo.width >= 3840 || inImageInfo.height >= 2160);
    const long canvasSize = isFourK ? 4096 : 2048;

    // Base Fallout 4 safe-zone boundaries (16:9 box)
    long maxOffsetX = canvasSize * 64 / 2048;
    long maxOffsetY = canvasSize * 484 / 2048;
    long maxFitWidth = canvasSize - 2 * maxOffsetX;
    long maxFitHeight = canvasSize - 2 * maxOffsetY;

    // Calculate aspect ratios
    double inputAspectRatio = static_cast<double>(inImageInfo.width) / inImageInfo.height;
    double targetAspectRatio = static_cast<double>(maxFitWidth) / maxFitHeight;

    long fitWidth = maxFitWidth;
    long fitHeight = maxFitHeight;
    long offsetX = maxOffsetX;
    long offsetY = maxOffsetY;

    if (inputAspectRatio > targetAspectRatio) {
        // Image is wider than 16:9 (e.g., 21:9, 32:9 ultra-wide)
        // Fit to width, scale down height, and center vertically
        fitWidth = maxFitWidth;
        fitHeight = static_cast<long>(maxFitWidth / inputAspectRatio);
        offsetY = maxOffsetY + (maxFitHeight - fitHeight) / 2;
    } else {
        // Image is taller than 16:9 (e.g., 4:3, 1:1 portrait)
        // Fit to height, scale down width, and center horizontally
        fitHeight = maxFitHeight;
        fitWidth = static_cast<long>(maxFitHeight * inputAspectRatio);
        offsetX = maxOffsetX + (maxFitWidth - fitWidth) / 2;
    }

    // 4. Resize
    ScratchImage resizedImage;
    message::checkForError(Resize(*workingImage, fitWidth, fitHeight, TEX_FILTER_DEFAULT, resizedImage));

    // 5. Select our pre-allocated canvas (Zero allocation cost!)
    ScratchImage& finalCanvas = isFourK ? reusable4kCanvas : reusable2kCanvas;

    // 6. Blit the image into the pre-allocated memory using dynamic offsets
    Rect r0(0, 0, fitWidth, fitHeight);
    message::checkForError(CopyRectangle(
        *resizedImage.GetImage(0, 0, 0), 
        r0, 
        *finalCanvas.GetImage(0, 0, 0), 
        TEX_FILTER_DEFAULT, 
        offsetX, 
        offsetY
    ));

    // 7. Disk I/O: Save
    message::checkForError(SaveToDDSFile(*finalCanvas.GetImage(0, 0, 0), DDS_FLAGS_NONE, _outputFilePath.c_str()));
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