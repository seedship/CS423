#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "userapp.h"

#define FILE_NAME "/proc/mp1/status"


int main(int argc, char* argv[])
{
	char buffer[100];
	FILE *f;


	if( access(FILE_NAME, F_OK ) != -1 ) {
		pid_t pid = getpid();
		printf("userapp pid: %u\n", pid);
		sprintf(buffer, "%u", pid);
		f = fopen(FILE_NAME, "w");
		fwrite(buffer, strlen(buffer), 1, f);
		fflush(f);
		printf("Writing to ");
		printf(FILE_NAME);
		printf("\n");
		fclose(f);
		if(argc < 2){
			for(unsigned long x = 0; x != 1l<<32; x++);
			return 0;
		} else {
			for(unsigned long x = 0; x < atoi(argv[1]); x++);
			return 0;
		}
	} else {
		printf("Cannot find proc entry, is the kernel module running?\n");
		return -1;
	}



	return 0;
}
