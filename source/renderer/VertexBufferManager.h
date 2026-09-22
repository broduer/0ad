/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Allocate and destroy CVertexBuffers
 */

#ifndef INCLUDED_VERTEXBUFFERMANAGER
#define INCLUDED_VERTEXBUFFERMANAGER

#include "renderer/VertexBuffer.h"
#include "renderer/backend/IBuffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace Renderer::Backend { class IDevice; }

// CVertexBufferManager: owner object for CVertexBuffer objects; acts as
// 'front end' for their allocation and destruction
class CVertexBufferManager
{
public:
	CVertexBufferManager(Renderer::Backend::IDevice* device) : m_Device(device) {}
	~CVertexBufferManager() { ReturnReleased(); }

	enum class Group : std::uint32_t
	{
		DEFAULT,
		TERRAIN,
		WATER,
		COUNT
	};

	// Helper for automatic VBChunk lifetime management.
	class Handle
	{
	public:
		Handle() = default;
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;

		Handle(CVertexBufferManager& manager, CVertexBuffer::VBChunk* chunk);
		Handle(Handle&& other);
		Handle& operator=(Handle&& other);

		~Handle() { Reset(); }

		bool IsValid() const { return m_Chunk != nullptr; }
		explicit operator bool() const { return IsValid(); }
		bool operator!() const { return !static_cast<bool>(*this); }
		void Reset();

		friend void swap(Handle& lhs, Handle& rhs)
		{
			using std::swap;

			swap(lhs.m_Manager, rhs.m_Manager);
			swap(lhs.m_Chunk, rhs.m_Chunk);
		}

		CVertexBuffer::VBChunk& operator*() const { return *m_Chunk; }
		CVertexBuffer::VBChunk* operator->() const { return m_Chunk; }
		CVertexBuffer::VBChunk* Get() const { return m_Chunk; }

	private:
		CVertexBufferManager* m_Manager = nullptr;
		CVertexBuffer::VBChunk* m_Chunk = nullptr;
	};

	/**
	 * Try to allocate a vertex buffer of the given size and type.
	 *
	 * @param vertexSize size of each vertex in the buffer
	 * @param numberOfVertices number of vertices in the buffer
	 * @param type buffer type
	 * @param dynamic will be buffer updated frequently or not
	 * @param backingStore if not dynamic, this is nullptr; else for dynamic,
	 *            this must be a copy of the vertex data that remains valid for the
	 *            lifetime of the VBChunk
	 * @return chunk, or empty handle if no free chunks available
	 */
	Handle AllocateChunk(
		const size_t vertexSize, const size_t numberOfVertices,
		const Renderer::Backend::IBuffer::Type type,
		const uint32_t usage, void* backingStore = nullptr, Group group = Group::DEFAULT);

	/**
	 * Queue @p chunk to return to its buffer at the start of the next frame.
	 * Callable from any thread, since render data can be destroyed on the
	 * logic thread while the render thread is paused.
	 */
	void Release(CVertexBuffer::VBChunk* chunk);

	/**
	 * Return the released chunks to their buffers. Render thread.
	 */
	void ReturnReleased();

	size_t GetBytesReserved() const;
	size_t GetBytesAllocated() const;

private:
	Renderer::Backend::IDevice* m_Device{nullptr};

	/// List of all known vertex buffers
	std::vector<std::unique_ptr<CVertexBuffer>> m_Buffers[static_cast<std::size_t>(Group::COUNT)];

	std::mutex m_ReleasedMutex;
	std::vector<CVertexBuffer::VBChunk*> m_Released;
	std::vector<CVertexBuffer::VBChunk*> m_Returning;
};

#endif // INCLUDED_VERTEXBUFFERMANAGER
