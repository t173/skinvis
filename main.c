// main.c -*-C-*-
//
// Stand alone skin sensor prototype serial communication interface
// checker

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "util.h"
#include "cmdline.h"
#include "skintalk.h"

struct skin skin;

void fullstop(int signum)
{
	skin_stop(&skin);
}

int main(int argc, char *argv[])
{
	parse_cmdline(argc, argv);
	if ( !skin_init(&skin, cmdline.patches, cmdline.cells, cmdline.device) ) {
		FATAL("Cannot initialize skin structure");
	}
	if ( signal(SIGINT, fullstop) == SIG_ERR ) {
		FATAL("Cannot install signal handler:\n%s\n", strerror(errno));
	}
	if ( cmdline.logfile ) {
		skin_log_stream(&skin, cmdline.logfile);
	}
	skin_debuglog_stream(&skin, "debug.out");
	skin_read_profile(&skin, "profile.csv");
	skin_log_stream(&skin, "log.csv");
	skin_set_alpha(&skin, 0.5);
	if ( !skin_start(&skin) ) {
		FATAL("Cannot start skin reader");
	}
	sleep(1);
	skin_calibrate_start(&skin);
	sleep(4);
	skin_calibrate_stop(&skin);
	for ( int p=0; p<skin.num_patches; p++ ) {
		for ( int c=0; c<skin.num_cells; c++ ) {
			printf("(%2d, %2d) = %d\n", p, c, skin_get_calibration(&skin, p, c));
		}
	}

	for ( int t=0; !skin.shutdown && t<10; t++ ) {
		//printf("%lld bytes\t %lld records\n", skin.total_bytes, skin.total_records);
		for ( int c=0; c<skin.num_cells; c++ ) {
			printf("%10d", skin_cell(&skin, 1, c));
		}
		putchar('\n');
		sleep(1);
	}
	skin_stop(&skin);
	skin_wait(&skin);
	printf("total bytes     = %lld\n", skin.total_bytes);
	printf("total records   = %lld\n", skin.total_records);
	printf("skipped records = %lld\n", skin.skipped_records);
	skin_free(&skin);
	return EXIT_SUCCESS;
}
//EOF
