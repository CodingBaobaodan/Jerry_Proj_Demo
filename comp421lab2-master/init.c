#include <stdio.h>
#include <stdlib.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>

int
main(int argc, char **argv)
{
    TracePrintf(0, "Init process running.\n");
    
    (void) argc;
    (void) argv;

    GetPid(); // should work when we implement kernel handler for this

    Delay(3); // delays 3 seconds I think - testing context switch

    GetPid(); 

    Exit(0);
}
