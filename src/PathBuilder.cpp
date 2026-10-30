#include "pch.h"
#include "PathBuilder.h"
#include <cstring>
#include <unordered_set>

PathBuilder::PathBuilder(PathDataParent& _pathData) : pathData(&_pathData)
{
	findDLLPath();
	readInis();

	if (dImageRD) {
		cleanOutputPathFiles();
		buildTextureDir();
		verifyFiles();
		findInputFiles();
		correctFiles();
		generateInputPaths();
		generateOutputPaths();
	}
}

bool parseBool(const std::string& val) {
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "1" || lower == "true" || lower == "yes";
}

std::unordered_map<std::string, std::string> parseIni(
    const std::string& filename,
    std::vector<WhitelistEntry>& whitelist)
{
    std::ifstream file(filename);
    std::unordered_map<std::string, std::string> settings;
    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        // Remove comments
        size_t commentPos = line.find_first_of(";#");
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        // Skip empty lines
        if (line.empty())
            continue;
        // Check for section header
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            currentSection.erase(0, currentSection.find_first_not_of(" \t"));
            currentSection.erase(currentSection.find_last_not_of(" \t") + 1);
            continue;
        }
        size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        if (key.empty() || value.empty())
            continue;

        // Handle [Whitelist]. Two key forms, RHS = image file name:
        //   formID:plugin          e.g. 0x21E50:Fallout4.esm = my_image.dds
        //   value:formID:plugin    e.g. 1:0x21E50:Fallout4.esm = my_image.dds  (older configs)
        // The value is only an internal ID now; entries without one get one assigned below.
		if (currentSection == "Whitelist") {
		    std::vector<std::string> parts;
		    for (size_t start = 0;;) {
		        const size_t colon = key.find(':', start);
		        std::string part = key.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
		        part.erase(0, part.find_first_not_of(" \t"));
		        part.erase(part.find_last_not_of(" \t") + 1);
		        parts.push_back(std::move(part));
		        if (colon == std::string::npos)
		            break;
		        start = colon + 1;
		    }
		    if (parts.size() != 2 && parts.size() != 3) {
		        spdlog::warn("Whitelist entry \"{}\": expected formID:plugin (or value:formID:plugin); skipping", key);
		        continue;
		    }
		    const bool        hasValue = parts.size() == 3;
		    const std::string valueStr = hasValue ? parts[0] : std::string{};
		    const std::string formIDStr = parts[hasValue ? 1 : 0];
		    const std::string plugin = parts[hasValue ? 2 : 1];

		    int whitelistValue = -1;  // -1: assigned after the whole file is read
		    int formID = -1;
		    try {
		        std::size_t used = 0;
		        formID = std::stoi(formIDStr, &used, 0);  // "0x21E50" (hex) or plain decimal
		        if (used != formIDStr.size())
		            throw std::invalid_argument("trailing characters");
		        if (hasValue) {
		            whitelistValue = std::stoi(valueStr, &used, 10);
		            if (used != valueStr.size() || whitelistValue < 0)
		                throw std::invalid_argument("bad value");
		        }
		    } catch (...) {
		        spdlog::warn("Whitelist entry \"{}\": couldn't parse the form ID{} (hex form IDs need a 0x prefix); skipping",
		            key, hasValue ? " or value" : "");
		        continue;
		    }
		    if (plugin.empty()) {
		        spdlog::warn("Whitelist entry \"{}\": missing plugin name; skipping", key);
		        continue;
		    }

		    // Reject this entry if it collides with one already accepted
		    bool rejected = false;
		    for (const auto& existing : whitelist) {
		        if (hasValue && existing.value == whitelistValue) {
		            spdlog::warn("Whitelist entry \"{}\": value {} already claimed by {:08X}:{}; rejecting duplicate",
		                key, whitelistValue, existing.formID, existing.plugin);
		            rejected = true;
		            break;
		        }
		        if (existing.formID == formID && path::toLower(existing.plugin) == path::toLower(plugin)) {
		            spdlog::warn("Whitelist entry \"{}\": {:08X}:{} is already whitelisted (to {}); rejecting duplicate",
		                key, formID, plugin, existing.filename);
		            rejected = true;
		            break;
		        }
		        // The same image may be pinned to several load screens, so file names may repeat
		    }
		    if (rejected) {
		        continue;
		    }

		    whitelist.push_back({ formID, plugin, value, whitelistValue });
		    continue;
		}

        // Normal ALR.ini setting
        if (!key.empty())
            settings[key] = value;
    }

    // Entries written without a value get the lowest numbers not already used by an entry
    // that has one (so older value:formID:plugin lines keep theirs, wherever they appear).
    std::unordered_set<int> usedValues;
    for (const auto& entry : whitelist) {
        if (entry.value >= 0)
            usedValues.insert(entry.value);
    }
    int nextValue = 0;
    for (auto& entry : whitelist) {
        if (entry.value >= 0)
            continue;
        while (usedValues.count(nextValue))
            nextValue++;
        entry.value = nextValue;
        usedValues.insert(nextValue);
    }
    return settings;
}

void PathBuilder::readInis()
{
	message::checkForFalseError(path::verifyPath(pathData->iniPath), "ALR.ini Not Found");

	whitelist.clear();
	auto settings = parseIni(pathData->iniPath, whitelist);

    bool enableGen = parseBool(settings["EnableLoadingScreenGeneration"]);
	std::string customFilePath = settings["ImageSourcePath"];

	if (!customFilePath.empty()) {
	    inputPath = customFilePath;
	} else {
	    findImgDir();
	}

	dImageRD = enableGen;

	// Optional tuning for the runtime texture swap
	dTextureSwapDebugRD = parseBool(settings["TextureSwapDebugLog"]);
	textureSwapFitMode = path::toLower(settings["TextureSwapFitMode"]);  // "crop" (default) or "stretch"
	spdlog::info("ALR.ini: TextureSwapFitMode={} TextureSwapDebugLog={}",
		textureSwapFitMode.empty() ? "crop" : textureSwapFitMode, dTextureSwapDebugRD);

	// Load-screen light overrides: any game setting named fLoadingMenuSceneLight* (the
	// [Interface] settings), e.g. fLoadingMenuSceneLight3DimmerValue = 2.13
	loadScreenLights.clear();
	for (const auto& [key, value] : settings) {
		if (key.rfind("fLoadingMenuSceneLight", 0) != 0)
			continue;
		try {
			loadScreenLights.emplace_back(key, std::stof(value));
		} catch (...) {
			spdlog::warn("ALR.ini: {} = '{}' is not a number; ignored", key, value);
		}
	}
}

void PathBuilder::verifyFiles()
{
	path::verifyPathError(inputPath, path::getDirectory(inputPath) + " Folder Not Found\nEither enter a new path in ALR.ini or use 'default' path and add ALR_Image_Dir to F4SE/Plugins");
	path::verifyPathError(outputPath, "Textures\\Interface\\ALR_Backgrounds was not created");
}

void PathBuilder::cleanOutputPathFiles()
{

    auto listDDS = [this]() {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(outputPath, ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            std::error_code fileEc;
            if (it->is_regular_file(fileEc) && path::toLower(it->path().extension().string()) == ".dds")
                files.push_back(it->path());
        }
        return files;
    };

    std::error_code ec;
    if (!std::filesystem::is_directory(outputPath, ec)) {
        // Usually just a first run: buildTextureDir() creates the folder next
        spdlog::info("cleanOutputPathFiles: {} not found, nothing to clean", outputPath);
        return;
    }

    std::size_t removed = 0;
    std::vector<std::filesystem::path> remaining = listDDS();
    for (int pass = 0; pass < 8 && !remaining.empty(); pass++) {
        std::size_t removedThisPass = 0;
        for (const auto& file : remaining) {  // listed first, then deleted: no deleting while iterating
            std::error_code fileEc;
            if (std::filesystem::remove(file, fileEc))
                removedThisPass++;
            else if (fileEc)
                spdlog::warn("cleanOutputPathFiles: failed to delete {}: {}", file.string(), fileEc.message());
        }
        removed += removedThisPass;
        remaining = listDDS();
        if (removedThisPass == 0)
            break;
    }

    spdlog::info("cleanOutputPathFiles: deleted {} DDS file(s) from {}", removed, outputPath);
    for (const auto& file : remaining)
        spdlog::warn("cleanOutputPathFiles: still present after cleanup, will stay in the image pool: {}", file.string());
}

void PathBuilder::findImgDir()
{
	inputPath = dataPathSS.str() + "F4SE\\Plugins\\ALR_Image_Dir\\";
}

void PathBuilder::buildTextureDir()
{
	std::error_code ec;
	std::filesystem::create_directories(outputPath, ec);  // creates any missing parents too
	if (ec)
		message::displayErrorMessage("ALR_ERROR", "Couldn't create " + outputPath + "\n" + ec.message());
}

void PathBuilder::findDLLPath() {
	TCHAR path[_MAX_PATH + 1];
	message::checkForError(GetModuleFileName(GetModuleHandle(("ALR.dll")), path, sizeof(path) / sizeof(path[0])));
	for (int i = 0; path[i + PLUGIN_PATH_LEN] != 0; i++)
		dataPathSS << path[i];

	outputPath = dataPathSS.str() + "Textures\\Interface\\ALR_Backgrounds\\";
	pathData->iniPath = dataPathSS.str() + "F4SE\\Plugins\\ALR.ini";
}

void PathBuilder::findInputFiles() {
    static const std::unordered_set<std::string> kSupportedExtensions = {
        ".jpg", ".jpeg", ".jfif",".png", ".dds", ".bmp", ".tif", ".tiff"
    };

    std::vector<std::string> allFiles;
    for (auto& path : std::filesystem::recursive_directory_iterator(inputPath)) {
        std::string ext = path::toLower(path.path().extension().string());
        if (kSupportedExtensions.count(ext)) {
            allFiles.emplace_back(path.path().string());
        }
    }

    if (allFiles.empty()) {
        message::displayErrorMessage("ALR_ERROR",
            "No Images Found In ALR.ini's Path, Ensure DDS/PNG/JPG/BMP/TIFF/GIF Images Are In F4SE\\Plugins\\ALR_Image_Dir Or The Path Set Manually In Ini ");
        return;
    }

    // Match each whitelist entry to a real file by filename, pull it out of the general pool
    std::unordered_map<int, std::string> pinnedSlots;  // value -> full path
    std::vector<bool> matched(allFiles.size(), false);

    for (const auto& entry : whitelist) {
        bool found = false;
        // A file already claimed by another entry still matches: several load screens can
        // share one image. It's converted once (generateOutputPaths skips the repeats).
        for (std::size_t i = 0; i < allFiles.size(); i++) {
            std::string fname = path::toLower(std::filesystem::path(allFiles[i]).filename().string());
            if (fname == path::toLower(entry.filename)) {
                pinnedSlots[entry.value] = allFiles[i];
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            spdlog::warn("Whitelist entry for value {} references \"{}\", but no matching file was found in the input directory",
                entry.value, entry.filename);
        }
    }

    // Everything not claimed by the whitelist
    std::vector<std::string> remaining;
    for (std::size_t i = 0; i < allFiles.size(); i++) {
        if (!matched[i]) remaining.push_back(allFiles[i]);
    }

    // Final vector must be large enough to hold the highest pinned slot
    std::size_t maxPinnedIndex = 0;
    for (const auto& [idx, path] : pinnedSlots) {
        maxPinnedIndex = std::max(maxPinnedIndex, static_cast<std::size_t>(idx));
    }
    std::size_t finalSize = std::max(allFiles.size(), maxPinnedIndex + 1);
    inputFiles.assign(finalSize, std::string{});

    for (const auto& [idx, path] : pinnedSlots) {
        inputFiles[idx] = path;
    }

    // Fill the remaining (non-pinned) empty slots with the unclaimed files, in order
    std::size_t remainIdx = 0;
    for (std::size_t i = 0; i < inputFiles.size() && remainIdx < remaining.size(); i++) {
        if (inputFiles[i].empty()) {
            inputFiles[i] = remaining[remainIdx++];
        }
    }

    // Trim only genuinely-unused trailing slots (e.g. a pinned value far beyond file count)
    while (!inputFiles.empty() && inputFiles.back().empty()) {
        inputFiles.pop_back();
    }
}

void PathBuilder::correctFiles() {
    std::unordered_set<std::size_t> pinnedIndices;
    for (const auto& entry : whitelist) {
        if (entry.value >= 0 && static_cast<std::size_t>(entry.value) < inputFiles.size()) {
            pinnedIndices.insert(static_cast<std::size_t>(entry.value));
        } else if (entry.value >= 0) {
            spdlog::warn("Whitelist value {} is beyond inputFiles bounds ({})",
                entry.value, inputFiles.size());
        }
    }

    // Collect only the non-pinned entries, shuffle those, write them back to their original slots
    std::vector<std::string> movable;
    std::vector<std::size_t> movablePositions;
    for (std::size_t i = 0; i < inputFiles.size(); i++) {
        if (!pinnedIndices.count(i)) {
            movable.push_back(inputFiles[i]);
            movablePositions.push_back(i);
        }
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(movable.begin(), movable.end(), g);

    for (std::size_t i = 0; i < movable.size(); i++) {
        inputFiles[movablePositions[i]] = movable[i];
    }

    // No cap on the number of images: every load screen uses the same carrier NIF
}

void PathBuilder::generateInputPaths() {
	
	for (auto& inputFile : inputFiles)
		pathData->inputFilePaths.emplace_back(inputFile);
}


string PathBuilder::TextureSwapFileName(const string& sourceFileName)
{
    // Keep the source name, limited to ASCII characters that are safe both in file
    // names and in game texture paths (paths are narrow strings in the engine).
    const string stem = std::filesystem::path(sourceFileName).stem().string();
    string out;
    for (unsigned char c : stem) {
        const bool safe = c < 0x80 && (std::isalnum(c) || std::strchr(" _-.()[]'", c) != nullptr);
        out.push_back(safe ? static_cast<char>(c) : '_');
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    if (out.empty())
        out = "image";
    return out + ".dds";
}

void PathBuilder::generateOutputPaths()
{

    const std::size_t count = pathData->inputFilePaths.size();
    pathData->outputPaths.assign(count, string{});

    auto isWhitelisted = [this](std::size_t a_slot, const string& a_srcName) {
        for (const auto& entry : whitelist) {
            // findInputFiles() put each whitelisted file in slot <value>
            if (entry.value == static_cast<int>(a_slot) && path::toLower(entry.filename) == path::toLower(a_srcName))
                return true;
        }
        return false;
    };

    std::unordered_map<string, string> used;  // lowercase output name -> source that got it
    std::size_t skipped = 0, shared = 0;
    for (int pass = 0; pass < 2; pass++) {  // pass 0: whitelisted sources, pass 1: the rest
        for (std::size_t i = 0; i < count; i++) {
            const string& src = pathData->inputFilePaths[i];
            if (src.empty())
                continue;  // gap left by a whitelist value larger than the number of images

            const string srcName = std::filesystem::path(src).filename().string();
            if (isWhitelisted(i, srcName) != (pass == 0))
                continue;

            const string name = TextureSwapFileName(srcName);
            const auto [it, inserted] = used.try_emplace(path::toLower(name), src);
            if (!inserted) {
                pathData->inputFilePaths[i].clear();  // ImageConvert skips empty entries
                if (std::filesystem::path(it->second) == std::filesystem::path(src)) {
                    shared++;  // the same file pinned to several load screens: converted once
                    continue;
                }
                spdlog::warn("Image skipped: {} and {} would both become {}; rename one of them",
                    it->second, src, name);
                skipped++;
                continue;
            }
            pathData->outputPaths[i] = outputPath + name;
        }
    }

    spdlog::info("{} image(s) to convert into {} ({} shared by several whitelist entries, {} skipped for duplicate names)",
        used.size(), outputPath, shared, skipped);
}
