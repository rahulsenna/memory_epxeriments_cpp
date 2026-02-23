#include <iostream>
#include <cstdlib>
#include <sstream>
#include "stlAliases.hpp"

struct THING
{
  int a[16];
};

int main()
{
	const std::size_t memSize = 1024 * 1024 *1024; // 1 GiB
	void *memStart = std::malloc(memSize);

	FreeListAllocator allocator(memSize, memStart);

	// --- std::vector ---
	{
    auto vec = Vector<int>(allocator);

		// STLAdaptor<int, FreeListAllocator> adaptor(allocator);
		// FreeListVector<int> vec(adaptor);

		for (int i = 0; i < 100; ++i)
    {
      int freeBlocks = 1;
      auto f = allocator.m_freeBlocks;
      std::ostringstream iss;
      while (f)
      {
        iss << freeBlocks << ' ' <<  (void*)f << ' ' << f->size << ' ';
        freeBlocks++;
        f = f->next;
      }

      vec.push_back(i * i);
      THING *a = (THING*)allocator.Allocate(sizeof(THING), alignof(THING));
      // void *a;
      printf("i: %d | vec.data(): %p | a: %p | allocations: %lu | freeBlocks(%d): %s\n", i, vec.data(), a, allocator.GetNumAllocation(), freeBlocks-1, iss.str().c_str());
      // allocator.Free(a);
      
    }
			

		std::cout << "Vector size: " << vec.size() << "\n";
		std::cout << "Used memory: " << allocator.GetUsed() << " bytes\n";

    auto vec2 = vec;

    std::cout << "2 Vector size: " << vec2.size() << "\n";
		std::cout << "2 Used memory: " << allocator.GetUsed() << " bytes\n";
	} // vector destructor frees all memory back to the allocator

	std::cout << "After vector destruction: " << allocator.GetUsed() << " bytes\n\n";

	// --- std::unordered_map ---
	{
		// STLAdaptor<std::pair<const int, std::string>, FreeListAllocator> mapAdaptor(allocator);
		// FreeListUnorderedMap<int, std::string> map(mapAdaptor);
    auto map = UnorderedMap<int, std::string>(allocator);

    map[1] = "One";
		map[2] = "Two";
		map[3] = "Three";

		for (const auto &[k, v] : map)
			std::cout << k << " -> " << v << "\n";

		std::cout << "Used memory: " << allocator.GetUsed() << " bytes\n";

    auto f = allocator.m_freeBlocks;
      while(f)
      {
        printf("f: %p | f->size: %lu\n", f, f->size);
        f = f->next;
      }
	}

	std::cout << "After map destruction: " << allocator.GetUsed() << " bytes\n";
	std::free(memStart);
	return 0;
}
