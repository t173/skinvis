#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "skintalk.h"

struct skin skin;

// Can compile like:
// gcc -g -Wall -o example example.c profile.c layout.c skintalk.c -lpthread

void fullstop(int signum)
{
	// Tell the reader thread to stop
	skin_stop(&skin);
}

int main()
{
	if ( signal(SIGINT, fullstop) == SIG_ERR ) {
		return EXIT_FAILURE;
	}

	// First initialize octocan device
	skin_from_layout(&skin, "/dev/ttyUSB0", "octocan2.layout");

	// Exponential smoothing parameter 0 < alpha <= 1 where
	// alpha=1 means no smoothing (always use most recent value)
	// and smaller values average over more history, where
	// (asymptotically) alpha=0 would be never-changing.

	// This is smoothing over individual tactels
	skin_set_alpha(&skin, 0.8);

	// This is smoothing of the center of pressure position
	skin_set_pressure_alpha(&skin, 0.1);

	// Dynamic range calibration comes from external file.  Read
	// this before doing baseline
	skin_read_profile(&skin, "octocan2.calib");
	skin_start(&skin);

	// This is baseline calibration
	printf("Calibrating... DO NOT TOUCH!\n");
	skin_calibrate_start(&skin);
	sleep(4);
	skin_calibrate_stop(&skin);

	// Continuously read patch pressure
	/* struct skin_pressure pressure = {}; */
	/* for (double max=-1; !skin.shutdown; ) { */
	/* 	skin_get_patch_pressure(&skin, 1, &pressure); */
	/* 	printf("%8.3f [%8.3f]   (%8.3f,%8.3f)\n", pressure.magnitude, max, pressure.x, pressure.y); */
	/* 	if ( max < pressure.magnitude ) */
	/* 		max = pressure.magnitude; */
	/* } */

	skincell_t buf[plo->num_cells];
	for (;;) {
		for ( int p=0; p < skin.layout.num_patches; p++ ) {
			// Here, we iterate through all patches. We can get the
			// patch ID while iterating
			struct patch_layout *plo = &skin.layout.patch[p];

			// ... or use this to get a specific patch layout by patch ID
			//struct patch_layout *plo = skin_get_patch_layout(&skin, p);

			printf("Patch %d has %d cells:\n", plo->patch_id, plo->num_cells);
			skin_get_patch_state(&skin, 1, buf);
			for ( int i=0; i < plo->num_cells; i++ ) {
				printf("\t%d: %f\n", plo->cell_id[i], buf[i]);
			}
		}
	}

	// Wait for reader thread to finish. It needs skin_stop signal
	// first!
	skin_wait(&skin);
	skin_free(&skin);
	return EXIT_SUCCESS;
}
