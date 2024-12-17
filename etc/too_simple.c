#include <stdio.h>

int main(void)
{
    double sum = 0.0;
    double flip = -1.0;
    for (long i = 1; i <= 1000000000; i++) {    
        flip *= -1.0;        
        sum += flip / (2*i - 1);               
    }                        
    printf("%.24f\n", sum*4.0);
    return 0;      
}