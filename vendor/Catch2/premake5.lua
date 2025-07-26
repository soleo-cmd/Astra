-- Catch2 library
project "Catch2"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/catch2/**.hpp",
        "src/catch2/**.cpp"
    }

    includedirs
    {
        "src"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"
        optimize "off"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        symbols "on"  -- Keep symbols for debugging/profiling

    filter "configurations:Dist"
        runtime "Release"
        optimize "full"
        symbols "off"  -- No symbols for distribution