#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "skintalk.h"

struct skin skin;

// Can compile like:
// gcc -g -Wall -o example example.c profile.c skintalk.c -lpthread

void fullstop(int signum)
{
	// Tell the reader thread to stop
	skin_stop(&skin);
}

int main()
{
	struct skin_pressure pressure = {};

	if ( signal(SIGINT, fullstop) == SIG_ERR ) {
		return EXIT_FAILURE;
	}

	// First initialize octocan device. It will assume a symlink
	// for /dev/octocan
	skin_init_octocan(&skin);

	// Exponential smoothing parameter 0 < alpha <= 1 where
	// alpha=1 means no smoothing (always use most recent value)
	// and smaller values average over more history, where
	// (asymptotically) alpha=0 would be never-changing.

	// This is smoothing over individual tactels
	skin_set_alpha(&skin, 0.5);

	// This is smoothing of the center of pressure position
	skin_set_pressure_alpha(&skin, 0.3);

	// Dynamic range calibration comes from external file.  Read
	// this before doing baseline
	skin_read_profile(&skin, "profile.csv");
	skin_start(&skin);

	// This is baseline calibration
	printf("Calibrating... DO NOT TOUCH!\n");
	skin_calibrate_start(&skin);
	sleep(4);
	skin_calibrate_stop(&skin);

	// Continuously read patch pressure
	while (!skin.shutdown) {
		skin_get_patch_pressure(&skin, 1, &pressure);
		printf("%8.3f   (%8.3f,%8.3f)\n", pressure.magnitude, pressure.x, pressure.y);
	}

	// Wait for reader thread to finish. It needs skin_stop signal
	// first!
	skin_wait(&skin);
	skin_free(&skin);
	return EXIT_SUCCESS;
}
