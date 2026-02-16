#pragma once

#include "allocator.hpp"

class LinearAllocator : public Allocator
{
public:
	LinearAllocator(const std::size_t sizeBytes,
									void *const start) noexcept;

	LinearAllocator(const LinearAllocator &) = delete;
	LinearAllocator &operator=(const LinearAllocator &) = delete;
	LinearAllocator(LinearAllocator &&) noexcept;
	LinearAllocator &operator=(LinearAllocator &&) noexcept;

	virtual void *Allocate(const std::size_t &size,
												 const std::uintptr_t &alignment = sizeof(std::intptr_t)) override;

	virtual void Free(void *const ptr) noexcept override final;

	void *GetCurrent() const noexcept;

	virtual void Rewind(void *const mark) noexcept = 0;
	virtual void Clear() noexcept = 0;

protected:
	void *m_current;
};