#include "tempo.hpp" 

void long_process() {
    for (int i = 0; i < 500; i++);
        //printf("%d\n", i);
}

int main() {
    
    tempo::FunctionTimer<long_process> long_process_l;

    long_process_l();
    long_process_l();

    return 0;
}