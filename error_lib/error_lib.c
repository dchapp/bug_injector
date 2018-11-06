#include <stdio.h>
#include <unistd.h> 

#define DEBUG

void hang_ms(int hang_time_ms) 
{
#ifdef DEBUG
  printf("Sleeping for %d ms\n", hang_time_ms);
#endif
  usleep(hang_time_ms);
}

void hang() 
{
#ifdef DEBUG
  printf("Hanging\n");
#endif
  while(1) {}
}

void fpe() 
{
#ifdef DEBUG
  printf("Divide by zero\n"); 
#endif
  unsigned long a = 0x800000000;
  unsigned long b = 0;
  unsigned long c = a / b;
  printf("%lu\n", c);
}

