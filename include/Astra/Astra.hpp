#pragma once

// Astra ECS - A high-performance Entity Component System library
// This header includes all Astra ECS headers in the correct dependency order

// Core headers - fundamental types and utilities
#include "Core/Base.hpp"
#include "Core/Version.hpp"
#include "Platform/Hardware.hpp"
#include "Platform/Platform.hpp"
#include "Platform/Simd.hpp"

// Core utilities
#include "Core/Result.hpp"
#include "Core/TypeID.hpp"
#include "Core/Signal.hpp"

// Memory management
#include "Memory/Memory.hpp"
#include "Memory/ChunkPool.hpp"

// Container types
#include "Container/AlignedStorage.hpp"
#include "Container/Bitmap.hpp"
#include "Container/FlatMap.hpp"
#include "Container/SmallVector.hpp"

// Entity system
#include "Entity/Entity.hpp"
#include "Entity/EntityPool.hpp"

// Component system
#include "Component/Component.hpp"
#include "Component/ComponentOps.hpp"
#include "Component/ComponentRegistry.hpp"

// Archetype system
#include "Archetype/ArchetypeEdgeStorage.hpp"
#include "Archetype/Archetype.hpp"
#include "Archetype/ArchetypeStorage.hpp"

// Registry and queries
#include "Registry/Query.hpp"
#include "Registry/View.hpp"
#include "Registry/RelationshipGraph.hpp"
#include "Registry/Relations.hpp"
#include "Registry/Registry.hpp"

// System support
#include "System/System.hpp"

// Serialization (optional - heavier dependencies)
#include "Serialization/SerializationError.hpp"
#include "Serialization/Compression/Compression.hpp"
#include "Serialization/Compression/LZ4Decoder.hpp"
#include "Serialization/BinaryArchive.hpp"
#include "Serialization/BinaryWriter.hpp"
#include "Serialization/BinaryReader.hpp"