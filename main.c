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

skin_t skin;

void
fullstop(int signum) {
	skin_stop(&skin);
}

int
main(int argc, char *argv[]) {
	parse_cmdline(argc, argv);
	if ( !skin_init(&skin, cmdline.patches, cmdline.cells, cmdline.device, cmdline.history) ) {
		FATAL("Cannot initialize skin structure");
	}
	if ( signal(SIGINT, fullstop) == SIG_ERR ) {
		FATAL("Cannot install signal handler:\n%s\n", strerror(errno));
	}
	if ( cmdline.logfile ) {
		skin_log_stream(&skin, cmdline.logfile);
	}
	if ( !skin_start(&skin) ) {
		FATAL("Cannot start skin reader");
	}
	skin_wait(&skin);
	skin_free(&skin);
	return EXIT_SUCCESS;
}
//EOF
