#ifndef _MEMORY_ARENA_NOEXCEPT_
#define _MEMORY_ARENA_NOEXCEPT_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <type_traits>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <cassert>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define USE_MMAP_BACKING
#else
#define USE_MALLOC_BACKING
#endif

//-------------[ CONFIG ]-----------------------------------------------------------------------
constexpr size_t KB = 1024ULL;
constexpr size_t MB = 1024ULL * KB;
constexpr size_t GB = 1024ULL * MB;

constexpr size_t DEFAULT_ARENA_SIZE      = 64ULL * GB;
constexpr size_t THREAD_LOCAL_ARENA_SIZE = 100ULL * MB;
constexpr size_t MIN_CHUNK_SIZE          = 64ULL;
constexpr size_t MAX_FREELIST_SIZE       = 16ULL;
constexpr size_t DEFAULT_ALIGNMENT       = alignof(std::max_align_t);

// #define ARENA_DEBUG_LOGGING
// #define ARENA_MULTITHREADED

#ifdef ARENA_DEBUG_LOGGING
#include <iostream>
#define DEBUG_LOG(x) do { std::cout << x << '\n'; } while(0)
#else
#define DEBUG_LOG(x) do {} while(0)
#endif

namespace arena {

//-------------[ CORE DATA ]----------------------------------------------------------------
struct Arena {
    void   *base = nullptr;
    size_t total_size = 0;
    size_t used_size = 0;
    bool   is_owner = false;
    void  *free_lists[MAX_FREELIST_SIZE] = {};
#ifdef ARENA_MULTITHREADED
    std::mutex mutex;
#endif
};

struct TempArena {
    Arena *parent = nullptr;
    void  *base = nullptr;
    size_t size = 0;
    size_t used = 0;
};

//-------------[ UTILS ]----------------------------------------------------------------
inline size_t align_up(size_t value, size_t alignment) noexcept {
    size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

inline size_t size_to_class(size_t size) noexcept {
    if (size <= MIN_CHUNK_SIZE) return 0;
    size_t rounded = MIN_CHUNK_SIZE;
    size_t index = 0;
    while (rounded < size && index < MAX_FREELIST_SIZE - 1) {
        rounded <<= 1;
        ++index;
    }
    return index;
}

inline size_t class_to_size(size_t index) noexcept {
    return MIN_CHUNK_SIZE << index;
}

//-------------[ ARENA LIFECYCLE ]----------------------------------------------------------------
inline void arena_init(Arena *a, size_t size) noexcept {
    a->total_size = size;
    a->used_size = 0;
    a->is_owner = true;

#ifdef USE_MMAP_BACKING
    a->base = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (a->base == MAP_FAILED) {
        __builtin_trap(); // Or: std::abort();
    }
#else
    a->base = std::aligned_alloc(DEFAULT_ALIGNMENT, size);
    if (!a->base) __builtin_trap();
#endif

    // Already zero-initialized by struct initialization, but clear just in case
    for (size_t i = 0; i < MAX_FREELIST_SIZE; ++i) {
        a->free_lists[i] = nullptr;
    }
    DEBUG_LOG("Arena init " << (size / MB) << " MB");
}

inline void arena_init_sub(Arena *a, void *base, size_t size) noexcept {
    a->base = base;
    a->total_size = size;
    a->used_size = 0;
    a->is_owner = false;
    for (size_t i = 0; i < MAX_FREELIST_SIZE; ++i) {
        a->free_lists[i] = nullptr;
    }
}

inline void arena_destroy(Arena *a) noexcept {
    if (a->is_owner && a->base) {
#ifdef USE_MMAP_BACKING
        munmap(a->base, a->total_size);
#else
        std::free(a->base);
#endif
        DEBUG_LOG("Arena destroyed");
    }
    a->base = nullptr;
    a->total_size = 0;
    a->used_size = 0;
}

inline void arena_reset(Arena *a) noexcept {
#ifdef ARENA_MULTITHREADED
    std::lock_guard<std::mutex> lock(a->mutex);
#endif
    a->used_size = 0;
    for (size_t i = 0; i < MAX_FREELIST_SIZE; ++i) {
        a->free_lists[i] = nullptr;
    }
}

//-------------[ ALLOC / FREE ]----------------------------------------------------------------
inline void *arena_alloc(Arena *a, size_t size, size_t alignment) noexcept {
#ifdef ARENA_MULTITHREADED
    std::lock_guard<std::mutex> lock(a->mutex);
#endif

    // Try free list
    if (size <= class_to_size(MAX_FREELIST_SIZE - 1)) {
        size_t sz_class = size_to_class(size);
        if (sz_class < MAX_FREELIST_SIZE) {
            void *node = a->free_lists[sz_class];
            if (node) {
                a->free_lists[sz_class] = *reinterpret_cast<void **>(node);
                DEBUG_LOG("Reuse " << class_to_size(sz_class) << " bytes");
                return node;
            }
        }
    }

    size_t aligned_used = align_up(a->used_size, alignment);
    size_t new_used = aligned_used + size;
    
    // Crash on OOM instead of throwing
    if (new_used > a->total_size) {
        __builtin_trap(); // Or: assert(!"Out of memory");
    }

    void *ptr = static_cast<char *>(a->base) + aligned_used;
    a->used_size = new_used;
    DEBUG_LOG("Alloc " << size << " bytes at " << ptr);
    return ptr;
}

inline void arena_free(Arena *a, void *ptr, size_t size) noexcept {
    if (!ptr) return;

#ifdef ARENA_MULTITHREADED
    std::lock_guard<std::mutex> lock(a->mutex);
#endif

    if (size <= class_to_size(MAX_FREELIST_SIZE - 1)) {
        size_t sz_class = size_to_class(size);
        if (sz_class < MAX_FREELIST_SIZE) {
            *reinterpret_cast<void **>(ptr) = a->free_lists[sz_class];
            a->free_lists[sz_class] = ptr;
            DEBUG_LOG("Free " << size << " bytes to class " << sz_class);
            return;
        }
    }
}

//-------------[ TEMP ARENA ]----------------------------------------------------------------
inline void temp_init(TempArena *t, Arena *parent, size_t size) noexcept {
    t->parent = parent;
    t->base = arena_alloc(parent, size, DEFAULT_ALIGNMENT);
    t->size = size;
    t->used = 0;
}

inline void temp_destroy(TempArena *t) noexcept {
    if (t->base && t->parent) {
        arena_free(t->parent, t->base, t->size);
    }
    t->base = nullptr;
    t->size = 0;
    t->used = 0;
}

inline void *temp_alloc(TempArena *t, size_t size, size_t alignment) noexcept {
    size_t aligned_used = align_up(t->used, alignment);
    size_t new_used = aligned_used + size;
    if (new_used > t->size) {
        __builtin_trap(); // Crash on temp OOM
    }
    void *ptr = static_cast<char *>(t->base) + aligned_used;
    t->used = new_used;
    return ptr;
}

inline void temp_reset(TempArena *t) noexcept {
    t->used = 0;
}

//-------------[ GLOBAL / THREAD ARENA ]----------------------------------------------------------------
inline Arena &get_global_arena() noexcept {
    static Arena global{};
    static bool inited = false;
    if (!inited) {
        arena_init(&global, DEFAULT_ARENA_SIZE);
        inited = true;
    }
    return global;
}

inline Arena &get_thread_arena() noexcept {
    static std::thread::id main_id = std::this_thread::get_id();

    thread_local struct ThreadArena {
        Arena *arena = nullptr;
        Arena  storage{};
        ThreadArena() {
            if (std::this_thread::get_id() == main_id) {
                arena = &get_global_arena();
            } else {
                void *backing = arena_alloc(&get_global_arena(), 
                                            THREAD_LOCAL_ARENA_SIZE, 
                                            DEFAULT_ALIGNMENT);
                arena_init_sub(&storage, backing, THREAD_LOCAL_ARENA_SIZE);
                arena = &storage;
            }
        }
        ~ThreadArena() {
            if (arena == &storage) {
                arena_destroy(&storage);
            }
        }
    } tls;

    return *tls.arena;
}

//-------------[ ALLOCATOR - NO EXCEPTIONS ]----------------------------------------------------------------
template <typename T>
struct Allocator {
    using value_type = T;

    Arena *arena;

    Allocator() noexcept : arena(&get_thread_arena()) {}
    explicit Allocator(Arena *a) noexcept : arena(a) {}

    template <class U>
    Allocator(const Allocator<U> &other) noexcept : arena(other.arena) {}

    // NO THROW - returns nullptr or crashes
    [[nodiscard]] T *allocate(std::size_t n) noexcept {
        // Check for overflow (unsigned wrap), crash if bad
        size_t bytes = n * sizeof(T);
        if (n != 0 && bytes / n != sizeof(T)) {
            __builtin_trap(); // Size overflow
        }
        
        void *p = arena_alloc(arena, bytes, alignof(T));
        // arena_alloc already crashes on OOM, so p is never nullptr here
        return static_cast<T *>(p);
    }

    void deallocate(T *p, std::size_t n) noexcept {
        arena_free(arena, p, n * sizeof(T));
    }

    template <class U>
    struct rebind {
        using other = Allocator<U>;
    };
};

template <class T, class U>
inline bool operator==(const Allocator<T> &a, const Allocator<U> &b) noexcept {
    return a.arena == b.arena;
}
template <class T, class U>
inline bool operator!=(const Allocator<T> &a, const Allocator<U> &b) noexcept {
    return !(a == b);
}

//-------------[ CONTAINER TYPEDEFS ]----------------------------------------------------------------
template <typename T>
using Vector = std::vector<T, Allocator<T>>;

using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

template <typename K, typename V, typename Compare = std::less<K>>
using Map = std::map<K, V, Compare, Allocator<std::pair<const K, V>>>;

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
using UnorderedMap = std::unordered_map<K, V, Hash, KeyEqual,
                                        Allocator<std::pair<const K, V>>>;

//-------------[ C-STYLE TEMP MACROS ]----------------------------------------------------------------
#define WITH_TEMP_ARENA(name, size)                                            \
    for (arena::TempArena name = {};                                           \
         (name.base == nullptr ? (arena::temp_init(&name, &arena::get_thread_arena(), (size)), 1) : 0) \
         || name.base != nullptr;                                              \
         (arena::temp_destroy(&name), name.base = (void *)1))

#define TEMP_ALLOC(temp, type)       (static_cast<type *>(arena::temp_alloc(&(temp), sizeof(type), alignof(type))))
#define TEMP_ARRAY(temp, count, type) (static_cast<type *>(arena::temp_alloc(&(temp), (count) * sizeof(type), alignof(type))))

} // namespace arena

//-------------[ HASH FOR arena::String ]----------------------------------------------------------------
namespace std {
    template <>
    struct hash<arena::String> {
        size_t operator()(const arena::String &s) const noexcept {
            return hash<string_view>{}(string_view(s.data(), s.size()));
        }
    };
}

#endif
