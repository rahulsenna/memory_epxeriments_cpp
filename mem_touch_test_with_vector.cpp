#include <iostream>
#include <vector>
#include <unistd.h>

struct BIG {
    char data[4096];
};

[[noreturn]] int main() {

    std::vector<BIG> numbers;

    numbers.reserve(4096ULL*1000ULL*1000ULL);

    while (true)
    {
        usleep(100000ULL);
        numbers.push_back({});
        std::cout << numbers.size() << " | " << numbers.capacity()/1000'000'000ULL << std::endl;
    }

}
