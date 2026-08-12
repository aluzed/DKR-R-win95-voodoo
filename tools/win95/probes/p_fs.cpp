#include <filesystem>
#include <cstdio>
int main(){ printf("%d\n",(int)std::filesystem::exists("C:\\CONFIG.SYS")); return 0; }
