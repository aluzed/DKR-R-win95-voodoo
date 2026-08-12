#include <atomic>
#include <cstdio>
std::atomic<int> a{0}; std::atomic<long long> b{0};
int main(){ a.fetch_add(1); int e=1; a.compare_exchange_strong(e,2); b.fetch_add(1); printf("%d %lld\n",a.load(),b.load()); return 0; }
