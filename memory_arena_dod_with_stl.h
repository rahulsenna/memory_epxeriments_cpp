#ifndef _MEMORY_ARENA_DOD_WITH_STL_
#define _MEMORY_ARENA_DOD_WITH_STL_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

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

constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * GB;
constexpr size_t THREAD_LOCAL_ARENA_SIZE = 100ULL * MB;
constexpr size_t MIN_CHUNK_SIZE = 64ULL;
constexpr size_t MAX_FREELIST_SIZE = 16ULL;
constexpr size_t DEFAULT_ALIGNMENT = alignof(std::max_align_t);

// #define ARENA_DEBUG_LOGGING
// #define ARENA_MULTITHREADED

#ifdef ARENA_DEBUG_LOGGING
#include <iostream>
#define DEBUG_LOG(x)        \
	do                        \
	{                         \
		std::cout << x << '\n'; \
	} while (0)
#else
#define DEBUG_LOG(x) \
	do                 \
	{                  \
	} while (0)
#endif

namespace arena
{

	//-------------[ CORE ARENA DATA ]----------------------------------------------------------------
	struct Arena
	{
		void *base;
		size_t total_size;
		size_t used_size;
		bool is_owner;
		void *free_lists[MAX_FREELIST_SIZE];
#ifdef ARENA_MULTITHREADED
		std::mutex mutex;
#endif
	};

	struct TempArena
	{
		Arena *parent;
		void *base;
		size_t size;
		size_t used;
	};

	//-------------[ UTILS ]----------------------------------------------------------------
	inline size_t align_up(size_t value, size_t alignment)
	{
		size_t mask = alignment - 1;
		return (value + mask) & ~mask;
	}

	inline size_t size_to_class(size_t size)
	{
		if (size <= MIN_CHUNK_SIZE)
			return 0;
		size_t rounded = MIN_CHUNK_SIZE;
		size_t index = 0;
		while (rounded < size && index < MAX_FREELIST_SIZE - 1)
		{
			rounded <<= 1;
			++index;
		}
		return index;
	}

	inline size_t class_to_size(size_t index)
	{
		return MIN_CHUNK_SIZE << index;
	}

	//-------------[ ARENA LIFECYCLE ]----------------------------------------------------------------
	inline void arena_init(Arena *a, size_t size)
	{
		a->total_size = size;
		a->used_size = 0;
		a->is_owner = true;

#ifdef USE_MMAP_BACKING
		a->base = mmap(nullptr, size, PROT_READ | PROT_WRITE,
									 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (a->base == MAP_FAILED)
		{
			std::abort();
		}
#else
		a->base = std::aligned_alloc(DEFAULT_ALIGNMENT, size);
		if (!a->base)
			std::abort();
#endif

		std::memset(a->free_lists, 0, sizeof(a->free_lists));
		DEBUG_LOG("Arena init " << (size / MB) << " MB at " << a->base);
	}

	inline void arena_init_sub(Arena *a, void *base, size_t size)
	{
		a->base = base;
		a->total_size = size;
		a->used_size = 0;
		a->is_owner = false;
		std::memset(a->free_lists, 0, sizeof(a->free_lists));
		DEBUG_LOG("Sub arena init " << (size / MB) << " MB at " << base);
	}

	inline void arena_destroy(Arena *a)
	{
		if (a->is_owner && a->base)
		{
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

	inline void arena_reset(Arena *a)
	{
#ifdef ARENA_MULTITHREADED
		std::lock_guard<std::mutex> lock(a->mutex);
#endif
		a->used_size = 0;
		std::memset(a->free_lists, 0, sizeof(a->free_lists));
	}

	//-------------[ ALLOC / FREE ]----------------------------------------------------------------
	inline void *arena_alloc(Arena *a, size_t size, size_t alignment)
	{
#ifdef ARENA_MULTITHREADED
		std::lock_guard<std::mutex> lock(a->mutex);
#endif

		// Try size‑class free list.
		if (size <= class_to_size(MAX_FREELIST_SIZE - 1))
		{
			size_t sz_class = size_to_class(size);
			if (sz_class < MAX_FREELIST_SIZE)
			{
				void *node = a->free_lists[sz_class];
				if (node)
				{
					a->free_lists[sz_class] = *reinterpret_cast<void **>(node);
					DEBUG_LOG("Reuse " << class_to_size(sz_class) << " bytes");
					return node;
				}
			}
		}

		size_t aligned_used = align_up(a->used_size, alignment);
		size_t new_used = aligned_used + size;
		if (new_used > a->total_size)
		{
			return nullptr;
		}

		void *ptr = static_cast<char *>(a->base) + aligned_used;
		a->used_size = new_used;
		DEBUG_LOG("Alloc " << size << " bytes at " << ptr);
		return ptr;
	}

	inline void arena_free(Arena *a, void *ptr, size_t size)
	{
		if (ptr == nullptr)
			return;

#ifdef ARENA_MULTITHREADED
		std::lock_guard<std::mutex> lock(a->mutex);
#endif

		if (size <= class_to_size(MAX_FREELIST_SIZE - 1))
		{
			size_t sz_class = size_to_class(size);
			if (sz_class < MAX_FREELIST_SIZE)
			{
				// push front on single‑linked freelist
				*reinterpret_cast<void **>(ptr) = a->free_lists[sz_class];
				a->free_lists[sz_class] = ptr;
				DEBUG_LOG("Free " << size << " bytes to class " << sz_class);
				return;
			}
		}
		DEBUG_LOG("Free " << size << " bytes (ignored)");
	}

	//-------------[ TEMP ARENA ]----------------------------------------------------------------
	inline void temp_init(TempArena *t, Arena *parent, size_t size)
	{
		t->parent = parent;
		t->base = arena_alloc(parent, size, DEFAULT_ALIGNMENT);
		t->size = size;
		t->used = 0;
	}

	inline void temp_destroy(TempArena *t)
	{
		if (t->base && t->parent)
		{
			arena_free(t->parent, t->base, t->size);
		}
		t->base = nullptr;
		t->size = 0;
		t->used = 0;
	}

	inline void *temp_alloc(TempArena *t, size_t size, size_t alignment)
	{
		size_t aligned_used = align_up(t->used, alignment);
		size_t new_used = aligned_used + size;
		if (new_used > t->size)
			return nullptr;
		void *ptr = static_cast<char *>(t->base) + aligned_used;
		t->used = new_used;
		return ptr;
	}

	inline void temp_reset(TempArena *t)
	{
		t->used = 0;
	}

	//-------------[ GLOBAL / THREAD ARENA ]----------------------------------------------------------------
	inline Arena &get_global_arena()
	{
		static Arena global{};
		static bool inited = false;
		if (!inited)
		{
			arena_init(&global, DEFAULT_ARENA_SIZE);
			inited = true;
		}
		return global;
	}

	inline Arena &get_thread_arena()
	{
		static std::thread::id main_id = std::this_thread::get_id();

		thread_local struct ThreadArena
		{
			Arena *arena;
			Arena storage;
			ThreadArena() : arena(nullptr), storage{} {}
			~ThreadArena()
			{
				if (arena == &storage)
				{
					arena_destroy(&storage);
				}
			}
		} tls;

		if (!tls.arena)
		{
			if (std::this_thread::get_id() == main_id)
			{
				tls.arena = &get_global_arena();
			}
			else
			{
				void *backing = arena_alloc(&get_global_arena(),
																		THREAD_LOCAL_ARENA_SIZE,
																		DEFAULT_ALIGNMENT);
				arena_init_sub(&tls.storage, backing, THREAD_LOCAL_ARENA_SIZE);
				tls.arena = &tls.storage;
			}
		}
		return *tls.arena;
	}

	//-------------[ MINIMAL STL ALLOCATOR ]----------------------------------------------------------------
	template <typename T>
	struct Allocator
	{
		using value_type = T;

		Arena *arena;

		Allocator() noexcept : arena(&get_thread_arena()) {}
		explicit Allocator(Arena *a) noexcept : arena(a) {}

		template <class U>
		Allocator(const Allocator<U> &other) noexcept : arena(other.arena) {}

		T *allocate(std::size_t n)
		{
			/* 
			if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
			{
				throw std::bad_array_new_length();
			} */
			assert(n < std::numeric_limits<std::size_t>::max() / sizeof(T) && "bad_array_new_length()")
			void *p = arena_alloc(arena, n * sizeof(T), alignof(T));
			assert(p != nullptr && "bad_alloc()");
			// if (!p)
				// throw std::bad_alloc();
			return static_cast<T *>(p);
		}

		void deallocate(T *p, std::size_t n) noexcept
		{
			arena_free(arena, p, n * sizeof(T));
		}

		template <class U>
		struct rebind
		{
			using other = Allocator<U>;
		};
	};

	template <class T, class U>
	inline bool operator==(const Allocator<T> &a, const Allocator<U> &b) noexcept
	{
		return a.arena == b.arena;
	}
	template <class T, class U>
	inline bool operator!=(const Allocator<T> &a, const Allocator<U> &b) noexcept
	{
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

//-------------[ C‑STYLE TEMP MACROS ]----------------------------------------------------------------
#define WITH_TEMP_ARENA(name, size)                                                                                           \
	for (arena::TempArena name = {};                                                                                            \
			 (name.base == nullptr ? (arena::temp_init(&name, &arena::get_thread_arena(), (size)), 1) : 0) || name.base != nullptr; \
			 (arena::temp_destroy(&name), name.base = (void *)1))

#define TEMP_ALLOC(temp, type) (static_cast<type *>(arena::temp_alloc(&(temp), sizeof(type), alignof(type))))
#define TEMP_ARRAY(temp, count, type) (static_cast<type *>(arena::temp_alloc(&(temp), (count) * sizeof(type), alignof(type))))

} // namespace arena

//-------------[ HASH FOR arena::String ]----------------------------------------------------------------
namespace std
{
	template <>
	struct hash<arena::String>
	{
		size_t operator()(const arena::String &s) const noexcept
		{
			return hash<string_view>{}(string_view(s.data(), s.size()));
		}
	};
}

#endif // _MEMORY_ARENA_DOD_WITH_STL_
