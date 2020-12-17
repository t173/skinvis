// cmdline.h -*-C-*-
//
// Command line parsing

#ifndef __CMDLINE_H
#define __CMDLINE_H

extern struct cmdline {
	char *device;   // serial device
	int baud;       // baud rate
	int history;    // number of values stored in ring buffers
	const char *logfile;  // save data to log file
	int verbose;    // verbose output
	int patches;
	int cells;
} cmdline;

void parse_cmdline(int argc, char *argv[]);

#endif // __CMDLINE_H
