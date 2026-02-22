#pragma once
#include "allocator.hpp"
#include "utils.hpp"

class LinearAllocator : public Allocator {
public:
    LinearAllocator(const std::size_t sizeBytes, void* const start) noexcept
        : Allocator(sizeBytes, start), m_current(start) {}

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    
    LinearAllocator(LinearAllocator&& other) noexcept
        : Allocator(std::move(other)), m_current(other.m_current) {
        other.m_current = nullptr;
    }
    
    LinearAllocator& operator=(LinearAllocator&& rhs) noexcept {
        Allocator::operator=(std::move(rhs));
        m_current = rhs.m_current;
        rhs.m_current = nullptr;
        return *this;
    }

    ~LinearAllocator() noexcept { Clear(); }

    void* Allocate(const std::size_t& size, const std::uintptr_t& alignment) override {
        assert(size > 0 && alignment > 0);

        std::size_t adjustment = align_forward_adjustment(m_current, alignment);

        if (m_usedBytes + adjustment + size > m_size)
            throw std::bad_alloc();

        void* alignedAddr = ptr_add(m_current, adjustment);
        m_current = ptr_add(alignedAddr, size);

        m_usedBytes = reinterpret_cast<std::uintptr_t>(m_current)
                    - reinterpret_cast<std::uintptr_t>(m_start);
        ++m_numAllocations;

        return alignedAddr;
    }

    void Free(void* const ptr) noexcept override {
        // Linear allocator doesn't support individual deallocation
        (void)ptr;
    }

    void* GetCurrent() const noexcept { return m_current; }

    void Rewind(void* const mark) noexcept {
        assert(m_current >= mark && m_start <= mark);
        m_current = mark;
        m_usedBytes = reinterpret_cast<std::uintptr_t>(m_current)
                    - reinterpret_cast<std::uintptr_t>(m_start);
    }

    void Clear() noexcept {
        m_numAllocations = 0;
        m_usedBytes = 0;
        m_current = m_start;
    }

protected:
    void* m_current;
};
