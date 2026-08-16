#include <chrono>
#include <cstdio>
#include <type_traits>
int main(){
  FILE *f = fopen("D:\\CHRONO.TXT","w");
  if(f){ fprintf(f,"is_steady=%s system_clock=%s steady_clock=%s\n",
    std::chrono::high_resolution_clock::is_steady?"true":"false",
    std::is_same<std::chrono::high_resolution_clock,std::chrono::system_clock>::value?"YES":"no",
    std::is_same<std::chrono::high_resolution_clock,std::chrono::steady_clock>::value?"YES":"no"); fclose(f);} 
  printf("high_resolution_clock::is_steady = %s\n",
         std::chrono::high_resolution_clock::is_steady ? "true" : "false");
  printf("is it system_clock? %s\n",
         std::is_same<std::chrono::high_resolution_clock,
                      std::chrono::system_clock>::value ? "YES" : "no");
  printf("is it steady_clock? %s\n",
         std::is_same<std::chrono::high_resolution_clock,
                      std::chrono::steady_clock>::value ? "YES" : "no");
  printf("period = %lld / %lld s\n",
         (long long)std::chrono::high_resolution_clock::period::num,
         (long long)std::chrono::high_resolution_clock::period::den);
  return 0;
}
