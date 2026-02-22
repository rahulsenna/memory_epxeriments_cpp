#include <iostream>
#include <cstdlib>
#include "stlAliases.hpp"

int main()
{
	const std::size_t memSize = 1024 * 1024; // 1 MiB
	void *memStart = std::malloc(memSize);

	FreeListAllocator allocator(memSize, memStart);

	// --- std::vector ---
	{
    auto vec = Vector<int>(allocator);

		// STLAdaptor<int, FreeListAllocator> adaptor(allocator);
		// FreeListVector<int> vec(adaptor);

		for (int i = 0; i < 100; ++i)
			vec.push_back(i * i);

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
	}

	std::cout << "After map destruction: " << allocator.GetUsed() << " bytes\n";
	std::free(memStart);
	return 0;
}
