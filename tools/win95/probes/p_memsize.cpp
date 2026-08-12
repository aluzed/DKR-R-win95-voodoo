#include <cstdio>
#include <cstddef>
// Copie exacte de librecomp/include/librecomp/addresses.hpp:10-12
constexpr size_t mem_size        = 512ULL * 1024ULL * 1024ULL;
constexpr size_t allocation_size = 4096ULL * 1024ULL * 1024ULL;
int main() {
    printf("sizeof(size_t)   = %d\n", (int)sizeof(size_t));
    printf("mem_size         = %llu (0x%llX)\n",
           (unsigned long long)mem_size, (unsigned long long)mem_size);
    printf("allocation_size  = %llu (0x%llX)\n",
           (unsigned long long)allocation_size, (unsigned long long)allocation_size);
    return 0;
}
