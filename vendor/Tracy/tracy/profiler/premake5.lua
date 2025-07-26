workspace "TracyProfiler"
    architecture "x64"
    configurations { "Debug", "Release" }
    
project "TracyProfiler"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++17"
    
    targetdir "bin/%{cfg.buildcfg}"
    objdir "bin-int/%{cfg.buildcfg}"
    
    files {
        "src/**.cpp",
        "src/**.hpp",
        "src/**.h",
        "../public/TracyClient.cpp",
        "../server/**.cpp",
        "../server/**.hpp",
        "../common/**.cpp",
        "../common/**.hpp",
        "../imgui/**.cpp",
        "../imgui/**.h",
        "../nfd/nfd_win.cpp",
        "../nfd/nfd.h",
        "../getopt/getopt.c",
        "../getopt/getopt.h"
    }
    
    includedirs {
        "src",
        "..",
        "../imgui",
        "../server",
        "../common",
        "../public",
        "../nfd",
        "../getopt",
        "../../../glfw/include",  -- Adjust path if needed
        "../../../capstone/include",  -- Adjust path if needed
        "../../../freetype/include"  -- Adjust path if needed
    }
    
    defines {
        "TRACY_NO_STATISTICS",
        "NOMINMAX",
        "_CRT_SECURE_NO_WARNINGS",
        "_CRT_NONSTDC_NO_DEPRECATE",
        "WIN32_LEAN_AND_MEAN",
        "_WIN32_WINNT=0x0601",
        "IMGUI_IMPL_OPENGL_LOADER_CUSTOM"
    }
    
    links {
        "ws2_32",
        "winmm",
        "opengl32",
        "imm32",
        "dbghelp",
        "d3d11",
        "dxgi"
    }
    
    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"
        optimize "off"
        
    filter "configurations:Release"
        runtime "Release"
        optimize "full"
        symbols "on"
        
    filter "system:windows"
        systemversion "latest"
        files {
            "src/winmain.cpp",
            "build/win32/Tracy.rc"
        }
        removefiles {
            "src/main.cpp"
        }