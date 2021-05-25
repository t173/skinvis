// fake.c -*-C-*- 
//
// Random stream generator

#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#include "util.h"

#define NUM_PATCHES 1
#define NUM_CELLS   16
#define NUM_ROWS    4
#define NUM_COLS    4

// Patch ID to use for single patch
#define PATCH_ID 5

// Simulated baud rate.  For the real serial device, the rate is one
// byte per 10 cycles (8 + start and stop bits).
#define BAUD 2000000

// Size of a cell record in bytes
#define RECORD_SIZE 5

// Magic number at start of each record
#define RECORD_START 0x55

#define REST_TIME_NS  ( 1000000000L/(BAUD/10)/RECORD_SIZE )
#define REST_SEC      ( REST_TIME_NS / 1000000000L )
#define REST_NSEC     ( REST_TIME_NS % 1000000000L )

#define MAKE_ADDR(c) ((PATCH_ID << 4) | ((c) & 0x0F))

#define MAGNITUDE     (1L << 20)
#define WIDTH         1.5
#define HORIZ_SPEED   2.0

int shutdown = 0;

const int placement[NUM_ROWS][NUM_COLS] = {
	{ 1,  0,  8,  9 },
	{ 3,  2, 10, 11 },
	{ 5,  4, 12, 13 },
	{ 7,  6, 14, 15 }
};

static void *
reader(void *args) {
	int fd = (int)args;
	uint8_t buf;
	for (;;) {
		if ( read(fd, &buf, 1) < 1 )
			return NULL;
	}
	return NULL;
}

void
reader_start(int fd) {
	pthread_t thread;
	if ( pthread_create(&thread, NULL, reader, (void *)fd) != 0 )
		FATAL("Cannot start reader thread");
}

double
gaussian(double x, double pos, double width) {
	const double x1 = x - pos;
	//return exp(-0.5*x1*x1/(width*width))/(width*2.5066282746310002);
	return exp(-0.5*x1*x1/(width*width));
}

uint32_t
get_value(int row, int col) {
	struct timespec now;
	if ( clock_gettime(CLOCK_REALTIME, &now) < 0 ) {
		FATAL("clock_gettime() failed");
	}
	double pos = NUM_COLS*fmod(now.tv_sec + now.tv_nsec*1e-9, HORIZ_SPEED);
	return (uint32_t)(MAGNITUDE*gaussian(col, pos, WIDTH));
	/* printf("%ld.%09ld\n", (long)now.tv_sec, (long)now.tv_nsec); */
	//return row*NUM_COLS + col;
}

// Rest between writing records to simulate baud rate
void rest(void) {
	static const struct timespec res = {
		.tv_sec = REST_SEC,
		.tv_nsec = REST_NSEC
	};
	clock_nanosleep(CLOCK_MONOTONIC, 0, &res, NULL);
}

void
write_record(int fd, int row, int col, uint32_t value) {
	// Magic number (1 byte)
	static const uint8_t magic = RECORD_START;
	if ( write(fd, &magic, 1) < 0 )
		goto write_fail;

	// Address (1 byte)
	const uint8_t addr = MAKE_ADDR(placement[row][col]);
	if ( write(fd, &addr, 1) < 0 )
		goto write_fail;

	// Value (3 bytes)
	for ( int b=2; b>=0; --b ) {
		const uint8_t byte = (value & (0xFF << 8*b)) >> 8*b;
		if ( write(fd, &byte, 1) < 0 )
			goto write_fail;
	}
	rest();
	return;
 write_fail:
	FATAL("Cannot write: %s", strerror(errno));
}

void
writer(int fd) {
	for (;;) {
		for ( int row=0; row<NUM_ROWS; ++row ) {
			for ( int col=0; col<NUM_COLS; ++col ) {
				write_record(fd, row, col, get_value(row, col));
			}
		}
	}
}

int
main(int argc, char *argv[]) {
	int fd;
	if ( argc != 2 ) {
		fprintf(stderr, "No filename given\n");
		return EXIT_FAILURE;
	}
	if ( (fd = open(argv[1], O_RDWR | O_CREAT, 0660)) < 0 )
		FATAL("Cannot open file: %s\n%s", argv[1], strerror(errno));

	reader_start(fd);
	writer(fd);

	close(fd);
	return EXIT_SUCCESS;
}
//EOF
