#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>

char line[TERMINAL_MAX_LINE];

int
main()
{
    // Mutiple threads trying to read on the same terminal 

    Fork();

    int pid = GetPid();
    fprintf(stderr, "pid = %d\n", pid);

    int len;
    fprintf(stderr, "Doing TtyRead...\n");
    len = TtyRead(0, line, sizeof(line));
    fprintf(stderr, "Done with TtyRead: len %d\n", len);
    fprintf(stderr, "line '");
    fwrite(line, 1, len, stderr);
    fprintf(stderr, "'\n");

    Exit(0);
}
