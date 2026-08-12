#include <mutex>
#include <cstdio>
std::mutex m;
int main(){ std::lock_guard<std::mutex> g(m); printf("x\n"); return 0; }
