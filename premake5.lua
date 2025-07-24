workspace "Astra"
    architecture "x64"
    configurations { "Debug", "Release" }
    startproject "AstraTests"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Astra"
    kind "None"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "include/**.hpp",
        "include/**.h"
    }

    includedirs
    {
        "include"
    }

    filter "configurations:Debug"
        defines { "ASTRA_BUILD_DEBUG" }
        runtime "Debug"
        symbols "on"
        optimize "off"
        exceptionhandling "off"
        rtti "off"

    filter "configurations:Release"
        defines { "ASTRA_BUILD_RELEASE" }
        runtime "Release"
        optimize "full"
        symbols "off"
        exceptionhandling "off"
        rtti "off"

project "AstraTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "tests/**.cpp",
        "tests/**.h"
    }

    includedirs
    {
        "include"
    }

    -- SIMD Support Configuration
    filter "system:windows"
        systemversion "latest"
        buildoptions { 
            "/Zc:__cplusplus",     -- Enable proper __cplusplus macro
            "/arch:AVX"             -- Enable up to AVX (includes SSE4.2)
        }
        defines { 
            "__SSE2__",             -- Define SSE2 support
            "__SSE4_2__"            -- Define SSE4.2 support (for CRC32)
        }

    filter "system:linux"
        links { "pthread" }
        buildoptions { 
            "-Wall", 
            "-Wextra", 
            "-Wpedantic",
            "-msse2",               -- Enable SSE2
            "-msse4.2",             -- Enable SSE4.2 (includes CRC32)
            "-mpclmul"              -- Sometimes needed for CRC32
        }
        
    filter "system:macosx"
        buildoptions { 
            "-Wall", 
            "-Wextra", 
            "-Wpedantic",
            "-msse2",               -- Enable SSE2
            "-msse4.2"              -- Enable SSE4.2
        }

    -- Platform-specific optimizations
    filter { "system:windows", "configurations:Release" }
        buildoptions { 
            "/O2",                  -- Maximum optimization
            "/Oi",                  -- Enable intrinsic functions
            "/Ot",                  -- Favor speed
            "/GL"                   -- Whole program optimization
        }
        linkoptions { "/LTCG" }     -- Link-time code generation

    filter { "system:linux", "configurations:Release" }
        buildoptions { 
            "-march=native",        -- Use all available CPU features
            "-mtune=native",        -- Tune for the current CPU
            "-ffast-math",          -- Fast math optimizations
            "-funroll-loops"        -- Unroll loops for performance
        }
        
    filter { "system:macosx", "configurations:Release" }
        buildoptions { 
            "-march=native",        -- Use all available CPU features
            "-mtune=native",        -- Tune for the current CPU
            "-ffast-math"           -- Fast math optimizations
        }

    filter "configurations:Debug"
        defines { "ASTRA_BUILD_DEBUG" }
        runtime "Debug"
        symbols "on"
        optimize "off"
        exceptionhandling "off"
        rtti "off"

    filter "configurations:Release"
        defines { "ASTRA_BUILD_RELEASE" }
        runtime "Release"
        optimize "full"
        symbols "off"
        exceptionhandling "off"
        rtti "off"