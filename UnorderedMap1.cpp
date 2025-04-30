#include <memory>
#include <cstddef>
#include <unordered_map>
#include <vector>
#include <iostream>

template <typename T>
class BlockAllocator {
public:
    // Type definitions
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    // Rebind allocator to type U
    template <typename U>
    struct rebind {
        using other = BlockAllocator<U>;
    };
    
    // Default constructor
    BlockAllocator() noexcept {}
    
    // Copy constructor
    template <typename U>
    BlockAllocator(const BlockAllocator<U>&) noexcept {}
    
    // Destructor
    ~BlockAllocator() {
        for (auto* block : memory_blocks) {
            std::free(block);
        }
    }

    // Allocate memory
    pointer allocate(size_type n) {
        size_type bytes_to_allocate = n * sizeof(T);
        // Round up to nearest multiple of 512 bytes
        bytes_to_allocate = ((bytes_to_allocate + 511) / 512) * 512;
        
        void* ptr = std::malloc(bytes_to_allocate);
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        memory_blocks.push_back(ptr);
        return static_cast<pointer>(ptr);
    }
    
    // Deallocate memory (we don't actually free memory until destruction)
    void deallocate(pointer p, size_type) noexcept {
        // No-op for individual deallocations
        // Memory will be released when allocator is destroyed
    }
    
    // Comparison operators
    template <typename U>
    bool operator==(const BlockAllocator<U>&) const noexcept {
        return true;
    }
    
    template <typename U>
    bool operator!=(const BlockAllocator<U>&) const noexcept {
        return false;
    }
    
private:
    std::vector<void*> memory_blocks;
};

// For unordered_map with reserve to work properly, we need a custom hash policy
template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class NoRehashUnorderedMap : public std::unordered_map<Key, T, Hash, KeyEqual, BlockAllocator<std::pair<const Key, T>>> {
private:
    using Base = std::unordered_map<Key, T, Hash, KeyEqual, BlockAllocator<std::pair<const Key, T>>>;
    
public:
    NoRehashUnorderedMap(size_t expected_max_elements) : Base() {
        // Reserve enough buckets to avoid rehashing
        // A load factor of 0.7 is typically a good balance
        size_t bucket_count = expected_max_elements * 1.5;
        this->reserve(bucket_count);
    }
};




#include <new>     // Required for placement new

// Track allocation
void* operator new(std::size_t size) {
    void* ptr = std::malloc(size);
    std::cout << "[ALLOC] Size: " << size << " bytes, Address: " << ptr << std::endl;
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

// Track deallocation
void operator delete(void* ptr) noexcept {
    std::cout << "[FREE ] Address: " << ptr << std::endl;
    std::free(ptr);
}

// Optional: Overload array versions too
void* operator new[](std::size_t size) {
    void* ptr = std::malloc(size);
    std::cout << "[ALLOC[]] Size: " << size << " bytes, Address: " << ptr << std::endl;
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    std::cout << "[FREE []] Address: " << ptr << std::endl;
    std::free(ptr);
}



// Example usage
int main() {
    // Create unordered_map with custom allocator - specify maximum elements upfront
    NoRehashUnorderedMap<uint64_t, double> map;
    // map.reserve(10000);
    
    // Insert elements without causing rehashing
    for (int i = 0; i < 1000; ++i) {
        // map.emplace(i, "Value " + std::to_string(i));
        map.emplace(i, i*3.L);
        if (i%10==0)
        {
            std::cout << "i: " << i << '\n';
        }
    }
    
    // Access elements
    std::cout << "Element at 500: " << map[500] << std::endl;
    
    return 0;
}