#include "pch.h"
#include "ImageConvert.h"

ImageConvert::ImageConvert(PathDataParent& _pathData)
    : pathData(&_pathData)
{
    // Kicks off the parallel thread team
    #pragma omp parallel
    {
        // Set up COM Multi-Threaded Apartment for this specific worker thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        // Safely distribute the iterations across the initialized threads
        #pragma omp for
        for (int i = 0; i < static_cast<int>(pathData->inputFilePaths.size()); i++) {
            // Empty slot: a whitelist value larger than the number of images
            if (pathData->inputFilePaths.at(i).empty()) {
                continue;
            }
            convert(path::to_wstring(pathData->inputFilePaths.at(i)), path::to_wstring(pathData->outputPaths.at(i)));
        }

        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
}


void ImageConvert::convert(const wstring& _inputFilePath, const wstring& _outputFilePath)
{
    TexMetadata inImageInfo;
    ScratchImage inImage;

    spdlog::info("converting input image: {}", std::filesystem::path(_inputFilePath).string());
    if (path::getExtension(_inputFilePath) == L"dds") {
        message::checkForError(LoadFromDDSFile(_inputFilePath.c_str(), DDS_FLAGS_NONE, &inImageInfo, inImage));
    } else {
        message::checkForError(LoadFromWICFile(_inputFilePath.c_str(), WIC_FLAGS_IGNORE_SRGB, &inImageInfo, inImage));
    }

    ScratchImage scratch;
    const Image* workingImage = inImage.GetImage(0, 0, 0);

    if (IsCompressed(workingImage->format)) {
        message::checkForError(Decompress(*workingImage, DXGI_FORMAT_R8G8B8A8_UNORM, scratch));
        inImage = std::move(scratch);
        workingImage = inImage.GetImage(0, 0, 0);
    }

    // Normalize to R8G8B8A8 (grayscale, 16-bit PNGs, BGRA DDS etc.)
    if (workingImage->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        message::checkForError(DirectX::Convert(*workingImage, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, scratch));
        inImage = std::move(scratch);
        workingImage = inImage.GetImage(0, 0, 0);
    }

    // D3D11 can't create a texture larger than 16384 on a side
    constexpr std::size_t kMaxDim = 16384;
    if (workingImage->width > kMaxDim || workingImage->height > kMaxDim) {
        const double scale = static_cast<double>(kMaxDim) / static_cast<double>((std::max)(workingImage->width, workingImage->height));
        const auto   w = (std::max)(std::size_t{ 1 }, static_cast<std::size_t>(workingImage->width * scale));
        const auto   h = (std::max)(std::size_t{ 1 }, static_cast<std::size_t>(workingImage->height * scale));
        message::checkForError(Resize(*workingImage, w, h, TEX_FILTER_DEFAULT, scratch));
        inImage = std::move(scratch);
        workingImage = inImage.GetImage(0, 0, 0);
    }

    message::checkForError(SaveToDDSFile(*workingImage, DDS_FLAGS_NONE, _outputFilePath.c_str()));
}
