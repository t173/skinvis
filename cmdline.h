// cmdline.h -*-C-*-
//
// Command line parsing

#ifndef CMDLINE_H_
#define CMDLINE_H_

extern struct cmdline {
	char *device;   // serial device
	int baud;       // baud rate
	const char *logfile;  // save data to log file
	int verbose;    // verbose output
	int patches;
	int cells;
} cmdline;

void parse_cmdline(int argc, char *argv[]);

#endif // CMDLINE_H_
