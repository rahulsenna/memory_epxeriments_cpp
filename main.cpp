#include <iostream>
#define LOG_ARENA
#define INITIALIZE_MEMORY_ARENA
#include "MemroyArenaNameSpace.h"


struct Thingy
{
    // char data;
    int meta;
    double deta;
};
#include <thread>

void foo()
{
    std::vector<int, rs::ArenaAllocator<int>>Lode;
    Lode.reserve(100);

    rs::vector<int> Mader;

    printf("Lode.data(): %p\n", Lode.data());
    printf("Mader.data(): %p\n", Mader.data());

    std::cout << "rs::get_thread_arena().base_address: " << rs::get_thread_arena().base_address << '\n';
    rs::GLOBAL_ARENA.deallocate<uint8_t>(rs::get_thread_arena().base_address, 100ULL*MB);

}

int main()
{
    std::vector<int> cap;
    std::cout << "cap.capacity(): " << cap.capacity() << '\n';

#if  0
    std::unordered_map<int, char> example{{1, 'a'}, {2, 'b'}};
 
    for (int x : {2, 5})
        if (example.contains(x))
            std::cout << x << ": Found\n";
        else
            std::cout << x << ": Not found\n";

#endif  //0            

    std::vector<int, rs::ArenaAllocator<int>>Lode;
    Lode.reserve(100);
    // Lode.push_back(1);
    // Lode.push_back(1);
    // Lode.push_back(1);

    rs::vector<uint64_t> bsdk;


    // Thingy *the = push_struct(Thingy);
    // auto temp_memory = begin_temp_memory(MIN_CHUNK_SIZE*100ULL);
    // Thingy *th = push_temp_struct(temp_memory, Thingy);
    // std::cout << "temp_memory.used: " << temp_memory.used << '\n';    
    // end_temp_memory(temp_memory);
#if  1
    std::thread t1{foo};
    std::thread t2{foo};
    // std::thread t3{foo};
    t1.join();
    t2.join();
    // t3.join();
#endif    



#if  0
    
    rs::vector<rs::string> names;

    rs::unordered_map<rs::string, rs::vector<rs::string>> mp;


    for (int i = 0; i < 10; ++i)
    {
        mp[std::to_string(i)].push_back("one") ;
        names.emplace_back("Rahul");
    }

    for (auto [k,v]: mp)
    {
        std::cout << "k: " << k  << " v: " << v.front() << '\n';
    }
    for (auto name: names)
    {
    	std::cout << "name: " << name << '\n';
    }

#endif  //0
    std::cout << "Hello Sailor" << '\n';
	return(0);

}