// malloc (MEMORY ALLOCATION) experiment

#include <cstdlib>
#include <iostream>
#include <unistd.h>

using namespace std;


struct BIG{
    char page_size[4096ULL] = {};
};

[[noreturn]] int main()
{
    cout << "Hello, Sailor\n";
    BIG *a = (BIG*) malloc(1000ULL*1000ULL*1000ULL*1000ULL); // 1TB


    BIG b = {};
    uint64_t pos = 0;
    while (true)
    {
        *a++ = b;
        // *(BIG*)((uint8_t*)a+pos) = b;
        pos = pos + 1000ULL*1000ULL*10ULL;
        usleep(1000ULL*100UL);
        cout << "hi\n";
    }

	return(0);
}

/* 
    Run this program and observe that memory is being used by this program. It increases as we touch more pages.
    malloc will only allocate address space, not commit memory until you write to that place.

    Conclusion: You can allocate many GIGs or even TBs of memory and don't have to worry about it. 
*/