#include <stdio.h>
#include <unistd.h> 

// This function should be injected wherever we want a "hang" error 
void hang(int hang_time_ms) {
  if (hang_time_ms > 0) {
    printf("Hanging for %d milliseconds\n", hang_time_ms);
    usleep(hang_time_ms);
  } else {
    printf("Hanging until signal\n");
    while(1) {}
  }
}

