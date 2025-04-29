#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <list>
#include <cstdlib>
#include <cassert>
#include <sys/mman.h>
#include <stdexcept>
#include <typeinfo>
#include <functional> // For std::hash
#include <utility>    // For std::pair

// Size constants
constexpr size_t TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * TB;
constexpr size_t DEFAULT_BLOCK_BYTES = 8ULL * 1024ULL * 1024ULL; // 8 MB default block size

// MemoryArena class to manage a large block of virtual memory
class MemoryArena {
private:
    void* baseAddress;
    size_t totalSize;
    size_t usedSize;
    std::mutex allocationMutex;

// Memory blocks for freelist
struct MemoryBlock {
    void* address;
    size_t size;
    size_t alignment; // Store the alignment requirement
};
    
    
    
    // Hash function for the pair key
    struct PairHash {
        template <class T1, class T2>
        std::size_t operator() (const std::pair<T1, T2>& pair) const {
            return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
        }
    };

    // Store freelists by size and alignment
    std::unordered_map<std::pair<size_t, size_t>, std::list<MemoryBlock>, PairHash> freelists;

public:
    MemoryArena(size_t size = DEFAULT_ARENA_SIZE) : totalSize(size), usedSize(0) {
        // Map a large virtual address space without committing physical memory (lazy commit)
        baseAddress = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        
        if (baseAddress == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate memory arena");
        }
        
        // Ensure baseAddress is maximally aligned
        if (reinterpret_cast<uintptr_t>(baseAddress) % alignof(std::max_align_t) != 0) {
            munmap(baseAddress, totalSize);
            throw std::runtime_error("Memory arena base address is not properly aligned");
        }
        
        std::cout << "Memory arena initialized with " << (totalSize / TB) 
                  << " TB at address " << baseAddress << std::endl;
    }
    
    ~MemoryArena() {
        // Unmap all memory
        if (baseAddress != MAP_FAILED) {
            munmap(baseAddress, totalSize);
        }
    }
    
    template<typename T>
    void* allocate(size_t n) {
        std::lock_guard<std::mutex> lock(allocationMutex);
        
        // Calculate total size needed and alignment
        size_t bytesNeeded = n * sizeof(T);
        size_t alignment = alignof(T);
        
        // Check if we have a matching block in the freelist with proper alignment
        auto key = std::make_pair(bytesNeeded, alignment);
        auto& blocks = freelists[key];
        
        if (!blocks.empty()) {
            // Reuse a freed block with matching alignment
            MemoryBlock block = blocks.front();
            blocks.pop_front();
            
            std::cout << "Reusing memory block of size " << bytesNeeded 
                      << " bytes with alignment " << alignment
                      << " (for type " << typeid(T).name() << ")" << std::endl;
            
            return block.address;
        }
        
        // Check if we can use a block with compatible alignment
        // (alignment is compatible if it's a multiple of what we need)
        for (auto& [blockKey, blockList] : freelists) {
            // Only check blocks of the same size
            if (blockKey.first != bytesNeeded) continue;
            
            // If block alignment is divisible by our required alignment, we can use it
            if (blockKey.second % alignment == 0 && !blockList.empty()) {
                MemoryBlock block = blockList.front();
                blockList.pop_front();
                
                std::cout << "Reusing memory block of size " << bytesNeeded 
                          << " bytes with compatible alignment " << blockKey.second
                          << " (for type " << typeid(T).name() << ")" << std::endl;
                
                return block.address;
            }
        }
        
        // Allocate new memory if nothing suitable in freelist
        if (usedSize + bytesNeeded > totalSize) {
            throw std::bad_alloc();
        }
        
        // Ensure proper alignment of the new allocation
        size_t misalignment = reinterpret_cast<uintptr_t>(static_cast<char*>(baseAddress) + usedSize) % alignment;
        size_t adjustment = (misalignment > 0) ? (alignment - misalignment) : 0;
        
        void* allocated = static_cast<char*>(baseAddress) + usedSize + adjustment;
        usedSize += bytesNeeded + adjustment;
        
        // Ensure the memory is committed by touching a byte in each page
        const size_t pageSize = 4096;
        for (size_t offset = 0; offset < bytesNeeded; offset += pageSize) {
            static_cast<char*>(allocated)[offset] = 0;
        }
        
        std::cout << "Allocated " << bytesNeeded << " bytes with alignment " << alignment
                  << " for type " << typeid(T).name() << " at " << allocated << std::endl;
        
        return allocated;
    }
    
    template<typename T>
    void deallocate(void* ptr, size_t n) {
        if (ptr == nullptr) return;
        
        std::lock_guard<std::mutex> lock(allocationMutex);
        
        // Calculate total size and alignment
        size_t bytesFreed = n * sizeof(T);
        size_t alignment = alignof(T);
        
        // Add to the freelist by size and alignment
        auto key = std::make_pair(bytesFreed, alignment);
        freelists[key].push_back({ptr, bytesFreed, alignment});
        
        std::cout << "Freed " << bytesFreed << " bytes with alignment " << alignment
                  << " from type " << typeid(T).name() << " at " << ptr << std::endl;
    }
    
    // Statistics
    size_t getUsedSize() const { return usedSize; }
    size_t getTotalSize() const { return totalSize; }
    size_t getFreeSize() const { return totalSize - usedSize; }
};

// Global memory arena instance
MemoryArena globalArena;

// Custom allocator for STL containers that uses our memory arena
template <typename T>
class ArenaAllocator {
private:
    size_t initialBytes;

public:
    // Required types for allocator
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    // Required for containers to properly rebind the allocator to different types
    template <typename U>
    struct rebind {
        using other = ArenaAllocator<U>;
    };
    
    ArenaAllocator() noexcept : initialBytes(DEFAULT_BLOCK_BYTES) {}
    
    explicit ArenaAllocator(size_t bytes) noexcept : initialBytes(bytes) {}
    
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) noexcept : initialBytes(other.getInitialBytes()) {}
    
    // Allocation function that calls our arena
    T* allocate(size_type n) {
        return static_cast<T*>(globalArena.allocate<T>(n));
    }
    
    // Deallocation function that returns memory to our arena's freelist
    void deallocate(T* p, size_type n) noexcept {
        globalArena.deallocate<T>(p, n);
    }
    
    // Get initial bytes for the container to use
    size_type getInitialBytes() const { return initialBytes; }
    
    // Calculate initial capacity based on bytes
    size_type initial_capacity() const { 
        return std::max<size_type>(1, initialBytes / sizeof(T));
    }
};

// Required operator== for allocator
template <typename T, typename U>
bool operator==(const ArenaAllocator<T>&, const ArenaAllocator<U>&) noexcept {
    return true; // All our allocators are equivalent
}

template <typename T, typename U>
bool operator!=(const ArenaAllocator<T>& x, const ArenaAllocator<U>& y) noexcept {
    return !(x == y);
}

// Specialized vector that uses our arena and always reserves space initially
template <typename T>
class ArenaVector : public std::vector<T, ArenaAllocator<T>> {
public:
    ArenaVector() : std::vector<T, ArenaAllocator<T>>(ArenaAllocator<T>()) {
        this->reserve(this->get_allocator().initial_capacity());
    }
    
    explicit ArenaVector(size_t initialBytes) : 
        std::vector<T, ArenaAllocator<T>>(ArenaAllocator<T>(initialBytes)) {
        this->reserve(this->get_allocator().initial_capacity());
    }
};

// Example usage
int main() {
    try {
        // Create vectors with our custom allocator - they will reserve based on bytes
        ArenaVector<int> intVector;
        ArenaVector<double> doubleVector;
        ArenaVector<char> charVector(16 * 1024 * 1024); // 16MB for char vector
        
        std::cout << "Int vector initial capacity: " << intVector.capacity() 
                  << " elements (" << (intVector.capacity() * sizeof(int)) << " bytes)" << std::endl;
        std::cout << "Double vector initial capacity: " << doubleVector.capacity() 
                  << " elements (" << (doubleVector.capacity() * sizeof(double)) << " bytes)" << std::endl;
        std::cout << "Char vector initial capacity: " << charVector.capacity() 
                  << " elements (" << (charVector.capacity() * sizeof(char)) << " bytes)" << std::endl;
        
        // Fill vectors
        for (int i = 0; i < 100000; i++) {
            intVector.push_back(i);
            doubleVector.push_back(i * 0.1);
        }
        
        // Test reuse of memory
        {
            ArenaVector<int> tempVector;
            for (int i = 0; i < 50000; i++) {
                tempVector.push_back(i);
            }
            // tempVector will be destroyed here, and its memory will be added to freelist
        }
        
        // This should reuse memory from the freelist
        ArenaVector<int> reusedVector;
        
        // Create different vector types that should use similar memory block sizes
        ArenaVector<float> floatVector;
        ArenaVector<uint64_t> longVector;
        
        std::cout << "Used memory: " << (globalArena.getUsedSize() / (1024 * 1024)) 
                  << " MB out of " << (globalArena.getTotalSize() / TB) << " TB" << std::endl;
                  
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    
    return 0;
}