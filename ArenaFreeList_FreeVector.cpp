#include <vector>
#include <unordered_map>
#include <sys/mman.h>

//-------------[ CONSTANTS  ]-----------------------------------------------------------------------
// Tune according to needs.
constexpr size_t TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * TB;
constexpr size_t DEFAULT_BLOCK_BYTES = 8ULL * 1024ULL * 1024ULL; // 8 MB default block size
// constexpr size_t DEFAULT_BLOCK_BYTES = 256ULL; // 8 MB default block size
constexpr size_t DEFAULT_STRING_RESERVE = 256ULL-8ULL; // Default string capacity in bytes
constexpr size_t DEFAULT_STRING_RESERVE_= 256ULL; // Default string capacity in bytes
constexpr size_t MIN_CHUNK_SIZE= 256ULL;
//-------------[ CONSTANTS  ]-----------------------------------------------------------------------


//-------------[ SETTINGS  ]-----------------------------------------------------------------------
#define _DEBUG_LOG
// #define MULTI_THREADED_ARENA
#define USE_FREE_LIST
//-------------[ SETTINGS  ]-----------------------------------------------------------------------


#ifdef _DEBUG_LOG
#include <iostream>
#endif


#ifdef MULTI_THREADED_ARENA
#include <mutex>
#endif

#ifdef _DEBUG_LOG  // Debug build
  #define DEBUG_LOG(...) std::cout << __VA_ARGS__ << '\n';
#else  // Release build
  #define DEBUG_LOG(...)  // Expands to nothing
#endif



// MemoryArena class to manage a large block of virtual memory
class MemoryArena {
private:
    void* baseAddress;
    size_t totalSize;
    size_t usedSize;
#ifdef MULTI_THREADED_ARENA
    std::mutex allocationMutex;
#endif
    // Memory blocks for freelist
    struct MemoryBlock {
        void* address;
        size_t size;
    };

#ifdef USE_FREE_LIST
    // Store freelists by size only
    std::unordered_map<size_t, std::vector<MemoryBlock>> freelists;
#endif  //USE_FREE_LIST

public:
    MemoryArena(size_t size = DEFAULT_ARENA_SIZE) : totalSize(size), usedSize(0) {
        // Map a large virtual address space without committing physical memory (lazy commit)
        baseAddress = mmap(nullptr, size, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        
        if (baseAddress == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate memory arena");
        }

#ifdef USE_FREE_LIST
        freelists.reserve(1000); // reserve buckets
#endif
        DEBUG_LOG("Memory arena initialized with " << (totalSize / TB) << " TB at address " << baseAddress);
    }
    
    ~MemoryArena() {
        // Unmap all memory
        if (baseAddress != MAP_FAILED) {
            munmap(baseAddress, totalSize);
        }
    }
    
    template<typename T>
    void* allocate(size_t n) {
#ifdef MULTI_THREADED_ARENA
        std::lock_guard<std::mutex> lock(allocationMutex);
#endif        
        // Calculate total size needed
        size_t bytesNeeded = n * sizeof(T);
        
        // Round up to nearest multiple of SMALL_CHUNK_SIZE bytes
        bytesNeeded = ((bytesNeeded + MIN_CHUNK_SIZE-1ULL) / MIN_CHUNK_SIZE) * MIN_CHUNK_SIZE; // SMALL_CHUNK_SIZE byte chunk

#ifdef USE_FREE_LIST
        // Check if we have a matching block in the freelist
        auto& blocks = freelists[bytesNeeded];
        // DEBUG_LOG("freelists[" << bytesNeeded << "] bytesNeeded | blockSize: " << blocks.size() << '\n');
        
        if (!blocks.empty())
        {
            // Reuse a freed block
            MemoryBlock block = blocks.back();
            blocks.pop_back();
            
            DEBUG_LOG("Reusing memory block of size " << bytesNeeded << " bytes (originally for type " << typeid(T).name() << ")");
            
            return block.address;
        } else
        {
            blocks.reserve(100); // reserve space for _this_size_ freelist reduce reallocations
            // std::cout << "blocks.reserve(100)\n";
        }
#endif  //USE_FREE_LIST
        
        // Allocate new memory if nothing in freelist
        if (usedSize + bytesNeeded > totalSize) {
            throw std::bad_alloc();
        }
        
        void* allocated = static_cast<char*>(baseAddress) + usedSize;
        usedSize += bytesNeeded;
#if 0        
        // Ensure the memory is committed by touching a byte in each page // WHY?
        const size_t pageSize = 4096;
        for (size_t offset = 0; offset < bytesNeeded; offset += pageSize) {
            static_cast<char*>(allocated)[offset] = 0;
        }
#endif
        DEBUG_LOG("Allocated " << bytesNeeded << " bytes for type " << typeid(T).name() << " at " << allocated);

        return allocated;
    }
    
    template<typename T>
    void deallocate(void* ptr, size_t n) {
        if (ptr == nullptr) return;
#ifdef MULTI_THREADED_ARENA        
        std::lock_guard<std::mutex> lock(allocationMutex);
#endif
        // Calculate total size
        size_t bytesFreed = n * sizeof(T);
        
        // Add to the freelist by size only
#ifdef USE_FREE_LIST
        freelists[bytesFreed].push_back({ptr, bytesFreed});
        // DEBUG_LOG("freelists[ " << bytesFreed << "] bytesFreed | blockSize: " << freelists[bytesFreed].size());

#endif  //USE_FREE_LIST

        DEBUG_LOG("Freed " << bytesFreed << " bytes from type " << typeid(T).name() << " at " << ptr);
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
        // std::cout << "ArenaAllocator(): initialBytes: " << initialBytes << '\n';
    }

    explicit ArenaAllocator(size_t bytes) noexcept : initialBytes(bytes) {
        // std::cout << "explicit ArenaAllocator(size_t bytes): initialBytes: " << initialBytes << '\n';

    }
    
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) noexcept : initialBytes(other.getInitialBytes()) {
        // std::cout << "template <typename U> ArenaAllocator(const ArenaAllocator<U>& other): initialBytes: " << initialBytes << '\n';

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

    // Hint for initial bucket count for unordered_map/set
    size_type initial_bucket_count() const
    {
        // A good bucket count is typically around 75% of the expected element count
        // for optimal hash table performance
        return std::max<size_type>(8, (initialBytes / sizeof(T)) * 3 / 4);
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
        this->reserve(DEFAULT_STRING_RESERVE);
    }

    ArenaString(const char* s)
        : Base(ArenaAllocator<char>(DEFAULT_STRING_RESERVE)) 
    {
        size_t len = std::strlen(s);
        auto  bytesNeeded = ((len + DEFAULT_STRING_RESERVE_-1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytesNeeded - 8ULL);
        this->assign(s, len);


    }

    ArenaString(const std::string& s)
        : Base(s, ArenaAllocator<char>(DEFAULT_STRING_RESERVE)) 
    {
        auto  bytesNeeded = ((s.length() + DEFAULT_STRING_RESERVE_-1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytesNeeded - 8ULL);
        this->assign(s);
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

    ArenaString& operator=(const char* s) {
        size_t len = std::strlen(s);
        int block_cnt = std::ceil((float)len/256.f);
        this->reserve(DEFAULT_STRING_RESERVE_ * block_cnt - 8ULL);
        this->assign(s, len);
        return *this;
    }
    
    ArenaString& operator=(const std::string& s) {
        int block_cnt = std::ceil((float)s.length()/256.f);
        this->reserve(DEFAULT_STRING_RESERVE_ * block_cnt - 8ULL);
        this->assign(s);
        return *this;
    }
};


// Hash function specialization for ArenaString
namespace std {
    template<>
    struct hash<ArenaString> {
        size_t operator()(const ArenaString& s) const {
            return hash<std::string_view>()(std::string_view(s.data(), s.size()));
        }
    };
}


// Specialized unordered map implementation with our arena allocator
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class ArenaUnorderedMap : public std::unordered_map<
    K, V, Hash, KeyEqual, ArenaAllocator<std::pair<const K, V>>> {

public:
    using Base = std::unordered_map<K, V, Hash, KeyEqual, ArenaAllocator<std::pair<const K, V>>>;
    using allocator_type = ArenaAllocator<std::pair<const K, V>>;

    ArenaUnorderedMap() : Base(
                              // Initial bucket count based on allocator hint
                              allocator_type().initial_bucket_count(),
                              Hash(),
                              KeyEqual(),
                              allocator_type())
    {
    }

    explicit ArenaUnorderedMap(size_t initialBytes) : Base(
                                                          // Initial bucket count based on allocator hint
                                                          allocator_type(initialBytes).initial_bucket_count(),
                                                          Hash(),
                                                          KeyEqual(),
                                                          allocator_type(initialBytes))
    {
    }
};

// #define TRACK_MEMORY_ALLOCATIONS
#define VECTOR_TEST
#define STRING_TEST
#define UM_TEST


#ifdef TRACK_MEMORY_ALLOCATIONS // Track std mem allocation

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

#endif  // Track std mem allocation



// Example usage
int main()
{


#if  0 // Check if memory is allocated in multiples of BLOCK_SIZE
    {
        struct Thing{
            char data[1024ULL*1024ULL];
        };
        

        ArenaVector<Thing> ThingVec;
        for (int i = 0; i < 20; ++i)
        {
            ThingVec.push_back({});
        }
    }
#endif  //Check if memory is allocated in multiples of BLOCK_SIZE

#if  0 // // std::unordered_map memory characteristic


    std::unordered_map<ArenaString, int> my_map;

    my_map.max_load_factor(0.75);
    my_map.reserve(1000);  // Reserve space for 1000 elements

    // Now insert without frequent rehashing or reallocs
    for (int i = 0; i < 100; ++i) {
        my_map["key" + std::to_string(i)] = i;
    }
    for (auto [k,v]: my_map)
    {
    	std::cerr << k << ": " << v  << '\n';
    }

    // std::unordered_map<int, int> mp;
    // mp.reserve(1000ULL);
    // std::cout << "mp.bucket_count(): " << mp.bucket_count() << '\n';
    // std::cout << "mp.bucket_size(): " << mp.bucket_size(100) << '\n';
    // std::cout << "mp.max_bucket_count(): " << mp.max_bucket_count() << '\n';

    // for (int i = 0; i < 100; ++i)
    // {
    // 	mp[i]  = i*i;
    //     // std::cout << "mp.bucket_count(): " << mp.bucket_count() << '\n';
    // 	// mp.emplace(i, i*i);
    // }
    // ArenaVector<std::pair<int,char>> a;
    // ArenaVector<ArenaString> a;
    // std::vector<int> f(1000);
    
#endif  // std::unordered_map memory characteristic
    std::cout << " -----------------------------------------------\n";

#ifdef UM_TEST
std::cout << " ----------[ UnorderedMap ]-------------------------------------\n";

    // ArenaUnorderedMap<ArenaString, int> m;

    ArenaUnorderedMap<int, ArenaString> testMap;
    // testMap.reserve(100000);


     // Add some entries
     for (int i = 0; i < 10; i++) {
        // Create strings that use the arena
        ArenaString value = "Value-";
        value += std::to_string(i);
        testMap[i] = value;
    }
    
    std::cout << "Map size: " << testMap.size() << std::endl;
    std::cout << "Map entry example: " << testMap[42] << std::endl;
#endif  //0

#ifdef STRING_TEST // ArenaString test
    std::cout << " ------------[ String Test ]-----------------------------------\n";

    ArenaString b = "RahulSinhaRahulSinhaRahulSinhaRahulSinha";
    // std::cout << "b: " << b << '\n';
    // printf("b.data(): %p\n", b.data());

    b = "123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-";
    // std::cout << "b: " << b << '\n';
    // printf("b.data(): %p\n", b.data());
    
    ArenaString c = "Another";
    // printf("c.data(): %p\n", c.data());



    // ArenaString str("123456789-123456789-123456789-123456789-123456789-123456789-");
    ArenaString str("123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-123456789-123456789-12345-");
    // std::cout << "str: " << str << '\n';
    // std::cout << "str.size(): " << str.size() << '\n';
    // std::cout << "str.capacity(): " << str.capacity() << '\n';

    auto s = ArenaAllocator<char>(DEFAULT_STRING_RESERVE);
    s.allocate(DEFAULT_STRING_RESERVE);
    // std::cout << "sizeof(char): " << sizeof(char) << '\n';
    // std::cout << "s.getInitialBytes(): " << s.getInitialBytes() << '\n';
    // std::cout << "s.initial_capacity(): " << s.initial_capacity() << '\n';

    ArenaString small_string_opt_len22 = "123456789-123456789-12";

#endif  // ArenaString test
    // ArenaString another_str            = "123456789-123456789-123456789-123456789-123456789-123456789-";
    // ArenaString name = "Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha ";
    // ArenaString name = "Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha Rahul Sinha ";
    // std::cout << "name.capacity(): " << name.capacity() << '\n';
    // std::cout << "name.size(): " << name.size() << '\n';
    // std::cout << "name: " << name << '\n';
    // Create vectors with our custom allocator

#ifdef VECTOR_TEST // ArenaVector Test
    std::cout << " ------------[ Vector Test ]-----------------------------------\n";

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
#endif // ArenaVector Test

    std::cout << "Used memory: " << (globalArena.getUsedSize() / (1024 * 1024))
              << " MB out of " << (globalArena.getTotalSize() / TB) << " TB" << std::endl;

    return 0;
}