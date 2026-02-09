#pragma once
#define _CRT_SECURE_NO_WARNINGS 1
#define _WIN32_WINNT_WIN10 0x0A00

#define PLUGIN_VERSION 1
#define PLUGIN_NAME "ALR"

#define PLUGIN_PATH_LEN 20
#define MAX_INPUTS 455

#define NOMINMAX

#include <Windows.h>
#include <DirectXTex-mar2025/DirectXTex/DirectXTex.h>
#include <comdef.h>
#include <memory>
#include <thread>
#include <process.h>

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>

#include <random>
#include <vector>
#include <array>
#include <unordered_map>
#include <shlobj.h>
#include <limits>
#include <numeric>
#include <algorithm>
#include <cctype>

#include "common/ITypes.h"
#include "common/IDebugLog.h"
#include "f4se_common/Utilities.h"
#include "F4SE/PluginAPI.h"
#include "f4se_common/f4se_version.h"

using namespace std;
using namespace DirectX;