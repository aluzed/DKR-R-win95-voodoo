#include <chrono>
#include <cstdio>
int main(){ auto n=std::chrono::steady_clock::now().time_since_epoch().count(); printf("%lld\n",(long long)n); return 0; }
