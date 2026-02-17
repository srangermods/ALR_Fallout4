-- include subprojects
includes("lib/commonlibf4")

-- set project constants
set_project("ALR_Fallout4")
set_version("1.0.1")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_runtimes("MT")

add_cxflags("/wd4244") -- disable warning C4244

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- define targets
target("ALR")
    add_rules("commonlibf4.plugin", {
        name = "ALR-Sranger",
        author = "srangermods",
        description = "improved ALR built using commonlibf4 from libxe"
    })

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
    add_includedirs("DirectXTex-mar2025/DirectXTex")
    add_links("extern/lib/DirectXTex") 
    set_pcxxheader("pch.h")

