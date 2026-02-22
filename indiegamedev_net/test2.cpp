#include "freelist_allocator.hpp"
#include "stladaptor.hpp"
#include <vector>
#include <iostream>
#include <cstdlib>

template <typename T>
using Vector = std::vector<T, STLAdaptor<T, FreeListAllocator>>;

int main()
{
  const std::size_t poolSize = 1024 * 1024;
  void *memory = std::malloc(poolSize);

  FreeListAllocator allocator(poolSize, memory);

  // Vector<int> vec{STLAdaptor<int, FreeListAllocator>(allocator)};
  auto vec = Vector<int>(STLAdaptor<int, FreeListAllocator>(allocator));

  for (int i = 0; i < 100; ++i)
  {
    vec.push_back(i * i);
  }

  std::cout << "Size: " << vec.size() << "\n";
  std::cout << "Used: " << allocator.GetUsed() << " bytes\n";

  std::free(memory);
  return 0;
}
