#ifndef _MEMORY_ARENA_
#define _MEMORY_ARENA_

#include <vector>
#include <unordered_map>
#include <sys/mman.h>

//-------------[ CONSTANTS  ]-----------------------------------------------------------------------
// Tune according to needs.
constexpr size_t TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t MB = 1024ULL * 1024ULL;
constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * TB;
constexpr size_t DEFAULT_BLOCK_BYTES = 8ULL * 1024ULL * 1024ULL; // 8 MB default block size
// constexpr size_t DEFAULT_BLOCK_BYTES = 256ULL; // 8 MB default block size
constexpr size_t DEFAULT_STRING_RESERVE = 256ULL - 8ULL; // Default string capacity in bytes
constexpr size_t DEFAULT_STRING_RESERVE_ = 256ULL;       // Default string capacity in bytes
constexpr size_t MIN_CHUNK_SIZE = 256ULL;
constexpr size_t DEFAULT_FREE_LIST_BUCKET_COUNT = 1000ULL;
constexpr size_t DEFAULT_FREE_LIST_BLOCK_SIZE = 100ULL;
//-------------[ CONSTANTS  ]-----------------------------------------------------------------------

//-------------[ SETTINGS  ]-----------------------------------------------------------------------
// #define LOG_ARENA
// #define MULTI_THREADED_ARENA
#define USE_FREE_LIST_ARENA
// #define TRACK_MEMORY_ALLOCATIONS
// #define INITIALIZE_MEMORY_ARENA
//-------------[ SETTINGS  ]-----------------------------------------------------------------------
#include <thread>


namespace rs
{
class MemoryArena;
extern MemoryArena GLOBAL_ARENA;


#ifdef MULTI_THREADED_ARENA
#include <mutex>
#endif

#ifdef LOG_ARENA // Debug build
#include <iostream>
#define DEBUG_LOG(...) std::cout << __VA_ARGS__ << '\n';
#else                  // Release build
#define DEBUG_LOG(...) // Expands to nothing
#endif


// MemoryArena class to manage a large block of virtual memory
class MemoryArena
{
public:
    void *base_address;
    size_t total_size;
    size_t used_size;
#ifdef MULTI_THREADED_ARENA
    std::mutex allocationMutex;
#endif
    // Memory blocks for freelist
    struct MemoryBlock
    {
        void *address;
        size_t size;
    };

#ifdef USE_FREE_LIST_ARENA
    // Store freelists by size only
    std::unordered_map<size_t, std::vector<MemoryBlock>> freelists;
#endif // USE_FREE_LIST_ARENA

    MemoryArena(size_t size = DEFAULT_ARENA_SIZE) : total_size(size), used_size(0)
    {
        // Map a large virtual address space without committing physical memory (lazy commit)
        base_address = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

        if (base_address == MAP_FAILED)
        {
            throw std::runtime_error("Failed to allocate memory arena");
        }

#ifdef USE_FREE_LIST_ARENA
        freelists.reserve(DEFAULT_FREE_LIST_BUCKET_COUNT); // reserve buckets;
#endif
        DEBUG_LOG("Memory arena initialized with " << (total_size / TB) << " TB at address " << base_address);
    }

    MemoryArena(std::uintptr_t address, size_t size)
        : base_address(reinterpret_cast<void *>(address)), total_size(size), used_size(0)
    {
#ifdef USE_FREE_LIST_ARENA
        freelists.reserve(DEFAULT_FREE_LIST_BUCKET_COUNT); // reserve buckets;
#endif
        DEBUG_LOG("Thread-local MemoryArena initialized with " << (total_size / MB) << " MB at address " << base_address);
    }

    ~MemoryArena()
    {
        DEBUG_LOG("ARENA DESTROYED " << std::this_thread::get_id());
        // Unmap all memory
        if (base_address != MAP_FAILED)
        {
            munmap(base_address, total_size); // todo:: watch this on different threads // 2. destructors aren't being called on another thread's this class is created with `new`
        }
    }

    template <typename T>
    void *allocate(size_t n)
    {
#ifdef MULTI_THREADED_ARENA
        std::lock_guard<std::mutex> lock(allocationMutex);
#endif
        // Calculate total size needed
        size_t bytes_needed = n * sizeof(T);

        // Round up to nearest multiple of SMALL_CHUNK_SIZE bytes
        bytes_needed = ((bytes_needed + MIN_CHUNK_SIZE - 1ULL) / MIN_CHUNK_SIZE) * MIN_CHUNK_SIZE; // SMALL_CHUNK_SIZE byte chunk

#ifdef USE_FREE_LIST_ARENA
        // Check if we have a matching block in the freelist
        auto &blocks = freelists[bytes_needed];
        // DEBUG_LOG("freelists[" << bytes_needed << "] bytes_needed | blockSize: " << blocks.size() << '\n');

        if (!blocks.empty())
        {
            // Reuse a freed block
            MemoryBlock block = blocks.back();
            blocks.pop_back();

            DEBUG_LOG("Reusing memory block of size " << bytes_needed << " bytes (originally for type " << typeid(T).name() << ")");

            return block.address;
        }
        else
        {
            if (blocks.capacity() < DEFAULT_FREE_LIST_BLOCK_SIZE)
                blocks.reserve(DEFAULT_FREE_LIST_BLOCK_SIZE); // reserve space for _this_size_ freelist reduce reallocations
        }
#endif // USE_FREE_LIST_ARENA

        // Allocate new memory if nothing in freelist
        if (used_size + bytes_needed > total_size)
        {
            throw std::bad_alloc();
        }

        void *allocated = static_cast<char *>(base_address) + used_size;
        used_size += bytes_needed;
#if 0        
        // Ensure the memory is committed by touching a byte in each page // WHY?
        const size_t pageSize = 4096;
        for (size_t offset = 0; offset < bytes_needed; offset += pageSize) {
            static_cast<char*>(allocated)[offset] = 0;
        }
#endif
        DEBUG_LOG("Allocated " << bytes_needed << " bytes for type " << typeid(T).name() << " at " << allocated);

        return allocated;
    }

    template <typename T>
    void deallocate(void *ptr, size_t n)
    {
        if (ptr == nullptr)
            return;
#ifdef MULTI_THREADED_ARENA
        std::lock_guard<std::mutex> lock(allocationMutex);
#endif
        // Calculate total size
        size_t bytesFreed = n * sizeof(T);
        bytesFreed = ((bytesFreed + MIN_CHUNK_SIZE - 1ULL) / MIN_CHUNK_SIZE) * MIN_CHUNK_SIZE; // SMALL_CHUNK_SIZE byte chunk

        // Add to the freelist by size only
#ifdef USE_FREE_LIST_ARENA
        freelists[bytesFreed].push_back({ptr, bytesFreed});
        // DEBUG_LOG("freelists[ " << bytesFreed << "] bytesFreed | blockSize: " << freelists[bytesFreed].size());

#endif // USE_FREE_LIST_ARENA

        DEBUG_LOG("Freed " << bytesFreed << " bytes from type " << typeid(T).name() << " at " << ptr);
    }

    // Statistics
    size_t get_free_size() const { return total_size - used_size; }
    void reset()
    {
        used_size = 0;
        freelists.clear();
    }
};

extern std::thread::id MAIN_THREAD_ID;
inline MemoryArena &get_thread_arena()
{
    thread_local MemoryArena* arena = nullptr;
    if (!arena)
    {
        DEBUG_LOG("CREATING A NEW ARENA | ThreadID: " << std::this_thread::get_id());
        if (std::this_thread::get_id() == MAIN_THREAD_ID)
        {
            // Main thread gets the global arena
            arena = &GLOBAL_ARENA;
        }
        else
        {
            // Other threads allocate 100MB chunks from the main arena
            void* block = GLOBAL_ARENA.allocate<uint8_t>(MB*100ULL);
            arena = new MemoryArena(reinterpret_cast<std::uintptr_t>(block), MB*100ULL);
        }
    }
    return *arena;
}

#ifdef INITIALIZE_MEMORY_ARENA
// Global memory arena instance
MemoryArena GLOBAL_ARENA;
std::thread::id MAIN_THREAD_ID = std::this_thread::get_id();

#endif
// Custom allocator for STL containers that uses our memory arena
template <typename T>
class ArenaAllocator
{
private:
    size_t initial_bytes;

public:
    // Required types for allocator
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    // Required for containers to properly rebind the allocator to different types
    template <typename U>
    struct rebind
    {
        using other = ArenaAllocator<U>;
    };

    ArenaAllocator() noexcept : initial_bytes(DEFAULT_BLOCK_BYTES)
    {
        // DEBUG_LOG("ArenaAllocator(): initial_bytes: " << initial_bytes);
    }

    explicit ArenaAllocator(size_t bytes) noexcept : initial_bytes(bytes)
    {
        // DEBUG_LOG("explicit ArenaAllocator(size_t bytes): initial_bytes: " << initial_bytes)';
    }

    template <typename U>
    ArenaAllocator(const ArenaAllocator<U> &other) noexcept : initial_bytes(other.get_initial_bytes())
    {
        // DEBUG_LOG("template <typename U> ArenaAllocator(const ArenaAllocator<U>& other): initial_bytes: " << initial_bytes);
    }

    // Allocation function that calls our arena
    T *allocate(size_type n)
    {
        // return static_cast<T *>(GLOBAL_ARENA.allocate<T>(n));
        return static_cast<T *>(rs::get_thread_arena().allocate<T>(n));
    }

    // Deallocation function that returns memory to our arena's freelist
    void deallocate(T *p, size_type n) noexcept
    {
        GLOBAL_ARENA.deallocate<T>(p, n);
    }

    // Get initial bytes for the container to use
    size_type get_initial_bytes() const { return initial_bytes; }

    // Calculate initial capacity based on bytes
    size_type initial_capacity() const
    {
        return std::max<size_type>(1, initial_bytes / sizeof(T));
    }

    // Hint for initial bucket count for unordered_map/set
    size_type initial_bucket_count() const
    {
        // A good bucket count is typically around 75% of the expected element count
        // for optimal hash table performance
        return std::max<size_type>(8, (initial_bytes / sizeof(T)) * 3 / 4);
    }
};

// Required operator== for allocator
template <typename T, typename U>
bool operator==(const ArenaAllocator<T> &, const ArenaAllocator<U> &) noexcept
{
    return true; // All our allocators are equivalent
}

template <typename T, typename U>
bool operator!=(const ArenaAllocator<T> &x, const ArenaAllocator<U> &y) noexcept
{
    return !(x == y);
}

// Specialized vector that uses our arena and always reserves space initially
template <typename T>
class vector : public std::vector<T, ArenaAllocator<T>>
{
public:
    vector() : std::vector<T, ArenaAllocator<T>>(ArenaAllocator<T>())
    {
        this->reserve(this->get_allocator().initial_capacity());
    }

    explicit vector(size_t initial_bytes) : std::vector<T, ArenaAllocator<T>>(ArenaAllocator<T>(initial_bytes))
    {
        this->reserve(this->get_allocator().initial_capacity());
    }
};

class string : public std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>
{
    using Base = std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;

public:
    string()
        : Base(ArenaAllocator<char>(DEFAULT_STRING_RESERVE))
    {
        this->reserve(DEFAULT_STRING_RESERVE);
    }

    string(const char *s)
        : Base(ArenaAllocator<char>(DEFAULT_STRING_RESERVE))
    {
        size_t len = std::strlen(s);
        auto bytes_needed = ((len + DEFAULT_STRING_RESERVE_ - 1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytes_needed - 8ULL);
        this->assign(s, len);
    }

    string(const std::string &s)
        : Base(s, ArenaAllocator<char>(DEFAULT_STRING_RESERVE))
    {
        auto bytes_needed = ((s.length() + DEFAULT_STRING_RESERVE_ - 1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytes_needed - 8ULL);
        this->assign(s);
    }

    // Allow move and copy constructors from base
    string(const Base &other) : Base(other) {}
    string(Base &&other) noexcept : Base(std::move(other)) {}

    string &operator=(const Base &other)
    {
        Base::operator=(other);
        return *this;
    }

    string &operator=(Base &&other) noexcept
    {
        Base::operator=(std::move(other));
        return *this;
    }

    string &operator=(const char *s)
    {
        size_t len = std::strlen(s);
        auto bytes_needed = ((len + DEFAULT_STRING_RESERVE_ - 1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytes_needed);
        this->assign(s, len);
        return *this;
    }

    string &operator=(const std::string &s)
    {
        auto bytes_needed = ((s.length() + DEFAULT_STRING_RESERVE_ - 1ULL) / DEFAULT_STRING_RESERVE_) * DEFAULT_STRING_RESERVE_;
        this->reserve(bytes_needed);
        this->assign(s);
        return *this;
    }
};


// Specialized unordered map implementation with our arena allocator
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class unordered_map : public std::unordered_map<
                              K, V, Hash, KeyEqual, ArenaAllocator<std::pair<const K, V>>>
{

public:
    using Base = std::unordered_map<K, V, Hash, KeyEqual, ArenaAllocator<std::pair<const K, V>>>;
    using allocator_type = ArenaAllocator<std::pair<const K, V>>;

    unordered_map() : Base(
                              // Initial bucket count based on allocator hint
                              allocator_type().initial_bucket_count(),
                              Hash(),
                              KeyEqual(),
                              allocator_type())
    {
    }

    explicit unordered_map(size_t initial_bytes) : Base(
                                                          // Initial bucket count based on allocator hint
                                                          allocator_type(initial_bytes).initial_bucket_count(),
                                                          Hash(),
                                                          KeyEqual(),
                                                          allocator_type(initial_bytes))
    {
    }
};

#ifdef TRACK_MEMORY_ALLOCATIONS
#include <new> // Required for placement new
// Track allocation
void *operator new(std::size_t size)
{
    void *ptr = std::malloc(size);
    std::cout << "[ALLOC] Size: " << size << " bytes, Address: " << ptr << std::endl;
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

// Track deallocation
void operator delete(void *ptr) noexcept
{
    std::cout << "[FREE ] Address: " << ptr << std::endl;
    std::free(ptr);
}

// Optional: Overload array versions too
void *operator new[](std::size_t size)
{
    void *ptr = std::malloc(size);
    std::cout << "[ALLOC[]] Size: " << size << " bytes, Address: " << ptr << std::endl;
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete[](void *ptr) noexcept
{
    std::cout << "[FREE []] Address: " << ptr << std::endl;
    std::free(ptr);
}

#endif // TRACK_MEMORY_ALLOCATIONS
}


// Hash function specialization for rs::string
namespace std
{
    template <>
    struct hash<rs::string>
    {
        size_t operator()(const rs::string &s) const
        {
            return hash<std::string_view>()(std::string_view(s.data(), s.size()));
        }
    };
}

#include <cassert>
#define push_temp_struct(arena, type) (type *)push_size_(arena, sizeof(type))
#define push_temp_array(arena, count, type) (type *)push_size_(arena, (count * sizeof(type)))

#define push_struct(type) (type *)push_size_(sizeof(type))
#define push_size(arena, size) push_size_(arena, Size)
struct temp_arena
{
    uint8_t *base;
    size_t used;
    size_t capacity;
};

inline temp_arena begin_temp_memory(size_t size)
{
    temp_arena result;
    result.base = (uint8_t *)rs::get_thread_arena().allocate<uint8_t>(size);

    result.used  = 0;
    result.capacity = size;
    return (result);
}

inline void end_temp_memory(temp_arena &arena)
{
    rs::get_thread_arena().deallocate<uint8_t>(arena.base,arena.capacity);
}

inline void *push_size_(temp_arena &arena, size_t size)
{
    assert(arena.used + size <= arena.capacity);
    void *result = arena.base + arena.used;
    arena.used += size;
    return (result);
}
inline void *push_size_(size_t size)
{
    return (rs::get_thread_arena().allocate<uint8_t>(1));
}


#endif // _MEMORY_ARENA_