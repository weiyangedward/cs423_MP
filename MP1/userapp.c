#include "userapp.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	pid_t pid = getpid(); // get pid
    
    FILE *fp = fopen("/proc/mp1/status", "w"); // open proc file
    fprintf(fp, "%d\n", pid); // write to proc file
    fprintf(fp, "%s\n", "hello mp1");
    fclose(fp);

    while(1){};

	return 0;
}
