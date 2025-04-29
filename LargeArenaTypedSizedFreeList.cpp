#include <iostream>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <list>
#include <sys/mman.h>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>

// Size constants
constexpr size_t TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * TB;
// constexpr size_t DEFAULT_VECTOR_CAPACITY = 1000000ULL;
constexpr size_t DEFAULT_VECTOR_CAPACITY = 10ULL; // for debugging re-allocations

// MemoryArena class to manage a large block of virtual memory
class MemoryArena {
private:
    void* baseAddress;
    size_t totalSize;
    size_t usedSize;
    std::mutex allocationMutex;

    // Type-based freelists for different vector types
    struct MemoryBlock {
        void* address;
        size_t size;
    };

    // Store freelists by type and block size
    std::unordered_map<std::type_index, std::unordered_map<size_t, std::list<MemoryBlock>>> freelists;

public:
    MemoryArena(size_t size = DEFAULT_ARENA_SIZE) : totalSize(size), usedSize(0) {
        // Map a large virtual address space without committing physical memory (lazy commit)
        baseAddress = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

        if (baseAddress == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate memory arena");
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

        // Calculate total size needed
        size_t bytesNeeded = n * sizeof(T);

        // Check if we have a matching block in the freelist
        auto typeIndex = std::type_index(typeid(T));
        auto& typeFreeList = freelists[typeIndex];
        auto it = typeFreeList.find(bytesNeeded);

        if (it != typeFreeList.end() && !it->second.empty()) {
            // Reuse a freed block
            MemoryBlock block = it->second.front();
            it->second.pop_front();

            std::cout << "Reusing memory block of size " << bytesNeeded
                      << " bytes for type " << typeid(T).name() << std::endl;

            return block.address;
        }

        // Allocate new memory if nothing in freelist
        if (usedSize + bytesNeeded > totalSize) {
            throw std::bad_alloc();
        }

        void* allocated = static_cast<char*>(baseAddress) + usedSize;
        usedSize += bytesNeeded;

        // Ensure the memory is committed by touching a byte in each page
        const size_t pageSize = 4096;
        for (size_t offset = 0; offset < bytesNeeded; offset += pageSize) {
            static_cast<char*>(allocated)[offset] = 0;
        }

        std::cout << "Allocated " << bytesNeeded << " bytes for type "
                  << typeid(T).name() << " at " << allocated << std::endl;

        return allocated;
    }

    template<typename T>
    void deallocate(void* ptr, size_t n) {
        if (ptr == nullptr) return;

        std::lock_guard<std::mutex> lock(allocationMutex);

        // Calculate total size
        size_t bytesFreed = n * sizeof(T);

        // Add to the freelist for this type and size
        auto typeIndex = std::type_index(typeid(T));
        freelists[typeIndex][bytesFreed].push_back({ptr, bytesFreed});

        std::cout << "Freed " << bytesFreed << " bytes for type "
                  << typeid(T).name() << " at " << ptr << std::endl;
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

    ArenaAllocator() noexcept = default;

    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>&) noexcept {}

    // Allocation function that calls our arena
    T* allocate(size_type n) {
        return static_cast<T*>(globalArena.allocate<T>(n));
    }

    // Deallocation function that returns memory to our arena's freelist
    void deallocate(T* p, size_type n) noexcept {
        globalArena.deallocate<T>(p, n);
    }

    // Make vector reserve a minimum capacity
    size_type initial_capacity() const { return DEFAULT_VECTOR_CAPACITY; }
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
    ArenaVector() : std::vector<T, ArenaAllocator<T>>() {
        this->reserve(DEFAULT_VECTOR_CAPACITY);
    }

    explicit ArenaVector(size_t initialCapacity) : std::vector<T, ArenaAllocator<T>>() {
        this->reserve(std::max(initialCapacity, DEFAULT_VECTOR_CAPACITY));
    }
};

// Example usage
int main()
{

    // Create vectors with our custom allocator
    ArenaVector<int> intVector;
    ArenaVector<double> doubleVector;

    std::cout << "intVector.data(): " << intVector.data() << '\n';
    std::cout << "doubleVector.data(): " << doubleVector.data() << '\n';
    // Fill vectors
    for (int i = 0; i < 20; i++)
    {
        intVector.push_back(i);
        doubleVector.push_back(i * 0.1);
    }
    ArenaVector<int> AnotherIntVector;
    std::cout << "AnotherIntVector.data(): " << AnotherIntVector.data() << '\n';


    std::cout << "After intVector.data(): " << intVector.data() << '\n';
    std::cout << "After doubleVector.data(): " << doubleVector.data() << '\n';

    std::cout << "Int vector size: " << intVector.size()
              << ", capacity: " << intVector.capacity() << std::endl;
    std::cout << "Double vector size: " << doubleVector.size()
              << ", capacity: " << doubleVector.capacity() << std::endl;

    // Test reuse of memory
    {
        ArenaVector<int> tempVector;
        std::cout << "tempVector.data(): " << tempVector.data() << '\n';
        for (int i = 0; i < 50000; i++)
        {
            tempVector.push_back(i);
        }
        std::cout << "after tempVector.data(): " << tempVector.data() << '\n';

        // tempVector will be destroyed here, and its memory will be added to freelist
    }

    // This should reuse memory from the freelist
    ArenaVector<int> reusedVector;
    std::cout << "reusedVector.data(): " << reusedVector.data() << '\n';
    for (int i = 0; i < 50000; i++)
    {
        reusedVector.push_back(i);
    }
    std::cout << "after reusedVector.data(): " << reusedVector.data() << '\n';

    std::cout << "Used memory: " << (globalArena.getUsedSize() / (1024 * 1024))
              << " MB out of " << (globalArena.getTotalSize() / TB) << " TB" << std::endl;

    return 0;
}