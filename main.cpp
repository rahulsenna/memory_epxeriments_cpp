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

int main()
{
    Thingy *the = push_struct(Thingy);
    auto temp_memory = begin_temp_memory(MIN_CHUNK_SIZE*100ULL);

    Thingy *th = push_temp_struct(temp_memory, Thingy);
    std::cout << "temp_memory.used: " << temp_memory.used << '\n';


    

    end_temp_memory(temp_memory);



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