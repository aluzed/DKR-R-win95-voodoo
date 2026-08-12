#include <thread>
#include <cstdio>
int main(){ int v=0; std::thread t([&]{v=1;}); t.join(); printf("%d\n",v); return 0; }
