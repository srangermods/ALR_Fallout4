// main.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "PathBuilder.h"
#include "PathDestroyer.h"
#include "ImageConvert.h"
#include "PathData.h"


__declspec(dllexport) PathDataParent* PathDataCreate()
{
    return new PathData();
}
__declspec(dllexport) PathBuilderParent* PathBuilderCreate(PathDataParent& _pathData)
{
    return new PathBuilder(_pathData);
}
__declspec(dllexport) ImageConvertParent* ImageConvertCreate(PathDataParent& _pathData, const std::vector<WhitelistEntry>& whitelist)
{
    return new ImageConvert(_pathData, whitelist);
}

// Keep this global pointer allocation
PathDataParent* pathData = PathDataCreate();
PathBuilderParent* PBinst;
PathBuilder* realPBinst;

std::optional<int> GetNifIndex(RE::TESObjectSTAT* stat)
{
    const char* modelPath = stat->GetModel();
    if (!modelPath || *modelPath == '\0') {
        return std::nullopt;
    }

    std::string stem = std::filesystem::path(modelPath).stem().string();
    if (stem.rfind("LS", 0) != 0) {
        return std::nullopt;
    }
    std::string numPart = stem.substr(2);

    int index = -1;
    auto [ptr, ec] = std::from_chars(numPart.data(), numPart.data() + numPart.size(), index);
    if (ec != std::errc{} || ptr != numPart.data() + numPart.size()) {
        return std::nullopt;
    }
    return index;
}



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
                ImageConvertParent* IMinst = ImageConvertCreate(*pathData, realPBinst->whitelist);
                delete IMinst;
            }
            
        });
        // Detach the thread so it runs independently in the background 
        // without blocking F4SE or the game's startup pipeline
        workerThread.detach();
        
    }

    if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        auto* alrPlugin = dataHandler->LookupLoadedLightModByName("ALR.esp"sv);
        RE::BGSTransform* sharedTransform = dataHandler->LookupForm<RE::BGSTransform>(0x1B5, "ALR.esp"sv);

        std::size_t outputPathFiles = realPBinst ? static_cast<std::size_t>(realPBinst->countOutputPathFiles()) : 0;

        //vector of donor stats, not including whitelisted ones
        std::vector<RE::TESObjectSTAT*> alrStats;
        //contains all stats in ALR, mapped to it's dds filename as a key
        std::unordered_map<int, RE::TESObjectSTAT*> statMap;
        bool isWhitelisted;
        if (alrPlugin) {
            for (auto* stat : dataHandler->GetFormArray<RE::TESObjectSTAT>()) {
                isWhitelisted = false;
                if (!alrPlugin->IsFormInMod(stat->GetFormID())) {
                    continue;
                }

                auto index = GetNifIndex(stat);
                if (!index) {
                    spdlog::warn("STAT {:08X}: model path doesn't match LS<n>; skipping", stat->GetFormID());
                    continue;
                }

                if (*index < 0 || static_cast<std::size_t>(*index) >= outputPathFiles) {
                    spdlog::warn("STAT {:08X}: LS{} has no corresponding generated image ({} images exist); skipping",
                        stat->GetFormID(), *index, outputPathFiles);
                    continue;
                }
                statMap[*index] = stat;
                for (const auto& entry : realPBinst->whitelist) {
                    if (entry.value == *index) {
                        spdlog::info("STAT {:08X}: {} is whitelisted; excluding from donor pool", stat->GetFormID(), *index);
                        isWhitelisted = true;
                        continue;
                    }
                }
                if (isWhitelisted)
                    continue;
                
                alrStats.push_back(stat);
            }
        }

        if (statMap.empty()) {
            spdlog::warn("No usable ALR.esp STATs found; skipping randomization");
        } else {
            std::mt19937 rng{ std::random_device{}() };
            std::uniform_int_distribution<std::size_t> statDist(0, alrStats.size() - 1);

            std::size_t statIdx = 0;
            auto& loadScreens = dataHandler->GetFormArray<RE::TESLoadScreen>();
            std::shuffle(loadScreens.begin(), loadScreens.end(), rng);
            for (auto* lscr : loadScreens) {
                isWhitelisted = false;
                for (const auto& entry : realPBinst->whitelist) {
                    if (lscr == dataHandler->LookupForm<RE::TESLoadScreen>(entry.formID, entry.plugin)){
                        isWhitelisted = true;
                        spdlog::info("This loadscreen is whitelisted! {} : using {} dds", entry.formID, entry.value);
                        lscr->loadNIFData->loadNif                = statMap[entry.value];
                        continue;
                    }
                }
                if (!isWhitelisted){
                    RE::TESObjectSTAT* donor = (statIdx < alrStats.size()) ? alrStats[statIdx++] : alrStats[statDist(rng)];
                    lscr->loadNIFData->loadNif               = donor;
                }
                
                lscr->loadNIFData->transform              = sharedTransform;
                lscr->loadNIFData->rotationConstraints[0] = 0;
                lscr->loadNIFData->rotationConstraints[1] = 0;
                lscr->loadNIFData->zoomConstraints[0]     = 0.0f;
                lscr->loadNIFData->zoomConstraints[1]     = 0.0f;
            }

            spdlog::info("Randomized load screens using {} of {} ALR.esp STATs (bounded by {} generated images)",
                alrStats.size(), MAX_INPUTS, outputPathFiles);
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