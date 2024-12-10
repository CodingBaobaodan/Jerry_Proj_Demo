#include <stdio.h>
#include <stdlib.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>

int
main(int argc, char **argv)
{
    TracePrintf(0, "Idle process running.\n");
    (void) argc;
    (void) argv;
    
    while (1) {
        Pause();
    }
}
