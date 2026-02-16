#pragma once

#include "allocator.hpp"

class FreeListAllocator : public Allocator
{
public:
	struct AllocationHeader
	{
		std::size_t size;
		std::uintptr_t adjustment;
	};

	struct FreeBlock
	{
		std::size_t size;
		FreeBlock *next;
	};

	FreeListAllocator(const std::size_t sizeBytes, void *const start) noexcept;
	FreeListAllocator(const FreeListAllocator &) = delete;
	FreeListAllocator &operator=(const FreeListAllocator &) = delete;
	FreeListAllocator(FreeListAllocator &&) noexcept;
	FreeListAllocator &operator=(FreeListAllocator &&) noexcept;
	virtual ~FreeListAllocator() noexcept;

	virtual void *Allocate(const std::size_t &size,
												 const std::uintptr_t &alignment = sizeof(std::intptr_t)) override;
	virtual void Free(void *const ptr) noexcept override;

private:
	FreeBlock *m_freeBlocks;
};
