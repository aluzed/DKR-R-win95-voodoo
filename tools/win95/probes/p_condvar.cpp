#include <condition_variable>
#include <mutex>
#include <cstdio>
std::mutex m; std::condition_variable cv; bool r=false;
int main(){ std::unique_lock<std::mutex> lk(m); cv.wait_for(lk,std::chrono::milliseconds(1),[]{return r;}); printf("x\n"); return 0; }
