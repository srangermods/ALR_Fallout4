// main.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "PathBuilder.h"
#include "ImageConvert.h"
#include "PathData.h"
#include "LoadScreenSwap.h"

__declspec(dllexport) PathDataParent* PathDataCreate()
{
    return new PathData();
}
__declspec(dllexport) PathBuilderParent* PathBuilderCreate(PathDataParent& _pathData)
{
    return new PathBuilder(_pathData);
}
__declspec(dllexport) ImageConvertParent* ImageConvertCreate(PathDataParent& _pathData)
{
    return new ImageConvert(_pathData);
}

PathDataParent* pathData = PathDataCreate();
PathBuilderParent* PBinst;
PathBuilder* realPBinst;

namespace
{

    constexpr REL::ID kLoadingMenuVtbl{ 172552 };
    constexpr REL::ID kAdvanceMovie{ 618896, 2249218, 2249218 };  // OG, NG, AE
    constexpr std::size_t kAdvanceMovieSlot = 0x4;

    struct AdvanceMovieHook
    {
        // Runs the original first, so a model created this frame is retextured
        // before the frame renders. Tick() does nothing until SetupLoadScreens()
        // has configured it at kGameDataReady.
        static void thunk(RE::LoadingMenu* a_this, float a_timeDelta, std::uint64_t a_time)
        {
            LoadScreenSwap::BeforeAdvance(a_this);  // capture artScreen before the original clears it
            func(a_this, a_timeDelta, a_time);
            LoadScreenSwap::Tick(a_this);
        }

        static inline REL::Relocation<decltype(thunk)> func;  // the original AdvanceMovie
    };

    void InstallHook()
    {
        REL::Relocation<std::uintptr_t> vtbl{ kLoadingMenuVtbl };
        AdvanceMovieHook::func = vtbl.write_vfunc(kAdvanceMovieSlot, AdvanceMovieHook::thunk);

        const auto base = REL::Relocation<std::uintptr_t>{ REL::Offset(0) }.address();
        spdlog::info("Hooked LoadingMenu::AdvanceMovie: vtable RVA={:X} original RVA={:X}",
            vtbl.address() - base, AdvanceMovieHook::func.address() - base);

        // Confirm slot 4 really is AdvanceMovie (by its own ID); if not, put the original back.
        if (AdvanceMovieHook::func.address() != kAdvanceMovie.address()) {
            spdlog::error("Vtable slot 4 is not LoadingMenu::AdvanceMovie (expected RVA {:X}); hook removed",
                kAdvanceMovie.address() - base);
            vtbl.write_vfunc(kAdvanceMovieSlot, AdvanceMovieHook::func.address());
        }
    }
}


void SetupLoadScreens(RE::TESDataHandler* dataHandler, RE::BGSTransform* sharedTransform)
{
    auto* alrPlugin = dataHandler->LookupLoadedLightModByName("ALR.esp"sv);
    if (!alrPlugin) {
        spdlog::warn("TextureSwap: ALR.esp not loaded; skipping");
        return;
    }

    constexpr std::uint32_t kCarrierStatID = 0x1B7;
    RE::TESObjectSTAT* carrier = dataHandler->LookupForm<RE::TESObjectSTAT>(kCarrierStatID, "ALR.esp"sv);
    if (!carrier) {
        // Fallback: the record was renumbered. Use the first STAT in ALR.esp that has a model.
        for (auto* stat : dataHandler->GetFormArray<RE::TESObjectSTAT>()) {
            const char* model = stat->GetModel();
            if (alrPlugin->IsFormInMod(stat->GetFormID()) && model && *model) {
                carrier = stat;
                spdlog::warn("TextureSwap: no STAT at ALR.esp:{:X}; using {:08X} ({}) instead", kCarrierStatID, stat->GetFormID(), model);
                break;
            }
        }
    }
    if (!carrier) {
        spdlog::warn("TextureSwap: ALR.esp has no STAT with a model; skipping");
        return;
    }

    auto& loadScreens = dataHandler->GetFormArray<RE::TESLoadScreen>();

    // Screens without 3D data borrow another screen's LoadNIFData. Found up front so it can never be null.
    RE::TESLoadScreen::LoadNIFData* fallbackNIFData = nullptr;
    for (auto* lscr : loadScreens) {
        if (lscr->loadNIFData) {
            fallbackNIFData = lscr->loadNIFData;
            break;
        }
    }
    if (!fallbackNIFData) {
        spdlog::warn("TextureSwap: no load screen has 3D data to share; skipping");
        return;
    }

    for (auto* lscr : loadScreens) {
        if (!lscr->loadNIFData) {
            lscr->loadNIFData = fallbackNIFData;
        }
        lscr->loadNIFData->loadNif                = carrier;
        lscr->loadNIFData->transform              = sharedTransform;
        lscr->loadNIFData->rotationConstraints[0] = 0;
        lscr->loadNIFData->rotationConstraints[1] = 0;
        lscr->loadNIFData->zoomConstraints[0]     = 0.0f;
        lscr->loadNIFData->zoomConstraints[1]     = 0.0f;
    }
    spdlog::info("TextureSwap: {} load screen(s) use carrier STAT {:08X} ({})",
        loadScreens.size(), carrier->GetFormID(), carrier->GetModel());

    LoadScreenSwap::ApplyScreenScale(sharedTransform);

    LoadScreenSwap::ApplyLoadScreenLighting(realPBinst->LoadScreenLights());

    LoadScreenSwap::Config config;
    config.folder   = realPBinst->OutputPath();
    config.fit      = realPBinst->TextureSwapFitMode() == "stretch" ? LoadScreenSwap::FitMode::kStretch : LoadScreenSwap::FitMode::kCrop;
    config.debugLog = realPBinst->TextureSwapDebug();

    for (const auto& entry : realPBinst->whitelist) {
        auto* lscr = dataHandler->LookupForm<RE::TESLoadScreen>(entry.formID, entry.plugin);
        if (!lscr) {
            spdlog::warn("TextureSwap: whitelisted load screen {:X}:{} not found", entry.formID, entry.plugin);
            continue;
        }

        LoadScreenSwap::Config::Pin pin;
        pin.screen = lscr;
        auto addCandidate = [&pin](std::string a_name) {
            a_name = path::toLower(a_name);
            if (!a_name.empty() && std::find(pin.files.begin(), pin.files.end(), a_name) == pin.files.end()) {
                pin.files.push_back(std::move(a_name));
            }
        };

        const auto fileName = std::filesystem::path(entry.filename).filename();
        addCandidate(std::filesystem::path(fileName).replace_extension(".dds").string());
        addCandidate(PathBuilder::TextureSwapFileName(fileName.string()));

        config.pins.push_back(std::move(pin));
    }
    LoadScreenSwap::Configure(std::move(config));
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
                LoadScreenSwap::SetImagesPending(true);
                ImageConvertParent* IMinst = ImageConvertCreate(*pathData);
                delete IMinst;
            }
            // Generation finished (or wasn't needed): the image folder is ready to use
            LoadScreenSwap::SetImagesPending(false);
            
        });
        // Detach the thread so it runs independently in the background 
        // without blocking F4SE or the game's startup pipeline
        workerThread.detach();
        
    }

    if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
        // Can fire more than once (e.g. after reordering mods in the in-game mod manager);
        // setup re-queries every form, so running it again is safe.
        spdlog::info("Starting Loading Screen replacement...");
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        RE::BGSTransform* sharedTransform = dataHandler->LookupForm<RE::BGSTransform>(0x1B5, "ALR.esp"sv);

        if (!realPBinst) {
            spdlog::warn("Image setup hasn't finished reading ALR.ini yet; loading screens left as vanilla");
            return;
        }
        SetupLoadScreens(dataHandler, sharedTransform);
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);

    auto messaging = F4SE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(OnF4SEMessage);
    } else {
        return false;
    }

    InstallHook();

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
        delete pathData; 
        break;
    }
    return TRUE;
}