#pragma once
#include <cstddef>
#include <type_traits>

// Forward declaration for comparison operators
template <typename T, typename Alloc>
class STLAdaptor;

template <typename T, typename Alloc>
class STLAdaptor {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = STLAdaptor<U, Alloc>;
    };

    // This is the key change from your original - deleted default constructor
    // because we need an allocator reference
    STLAdaptor() = delete;
    
    STLAdaptor(Alloc& allocator) noexcept : m_allocator(allocator) {}

    // Copy/move constructors
    STLAdaptor(const STLAdaptor& other) noexcept
        : m_allocator(other.m_allocator) {}

    template <typename U>
    STLAdaptor(const STLAdaptor<U, Alloc>& other) noexcept
        : m_allocator(other.m_allocator) {}

    STLAdaptor(STLAdaptor&& other) noexcept
        : m_allocator(other.m_allocator) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        return reinterpret_cast<T*>(
            m_allocator.Allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, [[maybe_unused]] std::size_t n) noexcept {
        m_allocator.Free(p);
    }

    // C++17 and earlier: construct/destroy (C++20+ uses allocator_traits defaults)
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new (p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }

    std::size_t MaxAllocationSize() const noexcept {
        return m_allocator.GetSize();
    }

    // Equality comparison - two allocators are equal if they reference same memory
    template <typename U>
    bool operator==(const STLAdaptor<U, Alloc>& rhs) const noexcept {
        return m_allocator.GetStart() == rhs.m_allocator.GetStart();
    }

    template <typename U>
    bool operator!=(const STLAdaptor<U, Alloc>& rhs) const noexcept {
        return !(*this == rhs);
    }

    Alloc& m_allocator;

private:
    template <typename U, typename A>
    friend class STLAdaptor;
};
