#include "linearAllocator.hpp"
#include <cassert>
#include <utility>
#include <new> // std::bad_alloc();

inline std::size_t align_forward_adjustment(const void *const ptr, const std::size_t &alignment) noexcept
{
	const auto iptr = reinterpret_cast<std::uintptr_t>(ptr);
	const auto aligned = (iptr - 1u + alignment) & -alignment;
	return aligned - iptr;
}

inline void *ptr_add(const void *const p, const std::uintptr_t &amount) noexcept
{
	return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(p) + amount);
}

LinearAllocator::LinearAllocator(const std::size_t sizeBytes,
																 void *const start) noexcept
		: Allocator(sizeBytes, start),
			m_current(const_cast<void *>(start))
{
}

LinearAllocator::LinearAllocator(LinearAllocator &&other) noexcept
		: Allocator(std::move(other)),
			m_current(other.m_current)
{
	other.m_current = nullptr;
}

LinearAllocator::~LinearAllocator() noexcept
{
	Clear();
}

LinearAllocator &LinearAllocator::operator=(LinearAllocator &&rhs) noexcept
{
	Allocator::operator=(std::move(rhs));
	m_current = rhs.m_current;
	rhs.m_current = nullptr;
	return *this;
}

void *LinearAllocator::Allocate(const std::size_t &size,
																const std::uintptr_t &alignment)
{
	assert(size > 0 && alignment > 0);

	std::size_t adjustment = align_forward_adjustment(m_current, alignment);

	if (m_usedBytes + adjustment + size > m_size)
		throw std::bad_alloc();

	void *alignedAddr = ptr_add(m_current, adjustment);

	m_current = ptr_add(alignedAddr, size);

	m_usedBytes = reinterpret_cast<std::uintptr_t>(m_current) - reinterpret_cast<std::uintptr_t>(m_start);

	++m_numAllocations;

	return alignedAddr;
}

void LinearAllocator::Free([[maybe_unused]] void *const ptr) noexcept
{
	// you can't free from a linear allocator
}

void LinearAllocator::Rewind(void *const mark) noexcept
{
	assert(m_current >= mark && m_start <= mark);

	m_current = mark;

	m_usedBytes = reinterpret_cast<std::uintptr_t>(m_current) - reinterpret_cast<std::uintptr_t>(m_start);
}

void LinearAllocator::Clear() noexcept
{
	m_numAllocations = 0;
	m_usedBytes = 0;
	m_current = const_cast<void *>(m_start);
}

void *LinearAllocator::GetCurrent() const noexcept
{
	return m_current;
}