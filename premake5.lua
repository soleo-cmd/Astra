workspace "Astra"
    architecture "x64"
    configurations { "Debug", "Release", "Dist" }
    startproject "Astra"

    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

    project "Astra"
        kind "ConsoleApp"
        language "C++"
        cppdialect "C++20"
        staticruntime "on"

        targetdir ("bin/" .. outputdir .. "/%{prj.name}")
        objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

        files
        {
            "src/**.hpp",
            "src/**.cpp"
        }

        -- SIMD Support Configuration
        filter "system:windows"
            systemversion "latest"
            buildoptions { 
                "/Zc:__cplusplus",      -- Enable proper __cplusplus macro
                "/arch:AVX",            -- Enable up to AVX (includes SSE4.2)
                "/diagnostics:column",  -- Show column info in errors
                "/diagnostics:caret",   -- Show carets pointing to errors
                "/bigobj"               -- Allow larger object files (helps with templates)
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
                "-fdiagnostics-color=always",    -- Colored compiler output
                "-ftemplate-backtrace-limit=0",  -- Full template error traces
                "-msse2",               -- Enable SSE2
                "-msse4.2",             -- Enable SSE4.2 (includes CRC32)
                "-mpclmul"              -- Sometimes needed for CRC32
            }
            
        filter "system:macosx"
            buildoptions { 
                "-Wall", 
                "-Wextra", 
                "-Wpedantic",
                "-fdiagnostics-color=always",    -- Colored compiler output
                "-ftemplate-backtrace-limit=0",  -- Full template error traces
                "-msse2",               -- Enable SSE2
                "-msse4.2"              -- Enable SSE4.2
            }

        filter "configurations:Debug"
            runtime "Debug"
            symbols "on"
            optimize "off"
            exceptionhandling "off"
            rtti "off"
            defines { "ASTRA_BUILD_DEBUG" }

        filter "configurations:Release"
            runtime "Release"
            optimize "full"
            symbols "on"  -- Keep symbols for profiling
            exceptionhandling "off"
            rtti "off"
            defines { "ASTRA_BUILD_RELEASE" }

        filter { "configurations:Release", "system:windows" }
            buildoptions { "/O2", "/Zi" }
            linkoptions { "/DEBUG" }

        filter "configurations:Dist"
            runtime "Release"
            optimize "full"
            symbols "off"  -- Strip symbols for distribution
            exceptionhandling "off"
            rtti "off"
            defines { "ASTRA_BUILD_DIST" }

        filter { "configurations:Dist", "system:windows" }
            buildoptions { "/O2" }
