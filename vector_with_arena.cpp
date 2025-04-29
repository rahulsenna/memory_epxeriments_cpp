#include <vector>
#include <cstddef>
#include <cassert>
#include <memory>
#include <new>
#include <unistd.h>
#include <iostream>

// Node for free list
struct FreeBlock {
    FreeBlock* next;
};

// A slightly smarter arena
class MemoryArena {
public:
    explicit MemoryArena(const std::size_t size) {
        data = new uint8_t[size];
        capacity = size;
        offset = 0;
        free_list = nullptr;
    }

    ~MemoryArena() {
        delete[] data;
    }

    void* allocate(const std::size_t size, const std::size_t alignment = alignof(std::max_align_t)) {
        // First try to reuse a block from the free list
        if (free_list) {
            void* ptr = free_list;
            free_list = free_list->next;
            return ptr;
        }

        // Otherwise bump allocation
        const auto current = reinterpret_cast<std::size_t>(data + offset);
        const std::size_t aligned = (current + alignment - 1) & ~(alignment - 1);
        const std::size_t next_offset = aligned - reinterpret_cast<std::size_t>(data) + size;

        if (next_offset > capacity) {
            throw std::bad_alloc();
        }

        const auto ptr = reinterpret_cast<void*>(aligned);
        offset = next_offset;
        return ptr;
    }

    void deallocate(void* ptr, std::size_t size) noexcept {
        // Push the freed block onto the free list
        const auto block = static_cast<FreeBlock*>(ptr);
        // block->next = free_list;
        // free_list = block;
    }

    void reset() {
        offset = 0;
        free_list = nullptr;
    }

private:
    uint8_t* data;
    std::size_t capacity;
    std::size_t offset;
    FreeBlock* free_list;
};

// Custom allocator
template<typename T>
struct ArenaAllocator {
    using value_type = T;

    MemoryArena* arena;

    explicit ArenaAllocator(MemoryArena* a) noexcept : arena(a) {}
    
    template<typename U>
    explicit ArenaAllocator(const ArenaAllocator<U>& other) noexcept : arena(other.arena) {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(arena->allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        arena->deallocate(p, n * sizeof(T));
    }
};

template<typename T, typename U>
bool operator==(const ArenaAllocator<T>& a, const ArenaAllocator<U>& b) {
    return a.arena == b.arena;
}

template<typename T, typename U>
bool operator!=(const ArenaAllocator<T>& a, const ArenaAllocator<U>& b) {
    return !(a == b);
}

// Example usage
int main()
{
    MemoryArena arena(1024ULL*1024ULL*1024ULL); // 1MB arena

    std::vector<uint32_t, ArenaAllocator<uint32_t>> numbers{ArenaAllocator<uint32_t>(&arena)};
    // numbers.reserve(1000ULL*1000ULL);  // Reserve enough to avoid reallocation

    std::vector<int> normal_vec;
    for (int i = 0; i < 100; ++i) {
        normal_vec.push_back(i);
    }
    std::vector<uint32_t, ArenaAllocator<uint32_t>>::value_type *mem = numbers.data();

    for (int i = 0; i < 100; ++i) {
        // numbers.push_back(0xB000B00B);
        mem = numbers.data();
        numbers.push_back(i);
        printf("%p\n", mem);
    }

    for (const uint32_t v : numbers) {
        printf("%u ", v);
    }
    printf("\n");
    // Deleting elements will now recycle memory back into the arena
    numbers.clear();

    // Reuse vector
    for (uint32_t i = 0; i < 50; ++i) {
        numbers.push_back(i * 2);
    }

    for (uint32_t v : numbers) {
        printf("%d ", v);
    }
    // while(1)
    {
        sleep(1);
        numbers.push_back(3);
        // madvec.push_back({});
        std::cout <<" Hello Sailor" << '\n';
    }
}
