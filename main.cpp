// main.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "PathBuilder.h"
#include "PathDestroyer.h"
#include "ImageConvert.h"
#include "PathData.h"

//IDebugLog glog;
//PluginHandle g_pluginHandle = kPluginHandle_Invalid;

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
PathDataParent* pathData = PathDataCreate();


F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);
	PathBuilderParent* PBinst = PathBuilderCreate(*pathData);
	if (PBinst->IsImageRD()) {
		PathBuilder* realPBinst = static_cast<PathBuilder*>(PBinst);

		ImageConvertParent* IMinst = ImageConvertCreate(*pathData, realPBinst->whitelistFileNames);
		delete IMinst;
	}

	delete PBinst;
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
	// Perform actions based on the reason for calling.
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.
		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		break;

	case DLL_THREAD_DETACH:

		// Do thread-specific cleanup.
		break;

	case DLL_PROCESS_DETACH:

		delete &pathData;
		// Perform any necessary cleanup.
		break;
	}

	return TRUE;  // Successful DLL_PROCESS_ATTACH.
}