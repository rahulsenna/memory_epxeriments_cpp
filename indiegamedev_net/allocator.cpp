#include "allocator.hpp"
#include <cassert>

Allocator::Allocator(const std::size_t sizeBytes, void *const start) noexcept
		: m_size(sizeBytes),
			m_usedBytes(0),
			m_numAllocations(0),
			m_start(start)
{
	assert(sizeBytes > 0);
}

Allocator::Allocator(Allocator &&other) noexcept
		: m_size(other.m_size),
			m_usedBytes(other.m_usedBytes),
			m_numAllocations(other.m_numAllocations),
			m_start(other.m_start)
{
	other.m_start = nullptr;
	other.m_size = 0;
	other.m_numAllocations = 0;
	other.m_usedBytes = 0;
}

Allocator &Allocator::operator=(Allocator &&rhs) noexcept
{
	m_size = rhs.m_size;
	m_usedBytes = rhs.m_usedBytes;
	m_numAllocations = rhs.m_numAllocations;
	m_start = rhs.m_start;

	rhs.m_start = nullptr;
	rhs.m_size = 0;
	rhs.m_numAllocations = 0;
	rhs.m_usedBytes = 0;

	return *this;
}

Allocator::~Allocator() noexcept
{
	assert(m_numAllocations == 0 && m_usedBytes == 0);
}

const std::size_t &Allocator::GetSize() const noexcept
{
	return m_size;
}

const std::size_t &Allocator::GetUsed() const noexcept
{
	return m_usedBytes;
}

const std::size_t &Allocator::GetNumAllocation() const noexcept
{
	return m_numAllocations;
}

const void *Allocator::GetStart() const noexcept
{
	return m_start;
}