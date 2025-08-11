#pragma once

// Astra ECS - A high-performance Entity Component System library
// This header includes all Astra ECS headers in the correct dependency order

#include "Core/Base.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/Platform.hpp"
#include "Platform/Simd.hpp"

#include "Core/Result.hpp"
#include "Core/TypeID.hpp"

#include "Container/Bitmap.hpp"
#include "Memory/ChunkPool.hpp"
#include "Container/FlatMap.hpp"

#include "Entity/Entity.hpp"
#include "Entity/EntityPool.hpp"

#include "Component/Component.hpp"
#include "Component/ComponentRegistry.hpp"

#include "Archetype/Archetype.hpp"
#include "Archetype/ArchetypeStorage.hpp"
#include "Registry/Registry.hpp"
#include "Registry/View.hpp"
