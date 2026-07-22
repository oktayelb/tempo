#include "tempo.hpp"



void  long_process(){

    for (int i = 0; i <50; i++)
        printf("%d\n",i);
}



int main(){


    tempo::Function<long_process> long_process_t;
    tempo::FunctionTimer<long_process> long_process_l;

    long_process_t();
    long_process_l();
}