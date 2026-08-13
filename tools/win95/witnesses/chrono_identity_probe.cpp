#include <chrono>
#include <cstdio>
#include <type_traits>
int main(){
  FILE *f = fopen("D:\\CHRONO.TXT","w");
  if(f){ fprintf(f,"is_steady=%s system_clock=%s steady_clock=%s\n",
    std::chrono::high_resolution_clock::is_steady?"true":"false",
    std::is_same<std::chrono::high_resolution_clock,std::chrono::system_clock>::value?"OUI":"non",
    std::is_same<std::chrono::high_resolution_clock,std::chrono::steady_clock>::value?"OUI":"non"); fclose(f);} 
  printf("high_resolution_clock::is_steady = %s\n",
         std::chrono::high_resolution_clock::is_steady ? "true" : "false");
  printf("est-ce system_clock ? %s\n",
         std::is_same<std::chrono::high_resolution_clock,
                      std::chrono::system_clock>::value ? "OUI" : "non");
  printf("est-ce steady_clock ? %s\n",
         std::is_same<std::chrono::high_resolution_clock,
                      std::chrono::steady_clock>::value ? "OUI" : "non");
  printf("periode = %lld / %lld s\n",
         (long long)std::chrono::high_resolution_clock::period::num,
         (long long)std::chrono::high_resolution_clock::period::den);
  return 0;
}
