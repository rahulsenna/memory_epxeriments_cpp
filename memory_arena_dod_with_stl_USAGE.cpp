#include "memory_arena_dod_with_stl.h"

#include <iostream>

struct ArenaScope
{
	arena::Arena a;
	ArenaScope(size_t size) { arena_init(&a, size); }
	~ArenaScope() { arena_destroy(&a); }
	arena::Arena *operator&() { return &a; }
};

void foo() {
    // Uses thread arena by default
    arena::Vector<int> v(100);
    v.push_back(0);
		arena::String name = "Rahul";
		std::cout << "name: " << name << '\n';

    arena::String s{"hello", arena::Allocator<char>(&arena::get_thread_arena())};

		std::cout << "v.size(): " << v.size() << '\n';

    // Explicit arena
    ArenaScope scope(1001*sizeof(float));
    arena::Vector<float> vf{ arena::Allocator<float>(&scope) };
		vf.reserve(1200);
    // vf.push_back(3.14f);
		// for (int i = 0; i < 999; ++i)
		// {
		// 	vf.push_back(i);
		// }
		std::cout << "vf.size(): " << vf.size() << '\n';
}

int main()
{
	foo();
	return 0;
}
