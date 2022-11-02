// cmdline.c -*-C-*-
//
// Parse the command line.
//
// To add a new command line option, create an OPT_ macro for the
// option character, add an entry to cmdline_opts[], and then add the
// appropriate code in the switch statement in parse_cmdline().

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include "cmdline.h"

#define OPT_DEVICE  'd'
#define OPT_BAUD    'b'
#define OPT_LOGFILE 'l'
#define OPT_PATCHES 'p'
#define OPT_CELLS   'c'
#define OPT_VERBOSE 'v'
#define OPT_HELP    '?'

// Command line options
struct cmdline_opt {
	char ch;         // option character
	char *desc;      // short description
	char *help;      // (optional) an extra help string
	int has_arg;     // whether or not the option needs an argument
	char *arg_desc;  // (optional) description of argument
} cmdline_opts[] = {
	{ .ch=OPT_DEVICE, .desc="serial device to use", .has_arg=1, .arg_desc="device" },
	{ .ch=OPT_BAUD, .desc="baud rate for serial device", .has_arg=1, .arg_desc="baud" },
	{ .ch=OPT_LOGFILE, .desc="write stream to log file", .has_arg=1, .arg_desc="filename" },
	{ .ch=OPT_PATCHES, .desc="number of patches (default=1)", .has_arg=1, .arg_desc="num" },
	{ .ch=OPT_CELLS, .desc="number of cells per patch (default=16)", .has_arg=1, .arg_desc="num" },
	{ .ch=OPT_HELP, .desc="help", .help="show usage", .has_arg=0 },
	{ .ch=OPT_VERBOSE, .desc="verbose", .help="show more information", .has_arg=0 },
	{ .ch=0 } // null terminator
};

// Default values
struct cmdline cmdline = {
	.device = "/dev/ttyUSB0",
	.baud = 2000000,
	.logfile = NULL,
	.patches = 8,
	.cells = 16,
	.verbose = 0
};

// Use executable name, if given from Makefile
#ifdef EXECNAME
#define TOSTR_EVAL(x) #x
#define TOSTR(x) TOSTR_EVAL(x)
#define EXECUTABLE TOSTR(EXECNAME)
#else
char *argv0;
#define EXECUTABLE argv0
#endif

void show_usage(void) {
	fprintf(stderr, "Usage:\n  %s [<options>]\n\nOptions:\n", EXECUTABLE);

	const char *desc_default = "value";
	size_t desc_maxlen = 0;
	for ( int i=0; cmdline_opts[i].ch; ++i ) {
		if ( cmdline_opts[i].has_arg ) {
			size_t len = cmdline_opts[i].arg_desc ? strlen(cmdline_opts[i].arg_desc) : strlen(desc_default);
			if ( desc_maxlen < len )
				desc_maxlen = len;
		}
	}
	for ( int i=0; cmdline_opts[i].ch; ++i ) {
		if ( cmdline_opts[i].has_arg ) {
			const char *arg_desc = cmdline_opts[i].arg_desc ? cmdline_opts[i].arg_desc : desc_default;
			fprintf(stderr, "  -%c <%s>%*s%s\n", cmdline_opts[i].ch, arg_desc,
							(int)(desc_maxlen - strlen(arg_desc) + 2), "",
							cmdline_opts[i].help ? cmdline_opts[i].help : cmdline_opts[i].desc);
		} else {
			fprintf(stderr, "  -%c%*s%s\n", cmdline_opts[i].ch,
							(int)(desc_maxlen + 5), "",
							cmdline_opts[i].help ? cmdline_opts[i].help : cmdline_opts[i].desc);
		}
	}
}

// Gets a positive integer or quits
int get_positive(const char *str, const char *desc) {
	errno = 0;
	char *pos;
	long value = strtol(str, &pos, 10);
	if ( value <= 0 || errno == ERANGE || *pos != '\0' ) {
		fprintf(stderr, "Invalid %s: %s\n", desc, str);
		show_usage();
		exit(EXIT_FAILURE);
	}
	return value;
}

void parse_cmdline(int argc, char *argv[]) {
#ifndef EXECNAME
	argv0 = argv[0];
#endif

	// Count options
	size_t len = 0;
	for ( int i=0; cmdline_opts[i].ch; ++i ) {
		len++;
		if ( cmdline_opts[i].has_arg )
			len++;
	}

	// Build string of all options
	char optstr[len + 1];
	int pos = 0;
	for ( int i=0; cmdline_opts[i].ch; ++i ) {
		optstr[pos++] = cmdline_opts[i].ch;
		if ( cmdline_opts[i].has_arg )
			optstr[pos++] = ':';
	}

	// Parse command line
	int opt;
	while ( (opt = getopt(argc, argv, optstr)) != -1 ) {
		int index;
		for ( index=0; cmdline_opts[index].ch && cmdline_opts[index].ch != opt; ++index );

		switch ( opt ) {
		case OPT_DEVICE:
			cmdline.device = optarg;
			break;
		case OPT_BAUD:
			cmdline.baud = get_positive(optarg, cmdline_opts[index].desc);
			break;
		case OPT_LOGFILE:
			cmdline.logfile = optarg;
			break;
		case OPT_PATCHES:
			cmdline.patches = get_positive(optarg, cmdline_opts[index].desc);
			break;
		case OPT_CELLS:
			cmdline.cells = get_positive(optarg, cmdline_opts[index].desc);
			break;
		case OPT_HELP:
			show_usage();
			exit(EXIT_SUCCESS);
		case OPT_VERBOSE:
			cmdline.verbose++;
			break;
		default:
			printf("Invalid option: %c\n", opt);
			show_usage();
			exit(EXIT_FAILURE);
		}
	}
}

//EOF
