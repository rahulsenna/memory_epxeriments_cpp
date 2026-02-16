#pragma once

#include "allocator.hpp"
#include <memory>
#include <type_traits>
#include <limits>

// STL Adaptor to make custom allocators compatible with STL containers
template <typename T, typename Alloc>
class STLAdaptor
{
public:
	using value_type = T;
	using pointer = T *;
	using const_pointer = const T *;
	using reference = T &;
	using const_reference = const T &;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	template <typename U>
	struct rebind
	{
		using other = STLAdaptor<U, Alloc>;
	};

	// Mandatory constructor: takes a reference to the underlying allocator
	STLAdaptor() = delete;
	STLAdaptor(Alloc &allocator) noexcept : m_allocator(allocator) {}

	// Copy/move constructors
	STLAdaptor(const STLAdaptor &other) noexcept
			: m_allocator(other.m_allocator) {}

	template <typename U>
	STLAdaptor(const STLAdaptor<U, Alloc> &other) noexcept
			: m_allocator(other.m_allocator) {}

	STLAdaptor(STLAdaptor &&other) noexcept
			: m_allocator(other.m_allocator) {}

	// Allocate / deallocate
	[[nodiscard]] T *allocate(std::size_t n)
	{
		return reinterpret_cast<T *>(
				m_allocator.Allocate(n * sizeof(T), alignof(T)));
	}

	void deallocate(T *p, [[maybe_unused]] std::size_t n) noexcept
	{
		m_allocator.Free(p);
	}

	// Construct/destroy (only needed for C++17 and earlier; C++20+ uses allocator_traits defaults)
	template <typename U, typename... Args>
	void construct(U *p, Args &&...args)
	{
		new (p) U(std::forward<Args>(args)...);
	}

	template <typename U>
	void destroy(U *p)
	{
		p->~U();
	}

	pointer address(reference x) const noexcept
	{
		return std::addressof(x);
	}

	const_pointer address(const_reference x) const noexcept
	{
		return std::addressof(x);
	}

	size_type max_size() const noexcept
	{
		return std::numeric_limits<size_type>::max() / sizeof(T);
	}

	Alloc &get_allocator() const noexcept { return m_allocator; }

	// Equality comparison
	template <typename U>
	bool operator==(const STLAdaptor<U, Alloc> &rhs) const noexcept
	{
		return m_allocator.GetStart() == rhs.m_allocator.GetStart();
	}

	template <typename U>
	bool operator!=(const STLAdaptor<U, Alloc> &rhs) const noexcept
	{
		return !(*this == rhs);
	}

private:
	Alloc &m_allocator;

	template <typename U, typename A>
	friend class STLAdaptor;
};
