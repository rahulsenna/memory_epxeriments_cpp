#pragma once

#include "stlAdaptor.hpp"
#include "freeListAllocator.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <deque>
#include <string>

// Convenience type aliases for using FreeListAllocator with STL containers
// Usage: FreeListVector<int> vec(allocator);

template <typename T>
using FreeListVector = std::vector<T, STLAdaptor<T, FreeListAllocator>>;

template <typename K, typename V>
using FreeListUnorderedMap = std::unordered_map<
		K,
		V,
		std::hash<K>,
		std::equal_to<K>,
		STLAdaptor<std::pair<const K, V>, FreeListAllocator>>;

template <typename K>
using FreeListUnorderedSet = std::unordered_set<
		K,
		std::hash<K>,
		std::equal_to<K>,
		STLAdaptor<K, FreeListAllocator>>;

template <typename T>
using FreeListList = std::list<T, STLAdaptor<T, FreeListAllocator>>;

template <typename T>
using FreeListDeque = std::deque<T, STLAdaptor<T, FreeListAllocator>>;

// String types
template <typename T>
using FreeListBasicString = std::basic_string<
		T,
		std::char_traits<T>,
		STLAdaptor<T, FreeListAllocator>>;

using FreeListString = FreeListBasicString<char>;
using FreeListWString = FreeListBasicString<wchar_t>;
using FreeListU16String = FreeListBasicString<char16_t>;
using FreeListU32String = FreeListBasicString<char32_t>;
