// main.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "PathBuilder.h"
#include "PathDestroyer.h"
#include "ImageConvert.h"
#include "PathData.h"
#include <spdlog/spdlog.h>

__declspec(dllexport) PathDataParent* PathDataCreate()
{
    return new PathData();
}
__declspec(dllexport) PathBuilderParent* PathBuilderCreate(PathDataParent& _pathData)
{
    return new PathBuilder(_pathData);
}
__declspec(dllexport) ImageConvertParent* ImageConvertCreate(PathDataParent& _pathData, const std::vector<std::string>& whitelistFileNames)
{
    return new ImageConvert(_pathData, whitelistFileNames);
}

// Keep this global pointer allocation
PathDataParent* pathData = PathDataCreate();
PathBuilderParent* PBinst;
PathBuilder* realPBinst;
void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == F4SE::MessagingInterface::kPostPostLoad) {
        
        // Spawn a completely detached native thread to isolate OpenMP from the game's loading thread
        std::thread workerThread([]() {
            
            // Give the GOG client hooks 200ms to cleanly finish loading their tracking structures
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            PBinst = PathBuilderCreate(*pathData);
            realPBinst = static_cast<PathBuilder*>(PBinst);
            if (PBinst->IsImageRD()) {
                ImageConvertParent* IMinst = ImageConvertCreate(*pathData, realPBinst->whitelistFileNames);
                delete IMinst;
            }
            
        });

        // Detach the thread so it runs independently in the background 
        // without blocking F4SE or the game's startup pipeline
        workerThread.detach();
    }
    if (a_msg->type == F4SE::MessagingInterface::kGameDataReady){
        //Randomize Vanilla Load Screens (respect whitelist)
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        static constexpr std::array kVanillaPlugins{
            "Fallout4.esm"sv,
            "DLCCoast.esm"sv,
            "DLCNukaWorld.esm"sv,
            "DLCRobot.esm"sv,
            "DLCworkshop03.esm"sv,
            "ccBGSFO4044-HellfirePowerArmor.esl"sv,
            "ccBGSFO4115-X02.esl"sv,
            "ccSBJFO4003-Grenade.esl"sv,
            };
        std::vector<RE::TESBoundObject*> vanillaLoadNifs;
        RE::BGSTransform* sharedTransform = nullptr;
        std::vector<RE::TESLoadScreen*>  modLoadScreens;
        std::vector<RE::TESLoadScreen*>  vanillaLoadScreens;
        std::vector<const RE::TESFile*> excludedFiles;
        excludedFiles.reserve(kVanillaPlugins.size());

        for (auto name : kVanillaPlugins) {
             const RE::TESFile* file = dataHandler->LookupLoadedModByName(name);
            if (!file) {
                file = dataHandler->LookupLoadedLightModByName(name);
            }
            if (file) {
                excludedFiles.push_back(file);
            }
            // else: not in this load order (e.g. a CC content pack not owned/installed) — skip it
        }
        for (auto* lscr : dataHandler->GetFormArray<RE::TESLoadScreen>()) {
            if (!lscr->loadNIFData || !lscr->loadNIFData->loadNif) {
                continue;  // nothing to read from or write to on this one
            }

            const auto formID = lscr->GetFormID();
            const bool isExcluded = std::ranges::any_of(excludedFiles,
                [formID](const RE::TESFile* f) { return f->IsFormInMod(formID); });

            if (isExcluded) {
                vanillaLoadNifs.push_back(lscr->loadNIFData->loadNif);
                vanillaLoadScreens.push_back(lscr);
                if (!sharedTransform) {
                    sharedTransform = lscr->loadNIFData->transform;
                }
            } else {
                modLoadScreens.push_back(lscr);
            }
        }
        std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<std::size_t> dist(0, vanillaLoadNifs.size() - 1);
        for (auto* lscr : vanillaLoadScreens) {
            //if lscr not in whitelist
            const char* editorID = lscr->GetFormEditorID();
            //spdlog::info("LSCR Editor ID: {}"sv, lscr->GetFormEditorID());
            std::string cleanID;
            if (editorID) {
                cleanID = editorID;
                // Check if the string starts with "LS"
                if (cleanID.rfind("LS", 0) == 0) { // Or cleanID.starts_with("LS") in C++20
                    cleanID = cleanID.substr(2);   // Extract everything starting at index 2
                }
                cleanID += ".DDS";
            }
            //spdlog::info("cleaned ID: {}"sv, cleanID);

            if (std::find(realPBinst->whitelistFileNames.begin(), realPBinst->whitelistFileNames.end(), cleanID) != realPBinst->whitelistFileNames.end()) {
                continue;
            }

            auto* data = lscr->loadNIFData;
            int LoadNifIndex = dist(rng);
            std::string strFileName = std::to_string(LoadNifIndex);
            strFileName += ".DDS";
            if (std::find(realPBinst->whitelistFileNames.begin(), realPBinst->whitelistFileNames.end(), strFileName) != realPBinst->whitelistFileNames.end()) {
                continue;
            }
            auto* donor = vanillaLoadNifs[dist(rng)];
            data->loadNif               = donor;
        }
        if (PBinst->RandomizeModLoadScreens()){            
            for (auto* lscr : modLoadScreens) {
                auto* data = lscr->loadNIFData;
                auto* donor = vanillaLoadNifs[dist(rng)];

                spdlog::info("LSCR {:08X}: loadNif {:08X} -> {:08X}",
                    lscr->GetFormID(), data->loadNif->GetFormID(), donor->GetFormID());

                data->loadNif               = donor;
                data->transform              = sharedTransform;
                data->rotationConstraints[0] = 0;
                data->rotationConstraints[1] = 0;
                data->zoomConstraints[0]     = 0.0f;
                data->zoomConstraints[1]     = 0.0f;
            }
            spdlog::info("Randomized {} of {} load screens ({} vanilla donors)",
                modLoadScreens.size(), modLoadScreens.size(), vanillaLoadNifs.size());
        }
    delete PBinst;
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    // 2. Register our event handler listener with F4SE instead of running the code here
    auto messaging = F4SE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(OnF4SEMessage);
    } else {
        // Fallback safety log check if messaging interface fails to initialize
        return false;
    }

    return true;
}

F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (const auto data = F4SE::PluginVersionData::GetSingleton())
     {
         a_info->infoVersion = F4SE::PluginInfo::kVersion;
         a_info->name = data->GetPluginName().data();
         a_info->version = data->GetPluginVersion().pack();
     }

    const auto ver = a_f4se->RuntimeVersion();
    if (ver < REL::Version(F4SE::RUNTIME_1_10_163))
         return false;

    return true;
 }

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpReserved)  // reserved
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        // Critical Fix: Remove the & symbol! 
        // pathData is already a pointer, delete &pathData tries to free the memory address of the pointer variable itself!
        delete pathData; 
        break;
    }
    return TRUE;
}