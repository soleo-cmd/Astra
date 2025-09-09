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
#include "Core/Delegate.hpp"

// Memory management
#include "Core/Memory.hpp"

// Container types
#include "Container/AlignedStorage.hpp"
#include "Container/Bitmap.hpp"
#include "Container/FlatMap.hpp"
#include "Container/FlatSet.hpp"
#include "Container/SmallVector.hpp"
#include "Container/Swiss.hpp"

// Entity system
#include "Entity/Entity.hpp"
#include "Entity/EntityIDStack.hpp"
#include "Entity/EntityTable.hpp"
#include "Entity/EntityRange.hpp"
#include "Entity/EntityManager.hpp"

// Component system
#include "Component/Component.hpp"
#include "Component/ComponentRegistry.hpp"

// Archetype system
#include "Archetype/ArchetypeChunkPool.hpp"
#include "Archetype/Archetype.hpp"
#include "Archetype/ArchetypeGraph.hpp"
#include "Archetype/ArchetypeManager.hpp"

// Registry and queries
#include "Registry/Query.hpp"
#include "Registry/View.hpp"
#include "Registry/RelationshipGraph.hpp"
#include "Registry/Relations.hpp"
#include "Registry/Registry.hpp"

// Command buffer system
#include "Commands/CommandTypes.hpp"
#include "Commands/CommandStorage.hpp"
#include "Commands/CommandExecutor.hpp"
#include "Commands/CommandBuffer.hpp"

// System support
#include "System/System.hpp"
#include "System/SystemMetadata.hpp"
#include "System/SystemExecutor.hpp"
#include "System/SystemScheduler.hpp"

// Serialization (optional - heavier dependencies)
#include "Serialization/SerializationError.hpp"
#include "Serialization/Compression/Compression.hpp"
#include "Serialization/Compression/LZ4Decoder.hpp"
#include "Serialization/BinaryArchive.hpp"
#include "Serialization/BinaryWriter.hpp"
#include "Serialization/BinaryReader.hpp"