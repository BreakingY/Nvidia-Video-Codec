#include "NvCodecRender.h"
#include <chrono>
#include <thread>
int main(int argc, char **argv)
{
    if (argc < 5) {
        printf("./demo input output gpu_idx use_nvenc(0  -not use 1- use)\n");
        return -1;
    }
    ck(cuInit(0));
    NvCodecRender *test = new NvCodecRender(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]) == 1 ? true : false);
    auto start_time = std::chrono::high_resolution_clock::now();
    test->Render();
    delete test;
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "耗时: " << duration.count() << " 毫秒" << std::endl;
    return 0;
}