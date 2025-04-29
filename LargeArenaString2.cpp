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

// Size constants
constexpr size_t TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * TB;
constexpr size_t DEFAULT_BLOCK_BYTES = 8ULL * 1024ULL * 1024ULL; // 8 MB default block size
constexpr size_t DEFAULT_STRING_RESERVE = 256ULL-8ULL; // Default string capacity in bytes

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
    };
    
    // Store freelists by size only
    std::unordered_map<size_t, std::list<MemoryBlock>> freelists;

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
        auto& blocks = freelists[bytesNeeded];
        
        if (!blocks.empty()) {
            // Reuse a freed block
            MemoryBlock block = blocks.front();
            blocks.pop_front();
            
            std::cout << "Reusing memory block of size " << bytesNeeded 
                      << " bytes (originally for type " << typeid(T).name() << ")" << std::endl;
            
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
        
        // Add to the freelist by size only
        freelists[bytesFreed].push_back({ptr, bytesFreed});
        
        std::cout << "Freed " << bytesFreed << " bytes from type " 
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
    
    /* ArenaAllocator() noexcept : initialBytes(DEFAULT_BLOCK_BYTES) {}
    
    explicit ArenaAllocator(size_t bytes) noexcept : initialBytes(bytes) {}
    
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) noexcept : initialBytes(other.getInitialBytes()) {}
   */
    ArenaAllocator() noexcept : initialBytes(DEFAULT_BLOCK_BYTES) {
        std::cout << "initialBytes: " << initialBytes << '\n';
    }

    explicit ArenaAllocator(size_t bytes) noexcept : initialBytes(bytes) {
        std::cout << "initialBytes: " << initialBytes << '\n';

    }
    
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) noexcept : initialBytes(other.getInitialBytes()) {
        std::cout << "initialBytes: " << initialBytes << '\n';

    }
  
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


class ArenaString : public std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>> {
    using Base = std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;

public:
    ArenaString() 
        : Base(ArenaAllocator<char>(DEFAULT_STRING_RESERVE)) 
    {
        std::cout << "-->ArenaString() constructer: <---"  << '\n';
        this->reserve(DEFAULT_STRING_RESERVE);
    }

    ArenaString(const char* s)
        : Base(s, ArenaAllocator<char>(DEFAULT_STRING_RESERVE)) 
    {
        std::cout << "-->ArenaString(const char* s) constructer: <---"  << '\n';

        std::cout << "this.size(): " << this->size() << '\n';
        this->reserve(std::max(DEFAULT_STRING_RESERVE, this->size()));
    }

    ArenaString(const std::string& s)
        : Base(s, ArenaAllocator<char>(DEFAULT_STRING_RESERVE)) 
    {
        this->reserve(std::max(DEFAULT_STRING_RESERVE, this->size()));
    }

    // Allow move and copy constructors from base
    ArenaString(const Base& other) : Base(other) {}
    ArenaString(Base&& other) noexcept : Base(std::move(other)) {}

    ArenaString& operator=(const Base& other) {
        Base::operator=(other);
        return *this;
    }

    ArenaString& operator=(Base&& other) noexcept {
        Base::operator=(std::move(other));
        return *this;
    }
};



// Example usage
int main()
{
    // ArenaString name = "Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha ";
    ArenaString name = "Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha ";
    std::cout << "name.capacity(): " << name.capacity() << '\n';
    std::cout << "name.size(): " << name.size() << '\n';
    std::cout << "name: " << name << '\n';
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

    std::cout << " -----------------------------------------------\n";
    // Test reuse of memory
    {
        ArenaVector<int> tempVector;
        std::cout << "tempVector.data(): " << tempVector.data() << '\n';
        for (int i = 0; i < 20; i++)
        {
            tempVector.push_back(i);
        }
        std::cout << "after tempVector.data(): " << tempVector.data() << '\n';

        // tempVector will be destroyed here, and its memory will be added to freelist
    }

    // This should reuse memory from the freelist
    ArenaVector<double> reusedVector;
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