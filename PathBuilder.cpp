#include "pch.h"
#include "PathBuilder.h"

PathBuilder::PathBuilder(PathDataParent& _pathData) : pathData(&_pathData)
{
	rebuildIniFlag = dImageRD = dOverlayRD = dRandomizeModLoadScreensRD = false;

	findDLLPath();
	findPrefPath();
	readInis();
	//if (rebuildIniFlag) rebuildIni();
	

	if (!dImageRD){
		//_MESSAGE("Disable deleting and reloading images.");
		if (!verifyImages()){
			dImageRD = true;
		}
	}

	//if (dOverlayRD)
	//	if (!verifyOverlay())
	//		dOverlayRD = false;

	if (dImageRD && !dOverlayRD) {
		//_MESSAGE("Setting up input files and paths.");
		cleanOutputPathFiles();
		buildTextureDir();
		verifyFiles();
		findInputFiles();
		correctFiles();
		generateInputPaths();
		generateOutputPaths();
	}
}

PathBuilder::~PathBuilder()
{
	delete pathData;
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

        // Handle [Whitelist]: key = "value:formID:plugin", RHS = inputFilename
		if (currentSection == "Whitelist") {
		    size_t firstColon = key.find(':');
		    if (firstColon == std::string::npos) {
		        spdlog::warn("Whitelist entry \"{}\": missing ':' separators; skipping", key);
		        continue;
		    }
		    size_t secondColon = key.find(':', firstColon + 1);
		    if (secondColon == std::string::npos) {
		        spdlog::warn("Whitelist entry \"{}\": expected value:formID:plugin; skipping", key);
		        continue;
		    }

		    std::string valueStr = key.substr(0, firstColon);
		    std::string formIDStr = key.substr(firstColon + 1, secondColon - firstColon - 1);
		    std::string plugin = key.substr(secondColon + 1);
		    plugin.erase(0, plugin.find_first_not_of(" \t"));
		    plugin.erase(plugin.find_last_not_of(" \t") + 1);

		    int whitelistValue = -1;
		    int formID = -1;
		    try {
		        whitelistValue = std::stoi(valueStr, nullptr, 10);
		        formID = std::stoi(formIDStr, nullptr, 0);
		    } catch (...) {
		        spdlog::warn("Whitelist entry \"{}\": couldn't parse value/formID; skipping", key);
		        continue;
		    }

		    // Reject this entry if it collides with one already accepted
		    bool rejected = false;
		    for (const auto& existing : whitelist) {
		        if (existing.value == whitelistValue) {
		            spdlog::warn("Whitelist entry \"{}\": value {} already claimed by {:08X}:{}; rejecting duplicate",
		                key, whitelistValue, existing.formID, existing.plugin);
		            rejected = true;
		            break;
		        }
		        if (existing.formID == formID && existing.plugin == plugin) {
		            spdlog::warn("Whitelist entry \"{}\": {:08X}:{} already whitelisted (as value {}); rejecting duplicate",
		                key, formID, plugin, existing.value);
		            rejected = true;
		            break;
		        }
		        if (existing.filename == value) {
		            spdlog::warn("Whitelist entry \"{}\": filename \"{}\" already used by value {}; rejecting duplicate",
		                key, value, existing.value);
		            rejected = true;
		            break;
		        }
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
    return settings;
}

void PathBuilder::readInis()
{
	ifstream in(pathData->iniPath);
	message::checkForFalseError(in.is_open(), "ALR.ini Not Found");


	
	firstTimeCheck();
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

	pathData->backgroundReplace = false;

	in.close();

	in.open(prefPath);

	pathData->outputHeight = 0;
	pathData->outputWidth = 0;

	if (!in.is_open()) {
		message::displayMessage("ALR_WARNING", "Fallout4Prefs.ini Not Found \n Default Settings Assumed");
		pathData->outputHeight = 1080;
		pathData->outputWidth = 1920;
	}
	else {
		string line;
		int resIndex = 8,
			resIdIndex = 6;

		while (getline(in, line)) {
			if (line.find("iSize ", 0) != string::npos) {
				ostringstream resSS;

				for (int i = resIndex; i < line.size(); i++)
					resSS << line.at(i);

				if(line.at(resIdIndex) == 'H')
					pathData->outputHeight = stoi(resSS.str());
				else
					pathData->outputWidth = stoi(resSS.str());

				if (pathData->outputHeight != 0 && pathData->outputWidth != 0)
					break;
			}
		}
	}
	in.close();
}


void PathBuilder::verifyFiles()
{
	path::verifyPathError(overlayPath, "Textures\\Interface\\Objects\\LoadScreen was not created");
	path::verifyPathError(backgroundPath, "Textures\\Interface\\ALR_Backgrounds was not created");
	path::verifyPathError(inputPath, path::getDirectory(inputPath) + " Folder Not Found\nEither enter a new path in ALR.ini or use 'default' path and add ALR_Image_Dir to F4SE/Plugins");
	path::verifyPathError(outputPath, "Textures\\Interface\\ALR_Backgrounds was not created");
}

void PathBuilder::cleanOutputPathFiles()
{
    std::error_code ec;
    auto iterator = std::filesystem::directory_iterator(outputPath, ec);
    if (ec) {
        spdlog::warn("cleanOutputPathFiles: couldn't open {}: {}", outputPath, ec.message());
        return;
    }

    for (const auto& entry : iterator) {
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (entry.path().extension() != ".DDS") {
            continue;
        }

        std::string stem = entry.path().stem().string();
        int fileNumber = -1;
        auto [ptr, parseEc] = std::from_chars(stem.data(), stem.data() + stem.size(), fileNumber);
        if (parseEc != std::errc{} || ptr != stem.data() + stem.size()) {
            // Doesn't match "<number>.DDS" — not one of ours, leave it alone
            continue;
        }

        std::filesystem::remove(entry.path(), ec);
        if (ec) {
            spdlog::warn("cleanOutputPathFiles: failed to delete {}: {}", entry.path().string(), ec.message());
        } else {
            spdlog::info("cleanOutputPathFiles: deleted {}", entry.path().string());
        }
    }
}

void PathBuilder::findImgDir()
{
	inputPath = dataPathSS.str() + "F4SE\\Plugins\\ALR_Image_Dir\\";
	//rebuildIniFlag = true;
}

void PathBuilder::firstTimeCheck()
{
	if (!(path::verifyPath(dataPathSS.str() + "ALR - All DLC.esp") || path::verifyPath(dataPathSS.str()) + "ALR - No DLC.esp") ||
		!(path::verifyPath(dataPathSS.str() + "ALR - All DLC.esp") || path::verifyPath(dataPathSS.str()) + "ALR - No DLC.esp"))
		message::displayErrorMessage("ALR_ERROR","ALR esp and or Main ba2 is missing, check the mod was installed properly");

	//rebuildIniFlag = true;
}

void PathBuilder::buildTextureDir()
{
	if (!filesystem::is_directory(path::getDirectoryFromBegin(outputPath,26)))
		message::checkForError(filesystem::create_directories(path::getDirectoryFromBegin(outputPath, 26)));

	if (!filesystem::is_directory(path::getDirectoryFromBegin(outputPath, 16)))
		message::checkForError(filesystem::create_directories(path::getDirectoryFromBegin(outputPath, 16)));

	if (!filesystem::is_directory(outputPath)) 
		message::checkForError(filesystem::create_directories(outputPath));

	if (!filesystem::is_directory(path::getDirectoryFromBegin(overlayPath, 11)))
		message::checkForError(filesystem::create_directories(path::getDirectoryFromBegin(overlayPath, 11)));

	if (!filesystem::is_directory(overlayPath)) 
		message::checkForError(filesystem::create_directories(overlayPath));

}

void PathBuilder::findDLLPath() {
	TCHAR path[_MAX_PATH + 1];
	message::checkForError(GetModuleFileName(GetModuleHandle(("ALR.dll")), path, sizeof(path) / sizeof(path[0])));
	for (int i = 0; path[i + PLUGIN_PATH_LEN] != 0; i++)
		dataPathSS << path[i];

	outputPath = dataPathSS.str() + "Textures\\Interface\\ALR_Backgrounds\\";
	pathData->iniPath = dataPathSS.str() + "F4SE\\Plugins\\ALR.ini";
	overlayPath = dataPathSS.str() + "Textures\\Interface\\Objects\\LoadScreen\\";
	backgroundPath = dataPathSS.str() + "Textures\\Interface\\";
}

int PathBuilder::countOutputPathFiles(){
	std::size_t fileCount = 0;
	for (const auto& entry : std::filesystem::directory_iterator(outputPath)) {
	    if (entry.is_regular_file() && entry.path().extension() == ".DDS") {
	        ++fileCount;
	    }
	}
	return fileCount;
}
void PathBuilder::findPrefPath()
{
	char path[MAX_PATH];
	message::checkForError(SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path));

	for (int i = 0; path[i] != NULL; i++)
		prefPathSS << path[i];

	prefPath = prefPathSS.str() + "\\My Games\\Fallout4\\Fallout4Prefs.ini";
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
        for (std::size_t i = 0; i < allFiles.size(); i++) {
            if (matched[i]) continue;
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
            spdlog::warn("Whitelist value {} is beyond inputFiles bounds ({}); this pin will be lost if MAX_INPUTS truncates further",
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

    if (inputFiles.size() > MAX_INPUTS) {
        for (std::size_t idx : pinnedIndices) {
            if (idx >= MAX_INPUTS) {
                spdlog::warn("Whitelist pin at index {} is being dropped — MAX_INPUTS ({}) truncates it",
                    idx, MAX_INPUTS);
            }
        }
        inputFiles.resize(MAX_INPUTS);
    }
}

void PathBuilder::generateInputPaths() {
	
	for (auto& inputFile : inputFiles)
		pathData->inputFilePaths.emplace_back(inputFile);
}

void PathBuilder::generateOutputPaths() {
	string outputPathFileName = "";
	for (int i = 0; i < pathData->inputFilePaths.size(); i++){ 
		outputPathFileName = outputPath + to_string(i) + ".DDS";
		pathData->outputPaths.emplace_back( outputPathFileName);
	}
	pathData->overlayPath = overlayPath + "Overlay01_d.DDS";

	if(pathData->backgroundReplace)
		pathData->backgroundPath = backgroundPath + "LoadingMenuBG.DDS";
}

bool PathBuilder::verifyImages()
{
	for (auto& path : pathData->outputPaths)
		if (!path::verifyPath(path))
			return false;
	return true;
}