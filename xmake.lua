-- include subprojects
includes("lib/commonlibf4")

local plugin_name = "ALR"
local plugin_version = "2.0.1"
local plugin_version_major, plugin_version_minor, plugin_version_patch = plugin_version:match("^(%d+)%.(%d+)%.(%d+)$")

add_requires("openmp")

-- set project constants
set_project("ALR_Fallout4")
set_version(plugin_version)
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
--set_runtimes("MT")

add_cxflags("/wd4244") -- disable warning C4244

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
--add_rules("mode.release", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")
-- override runtime count
add_defines("COMMONLIB_RUNTIMECOUNT=3")

-- define targets
target(plugin_name)
    add_rules("commonlibf4.plugin", {
        name = plugin_name,
        author = "srangermods",
        description = "improved ALR built using commonlibf4"
    })

    add_packages("openmp")

    -- add src files
    add_files(
        "main.cpp",
        "PathBuilder.cpp",
        "PathDestroyer.cpp",
        "imageConvert.cpp",
        "UI.cpp",
        "cmd.cpp",
        "pch.cpp"
    )
    add_headerfiles("**.h")
    add_includedirs("DirectXTex/DirectXTex")
    add_links("extern/lib/DirectXTex") 
    set_pcxxheader("pch.h")
    
     -- pass name and version
    add_defines(
        'PLUGIN_NAME="' .. plugin_name .. '"',
        "PLUGIN_VERSION_MAJOR=" .. plugin_version_major,
        "PLUGIN_VERSION_MINOR=" .. plugin_version_minor,
        "PLUGIN_VERSION_PATCH=" .. plugin_version_patch
    )

