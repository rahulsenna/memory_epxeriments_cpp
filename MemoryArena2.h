#ifndef _MEMORY_ARENA_V2_
#define _MEMORY_ARENA_V2_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <new>
#include <mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <map>
#include <utility>
#include <type_traits>
#include <cassert>
#include <limits>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>

//-------------[ CONFIGURATION ]-----------------------------------------------------------------------
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#define USE_MMAP_BACKING
#else
#define USE_MALLOC_BACKING
#endif

constexpr size_t KB = 1024ULL;
constexpr size_t MB = 1024ULL * KB;
constexpr size_t GB = 1024ULL * MB;
constexpr size_t TB = 1024ULL * GB;

constexpr size_t DEFAULT_ARENA_SIZE = 64ULL * GB;
constexpr size_t THREAD_LOCAL_ARENA_SIZE = 100ULL * MB;
constexpr size_t DEFAULT_ALIGNMENT = alignof(std::max_align_t);
constexpr size_t MIN_CHUNK_SIZE = 64ULL;
constexpr size_t MAX_FREELIST_SIZE = 16;

#define ARENA_DEBUG_LOGGING
// #define ARENA_MULTITHREADED

//-------------[ DEBUGGING ]-----------------------------------------------------------------------
#ifdef ARENA_DEBUG_LOGGING
#include <iostream>
#include <syncstream>
#define DEBUG_LOG(...) std::osyncstream(std::cout) << __VA_ARGS__ << '\n';
// #define DEBUG_LOG(...) std::cout << __VA_ARGS__ << '\n';
#else
#define DEBUG_LOG(...) ((void)0)
#endif

//-------------[ MEMORY ARENA CORE ]-----------------------------------------------------------------------
namespace arena
{

	inline constexpr size_t size_to_class(size_t size)
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

	inline constexpr size_t class_to_size(size_t index)
	{
		return MIN_CHUNK_SIZE << index;
	}

	struct FreeNode
	{
		FreeNode *next;
		FreeNode *prev;
		size_t size;

		void unlink()
		{
			if (next)
				next->prev = prev;
			if (prev)
				prev->next = next;
		}
	};

	class MemoryArena
	{
	public:
		MemoryArena() = default;

		explicit MemoryArena(size_t size)
				: total_size_(size), used_size_(0), is_owner_(true)
		{

#ifdef USE_MMAP_BACKING
			base_ = mmap(nullptr, size, PROT_READ | PROT_WRITE,
									 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
			if (base_ == MAP_FAILED)
			{
				throw std::runtime_error("Failed to mmap memory arena");
			}
#else
			base_ = std::aligned_alloc(DEFAULT_ALIGNMENT, size);
			if (!base_)
				throw std::bad_alloc();
#endif
			std::memset(free_lists_, 0, sizeof(free_lists_));

			DEBUG_LOG("Arena created: " << (size / MB) << " MB at " << base_);
		}

		MemoryArena(void *base, size_t size)
				: base_(base), total_size_(size), used_size_(0), is_owner_(false)
		{
			std::memset(free_lists_, 0, sizeof(free_lists_));
			DEBUG_LOG("Sub-arena created: " << (size / MB) << " MB at " << base_);
		}

		~MemoryArena()
		{
			if (is_owner_ && base_)
			{
#ifdef USE_MMAP_BACKING
				munmap(base_, total_size_);
#else
				std::free(base_);
#endif
				DEBUG_LOG("Arena destroyed");
			}
		}

		MemoryArena(const MemoryArena &) = delete;
		MemoryArena &operator=(const MemoryArena &) = delete;

		template <typename T>
		[[nodiscard]] T *allocate(size_t n)
		{
#ifdef ARENA_MULTITHREADED
			std::lock_guard<std::mutex> lock(mutex_);
#endif
			size_t bytes = n * sizeof(T);
			size_t alignment = std::max(alignof(T), DEFAULT_ALIGNMENT);

			// Free list for single objects only
			if (n == 1)
			{
				size_t sz_class = size_to_class(bytes);
				if (sz_class < MAX_FREELIST_SIZE)
				{
					if (FreeNode *node = pop_free_list(sz_class))
					{
						DEBUG_LOG("Reuse: " << class_to_size(sz_class) << " bytes for " << typeid(T).name());
						return reinterpret_cast<T *>(node + 1);
					}
				}
			}

			// Bump allocation
			size_t aligned_used = align_up(used_size_, alignment);
			size_t total_needed = aligned_used + bytes;

			if (total_needed > total_size_)
			{
				throw std::bad_alloc();
			}

			void *ptr = static_cast<char *>(base_) + aligned_used;
			used_size_ = total_needed;

			DEBUG_LOG("Alloc: " << bytes << " bytes (" << n << " x " << sizeof(T) << ")");
			return static_cast<T *>(ptr);
		}

		template <typename T>
		void deallocate(T *ptr, size_t n) noexcept
		{
			if (!ptr)
				return;

#ifdef ARENA_MULTITHREADED
			std::lock_guard<std::mutex> lock(mutex_);
#endif
			size_t bytes = n * sizeof(T);

			if (n == 1)
			{
				size_t sz_class = size_to_class(bytes);
				if (sz_class < MAX_FREELIST_SIZE)
				{
					FreeNode *node = reinterpret_cast<FreeNode *>(ptr) - 1;
					node->size = class_to_size(sz_class);
					push_free_list(sz_class, node);
					DEBUG_LOG("Free: " << bytes << " bytes to class " << sz_class);
					return;
				}
			}
			DEBUG_LOG("Free: " << bytes << " bytes (leaked)");
		}

		void reset() noexcept
		{
#ifdef ARENA_MULTITHREADED
			std::lock_guard<std::mutex> lock(mutex_);
#endif
			used_size_ = 0;
			std::memset(free_lists_, 0, sizeof(free_lists_));
		}

		size_t total_size() const noexcept { return total_size_; }
		size_t used_size() const noexcept { return used_size_; }
		size_t free_size() const noexcept { return total_size_ - used_size_; }

	private:
		static size_t align_up(size_t value, size_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		FreeNode *pop_free_list(size_t index) noexcept
		{
			FreeNode *node = free_lists_[index];
			if (node)
			{
				free_lists_[index] = node->next;
				if (node->next)
					node->next->prev = nullptr;
			}
			return node;
		}

		void push_free_list(size_t index, FreeNode *node) noexcept
		{
			node->prev = nullptr;
			node->next = free_lists_[index];
			if (free_lists_[index])
				free_lists_[index]->prev = node;
			free_lists_[index] = node;
		}

		void *base_ = nullptr;
		size_t total_size_ = 0;
		size_t used_size_ = 0;
		bool is_owner_ = false;
		FreeNode *free_lists_[MAX_FREELIST_SIZE];

#ifdef ARENA_MULTITHREADED
		std::mutex mutex_;
#endif
	};

	//-------------[ GLOBAL & THREAD-LOCAL ]-----------------------------------------------------------------------
	inline MemoryArena &get_global_arena()
	{
		static MemoryArena arena(DEFAULT_ARENA_SIZE);
		return arena;
	}

	inline MemoryArena &get_thread_arena()
	{
		static std::thread::id main_thread_id = std::this_thread::get_id();

		thread_local struct ThreadArenaHolder
		{
			MemoryArena *arena = nullptr;
			std::aligned_storage_t<sizeof(MemoryArena), alignof(MemoryArena)> storage;

			~ThreadArenaHolder()
			{
				if (arena && std::this_thread::get_id() != main_thread_id)
				{
					arena->~MemoryArena();
				}
			}
		} holder;

		if (!holder.arena)
		{
			if (std::this_thread::get_id() == main_thread_id)
			{
				holder.arena = &get_global_arena();
			}
			else
			{
				void *backing = get_global_arena().allocate<std::byte>(THREAD_LOCAL_ARENA_SIZE);
				holder.arena = new (&holder.storage) MemoryArena(backing, THREAD_LOCAL_ARENA_SIZE);
			}
		}
		return *holder.arena;
	}

	//-------------[ ALLOCATOR ]-----------------------------------------------------------------------
	template <typename T>
	class Allocator
	{
	public:
		using value_type = T;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using propagate_on_container_copy_assignment = std::false_type;
		using propagate_on_container_move_assignment = std::false_type;
		using propagate_on_container_swap = std::false_type;
		using is_always_equal = std::false_type;

		Allocator() noexcept = default;
		explicit Allocator(MemoryArena &arena) noexcept : arena_(&arena) {}

		template <typename U>
		Allocator(const Allocator<U> &other) noexcept : arena_(other.arena()) {}

		[[nodiscard]] T *allocate(std::size_t n)
		{
			if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
			{
				throw std::bad_array_new_length();
			}
			if (arena_)
				return arena_->allocate<T>(n);
			return get_thread_arena().allocate<T>(n);
		}

		void deallocate(T *p, std::size_t n) noexcept
		{
			if (arena_)
				arena_->deallocate(p, n);
			else
				get_thread_arena().deallocate(p, n);
		}

		MemoryArena *arena() const noexcept { return arena_; }

		template <typename U>
		struct rebind
		{
			using other = Allocator<U>;
		};

		template <typename U, typename... Args>
		void construct(U *p, Args &&...args)
		{
			::new (static_cast<void *>(p)) U(std::forward<Args>(args)...);
		}

		template <typename U>
		void destroy(U *p)
		{
			p->~U();
		}

	private:
		MemoryArena *arena_ = nullptr;
	};

	template <typename T, typename U>
	bool operator==(const Allocator<T> &a, const Allocator<U> &b) noexcept
	{
		return a.arena() == b.arena();
	}

	template <typename T, typename U>
	bool operator!=(const Allocator<T> &a, const Allocator<U> &b) noexcept
	{
		return !(a == b);
	}

	//-------------[ TEMPORARY MEMORY ]-----------------------------------------------------------------------
	class TempArena
	{
	public:
		explicit TempArena(size_t size)
				: size_(size), memory_(get_thread_arena().allocate<std::byte>(size)) {}

		~TempArena()
		{
			if (memory_)
				get_thread_arena().deallocate(memory_, size_);
		}

		TempArena(const TempArena &) = delete;
		TempArena &operator=(const TempArena &) = delete;

		TempArena(TempArena &&other) noexcept
				: used_(other.used_), size_(other.size_), memory_(other.memory_)
		{
			other.memory_ = nullptr;
			other.size_ = 0;
			other.used_ = 0;
		}

		TempArena &operator=(TempArena &&other) noexcept
		{
			if (this != &other)
			{
				if (memory_)
					get_thread_arena().deallocate(memory_, size_);
				used_ = other.used_;
				size_ = other.size_;
				memory_ = other.memory_;
				other.memory_ = nullptr;
				other.size_ = 0;
				other.used_ = 0;
			}
			return *this;
		}

		template <typename T>
		[[nodiscard]] T *alloc()
		{
			static_assert(alignof(T) <= DEFAULT_ALIGNMENT);
			size_t aligned_used = align_up(used_, alignof(T));
			if (aligned_used + sizeof(T) > size_)
				throw std::bad_alloc();
			T *ptr = reinterpret_cast<T *>(static_cast<std::byte *>(memory_) + aligned_used);
			used_ = aligned_used + sizeof(T);
			return ptr;
		}

		template <typename T>
		[[nodiscard]] T *alloc_array(size_t n)
		{
			static_assert(alignof(T) <= DEFAULT_ALIGNMENT);
			size_t bytes = n * sizeof(T);
			size_t aligned_used = align_up(used_, alignof(T));
			if (aligned_used + bytes > size_)
				throw std::bad_alloc();
			T *ptr = reinterpret_cast<T *>(static_cast<std::byte *>(memory_) + aligned_used);
			used_ = aligned_used + bytes;
			return ptr;
		}

		void reset() noexcept { used_ = 0; }
		size_t used() const noexcept { return used_; }
		size_t remaining() const noexcept { return size_ - used_; }

	private:
		static size_t align_up(size_t value, size_t alignment) noexcept
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		size_t used_ = 0;
		size_t size_ = 0;
		std::byte *memory_ = nullptr;
	};

	//-------------[ CONTAINER TYPEDEFS ]-----------------------------------------------------------------------
	template <typename T>
	using Vector = std::vector<T, Allocator<T>>;

	template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
	using UnorderedMap = std::unordered_map<K, V, Hash, KeyEqual, Allocator<std::pair<const K, V>>>;

	template <typename K, typename V, typename Compare = std::less<K>>
	using Map = std::map<K, V, Compare, Allocator<std::pair<const K, V>>>;

	using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

	//-------------[ UTILITY ]-----------------------------------------------------------------------
	template <typename T, typename... Args>
	T *new_in_arena(MemoryArena &arena, Args &&...args)
	{
		void *mem = arena.allocate<T>(1);
		return new (mem) T(std::forward<Args>(args)...);
	}

	template <typename T>
	void delete_in_arena(MemoryArena &arena, T *ptr)
	{
		if (ptr)
		{
			ptr->~T();
			arena.deallocate(ptr, 1);
		}
	}

} // namespace arena

//-------------[ HASH SPECIALIZATION ]-----------------------------------------------------------------------
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

//-------------[ C-STYLE TEMP ARENA MACROS ]-----------------------------------------------------------------------
#define WITH_TEMP_ARENA(name, size) \
	for (arena::TempArena name(size); name.remaining() > 0; name.reset())

#define TEMP_ALLOC(temp, type) (temp).alloc<type>()
#define TEMP_ARRAY(temp, count, type) (temp).alloc_array<type>(count)

#endif // _MEMORY_ARENA_V2_
