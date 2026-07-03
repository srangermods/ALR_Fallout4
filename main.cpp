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
__declspec(dllexport) ImageConvertParent* ImageConvertCreate(PathDataParent& _pathData, const std::vector<std::string>& whitelistFileNames)
{
    return new ImageConvert(_pathData, whitelistFileNames);
}

// Keep this global pointer allocation
PathDataParent* pathData = PathDataCreate();

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
    if (a_msg->type == F4SE::MessagingInterface::kPostPostLoad) {
        
        // Spawn a completely detached native thread to isolate OpenMP from the game's loading thread
        std::thread workerThread([]() {
            
            // Give the GOG client hooks 200ms to cleanly finish loading their tracking structures
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            PathBuilderParent* PBinst = PathBuilderCreate(*pathData);
            if (PBinst->IsImageRD()) {
                PathBuilder* realPBinst = static_cast<PathBuilder*>(PBinst);

                ImageConvertParent* IMinst = ImageConvertCreate(*pathData, realPBinst->whitelistFileNames);
                delete IMinst;
            }
            delete PBinst;
        });

        // Detach the thread so it runs independently in the background 
        // without blocking F4SE or the game's startup pipeline
        workerThread.detach();
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