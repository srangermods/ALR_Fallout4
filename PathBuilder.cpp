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

            // Trim section name
            currentSection.erase(0, currentSection.find_first_not_of(" \t"));
            currentSection.erase(currentSection.find_last_not_of(" \t") + 1);

            continue;
        }

        size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);

        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (key.empty() || value.empty())
            continue;

        // Handle [Whitelist]
        if (currentSection == "Whitelist") {
            size_t colon = key.find(':');

            if (colon == std::string::npos)
                continue;

            int formID = std::stoi(key.substr(0, colon), nullptr, 0);
            std::string plugin = key.substr(colon + 1);

            // Trim plugin whitespace
            plugin.erase(0, plugin.find_first_not_of(" \t"));
            plugin.erase(plugin.find_last_not_of(" \t") + 1);

            try {
                int whitelistValue = std::stoi(value);

                whitelist.push_back({
                    formID,
                    plugin,
                    whitelistValue
                });
            }
            catch (...) {
                // Invalid whitelist entry; skip it
            }

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
	for (auto& path : std::filesystem::recursive_directory_iterator(inputPath)) {
		string ext = path::toLower(path.path().extension().string());
		if (ext == ".jpg" || ext == ".png" || ext == ".dds")
			inputFiles.emplace_back(path.path().string());
	}

	if(inputFiles.size() == 0) message::displayErrorMessage("ALR_ERROR", "No Images Found In ALR.ini's Path, Ensure DDS/PNG/JPG Images Are In F4SE\\Plugins\\ALR_Image_Dir Or The Path Set Manually In Ini ");
}

void PathBuilder::correctFiles() {

	random_device rd;
	mt19937 g(rd());

	shuffle(inputFiles.begin(), inputFiles.end(), g);

/*	else if (inputFiles.size() < MAX_INPUTS) {

		vector<string> tCopy = inputFiles;
		for (int i = 0; inputFiles.size() < MAX_INPUTS; i++) {
			shuffle(tCopy.begin(), tCopy.end(), g);
			inputFiles.insert(inputFiles.end(), tCopy.begin(), tCopy.end());
		}
		inputFiles.resize(MAX_INPUTS);
	}*/
}

void PathBuilder::generateInputPaths() {
	//random_device rd;
	//mt19937 g(rd());
	//shuffle(inputFiles.begin(), inputFiles.end(), g);
	
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