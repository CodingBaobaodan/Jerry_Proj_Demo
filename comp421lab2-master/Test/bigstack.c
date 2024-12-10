#include <comp421/yalnix.h>
#include <comp421/hardware.h>

void
force(char *addr)
{
    *addr = 42;
}

int
main()
{
    char big_buffer[20*1024];
    int foo;
    int i;

    (void) foo;

    TracePrintf(0, "Here\n");

    foo = 42;
    TracePrintf(0, "Here\n");
    for (i = 0; i < (int) sizeof(big_buffer); i++) 
	force(big_buffer + i);

    Exit(0);
}
