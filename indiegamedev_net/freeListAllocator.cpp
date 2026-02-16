#include "freeListAllocator.hpp"
#include "memory_utils.hpp"
#include <cassert>
#include <new>
#include <utility>

FreeListAllocator::FreeListAllocator(const std::size_t sizeBytes,
																		 void *const start) noexcept
		: Allocator(sizeBytes, start), m_freeBlocks(nullptr)
{
	m_freeBlocks = reinterpret_cast<FreeBlock *>(start);
	m_freeBlocks->size = sizeBytes;
	m_freeBlocks->next = nullptr;
}

FreeListAllocator::FreeListAllocator(FreeListAllocator &&other) noexcept
		: Allocator(std::move(other)), m_freeBlocks(other.m_freeBlocks)
{
	other.m_freeBlocks = nullptr;
}

FreeListAllocator &FreeListAllocator::operator=(FreeListAllocator &&rhs) noexcept
{
	Allocator::operator=(std::move(rhs));
	m_freeBlocks = rhs.m_freeBlocks;
	rhs.m_freeBlocks = nullptr;
	return *this;
}

FreeListAllocator::~FreeListAllocator() noexcept
{
	m_freeBlocks = nullptr;
}

void *FreeListAllocator::Allocate(const std::size_t &size,
																	const std::uintptr_t &alignment)
{
	assert(size > 0 && alignment > 0);

	FreeBlock *prevFreeBlock = nullptr;
	FreeBlock *freeBlock = m_freeBlocks;
	FreeBlock *bestFitPrev = nullptr;
	FreeBlock *bestFit = nullptr;
	std::uintptr_t bestFitAdjustment = 0;
	std::size_t bestFitTotalSize = 0;

	while (freeBlock != nullptr)
	{
		std::uintptr_t adjustment =
				align_forward_adjustment_with_header<AllocationHeader>(
						freeBlock, alignment);

		std::size_t totalSize = size + adjustment;

		if (freeBlock->size >= totalSize &&
				(bestFit == nullptr || freeBlock->size < bestFit->size))
		{
			bestFitPrev = prevFreeBlock;
			bestFit = freeBlock;
			bestFitAdjustment = adjustment;
			bestFitTotalSize = totalSize;

			if (freeBlock->size == totalSize)
				break;
		}

		prevFreeBlock = freeBlock;
		freeBlock = freeBlock->next;
	}

	if (bestFit == nullptr)
		throw std::bad_alloc();

	if (bestFit->size - bestFitTotalSize <= sizeof(AllocationHeader))
	{
		bestFitTotalSize = bestFit->size;

		if (bestFitPrev != nullptr)
			bestFitPrev->next = bestFit->next;
		else
			m_freeBlocks = bestFit->next;
	}
	else
	{
		FreeBlock *newBlock = reinterpret_cast<FreeBlock *>(ptr_add(bestFit, bestFitTotalSize));
		newBlock->size = bestFit->size - bestFitTotalSize;
		newBlock->next = bestFit->next;

		if (bestFitPrev != nullptr)
			bestFitPrev->next = newBlock;
		else
			m_freeBlocks = newBlock;
	}

	std::uintptr_t alignedAddr = reinterpret_cast<std::uintptr_t>(bestFit) + bestFitAdjustment;
	auto *header = reinterpret_cast<AllocationHeader *>(
			alignedAddr - sizeof(AllocationHeader));

	header->size = bestFitTotalSize;
	header->adjustment = bestFitAdjustment;

	m_usedBytes += bestFitTotalSize;
	++m_numAllocations;

	return reinterpret_cast<void *>(alignedAddr);
}

void FreeListAllocator::Free(void *const ptr) noexcept
{
	assert(ptr != nullptr);

	auto *header = reinterpret_cast<AllocationHeader *>(
			ptr_sub(ptr, sizeof(AllocationHeader)));

	std::uintptr_t blockStart =
			reinterpret_cast<std::uintptr_t>(ptr) - header->adjustment;
	std::size_t blockSize = header->size;
	std::uintptr_t blockEnd = blockStart + blockSize;

	FreeBlock *prevFreeBlock = nullptr;
	FreeBlock *freeBlock = m_freeBlocks;

	while (freeBlock != nullptr)
	{
		if (reinterpret_cast<std::uintptr_t>(freeBlock) >= blockEnd)
			break;

		prevFreeBlock = freeBlock;
		freeBlock = freeBlock->next;
	}

	FreeBlock *newBlock = nullptr;

	if (prevFreeBlock == nullptr)
	{
		newBlock = reinterpret_cast<FreeBlock *>(blockStart);
		newBlock->size = blockSize;
		newBlock->next = m_freeBlocks;
		m_freeBlocks = newBlock;
	}
	else if (reinterpret_cast<std::uintptr_t>(prevFreeBlock) +
							 prevFreeBlock->size ==
					 blockStart)
	{
		prevFreeBlock->size += blockSize;
		newBlock = prevFreeBlock;
	}
	else
	{
		newBlock = reinterpret_cast<FreeBlock *>(blockStart);
		newBlock->size = blockSize;
		newBlock->next = prevFreeBlock->next;
		prevFreeBlock->next = newBlock;
	}

	if (newBlock->next != nullptr &&
			reinterpret_cast<std::uintptr_t>(newBlock) + newBlock->size ==
					reinterpret_cast<std::uintptr_t>(newBlock->next))
	{
		newBlock->size += newBlock->next->size;
		newBlock->next = newBlock->next->next;
	}

	--m_numAllocations;
	m_usedBytes -= blockSize;
}
