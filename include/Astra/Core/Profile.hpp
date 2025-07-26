#pragma once

#include "Base.hpp"

// Tracy integration - only enabled in Release builds with TRACY_ENABLE
#if defined(ASTRA_BUILD_RELEASE) && defined(TRACY_ENABLE)
    #include <tracy/Tracy.hpp>
    
    // Zone scoped profiling - tracks execution time of current scope
    #define ASTRA_PROFILE_ZONE() ZoneScoped
    #define ASTRA_PROFILE_ZONE_NAMED(name) ZoneScopedN(name)
    #define ASTRA_PROFILE_ZONE_COLOR(color) ZoneScopedC(color)
    #define ASTRA_PROFILE_ZONE_NAMED_COLOR(name, color) ZoneScopedNC(name, color)
    
    // Function profiling - automatically names zone after function
    #define ASTRA_PROFILE_FUNCTION() ZoneScoped
    
    // Custom zone with dynamic text
    #define ASTRA_PROFILE_ZONE_TEXT(text, size) ZoneText(text, size)
    #define ASTRA_PROFILE_ZONE_VALUE(value) ZoneValue(value)
    
    // Frame marking for game loops
    #define ASTRA_PROFILE_FRAME_MARK() FrameMark
    #define ASTRA_PROFILE_FRAME_MARK_NAMED(name) FrameMarkNamed(name)
    
    // Memory profiling
    #define ASTRA_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
    #define ASTRA_PROFILE_FREE(ptr) TracyFree(ptr)
    
    // Lock profiling
    #define ASTRA_PROFILE_LOCKABLE(type, var) TracyLockable(type, var)
    #define ASTRA_PROFILE_LOCKABLE_NAMED(type, var, name) TracyLockableN(type, var, name)
    
    // Plot values over time
    #define ASTRA_PROFILE_PLOT(name, val) TracyPlot(name, val)
    
    // Messages
    #define ASTRA_PROFILE_MESSAGE(text, size) TracyMessage(text, size)
    #define ASTRA_PROFILE_MESSAGE_COLOR(text, size, color) TracyMessageC(text, size, color)
    
    // App info
    #define ASTRA_PROFILE_APP_INFO(text, size) TracyAppInfo(text, size)
    
    // GPU profiling would go here if needed
    
#else
    // No-op macros when profiling is disabled
    #define ASTRA_PROFILE_ZONE()
    #define ASTRA_PROFILE_ZONE_NAMED(name)
    #define ASTRA_PROFILE_ZONE_COLOR(color)
    #define ASTRA_PROFILE_ZONE_NAMED_COLOR(name, color)
    
    #define ASTRA_PROFILE_FUNCTION()
    
    #define ASTRA_PROFILE_ZONE_TEXT(text, size)
    #define ASTRA_PROFILE_ZONE_VALUE(value)
    
    #define ASTRA_PROFILE_FRAME_MARK()
    #define ASTRA_PROFILE_FRAME_MARK_NAMED(name)
    
    #define ASTRA_PROFILE_ALLOC(ptr, size)
    #define ASTRA_PROFILE_FREE(ptr)
    
    #define ASTRA_PROFILE_LOCKABLE(type, var) type var
    #define ASTRA_PROFILE_LOCKABLE_NAMED(type, var, name) type var
    
    #define ASTRA_PROFILE_PLOT(name, val)
    
    #define ASTRA_PROFILE_MESSAGE(text, size)
    #define ASTRA_PROFILE_MESSAGE_COLOR(text, size, color)
    
    #define ASTRA_PROFILE_APP_INFO(text, size)
#endif

// Color constants for profiling zones (matches Tracy's color scheme)
namespace Astra::Profile
{
    constexpr uint32_t ColorDefault = 0x000000;
    constexpr uint32_t ColorSystem = 0xDD0000;
    constexpr uint32_t ColorUpdate = 0x00DD00;
    constexpr uint32_t ColorRender = 0x0000DD;
    constexpr uint32_t ColorPhysics = 0xDDDD00;
    constexpr uint32_t ColorAI = 0xDD00DD;
    constexpr uint32_t ColorNetwork = 0x00DDDD;
    constexpr uint32_t ColorIO = 0x808080;
    constexpr uint32_t ColorMemory = 0xFF8800;
    constexpr uint32_t ColorWait = 0xFF0088;
    
    // ECS-specific colors
    constexpr uint32_t ColorEntity = 0x88FF00;
    constexpr uint32_t ColorComponent = 0x0088FF;
    constexpr uint32_t ColorView = 0xFF0088;
    constexpr uint32_t ColorQuery = 0x8800FF;
}