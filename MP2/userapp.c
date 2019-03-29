#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "userapp.h"

#define FILE_NAME "/proc/mp2/status"

/**
 * @brief main -- This is the main loop of the userapp. It registers itself in
 * the MP2 procfs. It takes 3 parameters for computation, period, and number of
 * jobs. It then waits for the computation duration to pass before yielding.
 * @param argc - number of arguments
 * @param argv - computation, period, and number of repetitions
 * @return 2 if invalid syntax, 1 if not registered, 0 if successful, -1 if
 * kernel module not found
 */
int main(int argc, char* argv[])
{
	// Print out command usage
	if(argc < 4){
		printf("Usage: ./userapp [computation(ms)] [period(ms)] [number of repetitions]\n");
		return 2;
	}

	// Parse arguments
	int computation = atoi(argv[1]);
	int period = atoi(argv[2]);
	int reps = atoi(argv[3]);

	// Get PID
	pid_t pid = getpid();

	// Buffers
	char buffer[100]; // "R, PID, PERIOD, COMPUTATION";
	char kern_info[500];
	FILE *f;

	// Check if kernel module is running
	if( access(FILE_NAME, F_OK ) != -1 ) {
		// Register
		sprintf(buffer, "R, %u, %u, %u", pid, period, computation);
		f = fopen(FILE_NAME, "r+");
		fwrite(buffer, strlen(buffer), 1, f);
		fflush(f);

		// Make sure process is registered
		fread(kern_info, 1, 99, f);
		sprintf(buffer, "PID:\t%u", pid);
		if(!strstr(kern_info, buffer)) return 1;

		// Yield and begin periodic scheduling
		sprintf(buffer, "Y, %u", pid);
		fwrite(buffer, strlen(buffer), 1, f);
		fflush(f);

		// Do jobs
		for(unsigned int idx = 0; idx < reps; idx++){
			struct timeval t, t0;
			unsigned elapsed = 0;
			gettimeofday(&t0, NULL);
			unsigned long mstime = t0.tv_sec * 1000 + t0.tv_usec/1000;
			printf("%u (%u %u): Released at time %lu\n", pid, computation, period, mstime);
			// Job
			while(elapsed < computation){
				gettimeofday(&t0, NULL);
				for(unsigned x = 0; x < 1<<20; x++);
				gettimeofday(&t, NULL);
				elapsed += (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_usec - t0.tv_usec)/1000;
			}

			// Printing compute time and yielding
			gettimeofday(&t0, NULL);
			mstime = t0.tv_sec * 1000 + t0.tv_usec/1000;
			printf("%u (%u %u): Yielding at time %lu. Processing time:%u\n", pid, computation, period, mstime, elapsed);
			sprintf(buffer, "Y, %u", pid);
			fwrite(buffer, strlen(buffer), 1, f);
			fflush(f);

		}
		// Deregister
		sprintf(buffer, "D, %u", pid);
		fwrite(buffer, strlen(buffer), 1, f);
		fflush(f);
		fclose(f);

		printf("Userapp with instance %u done!\n", pid);
	} else {
		// Proc entry not found
		printf("Cannot find proc entry, is the kernel module running?\n");
		return -1;
	}

	return 0;
}
