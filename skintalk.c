// skintalk.c -*-C-*- 
//
// Skin serial communication interface

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
//#include <termios.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "util.h"
#include "skintalk.h"
#include "ring.h"

#define START_CODE  '1'
#define STOP_CODE   '0'

// Size of read buffer
//#define BUFFER_SIZE 4096
#define BUFFER_SIZE 128

// Size of a cell record in bytes
#define RECORD_SIZE 5

// Magic number at start of each record
#define RECORD_START 0x55

// A single measurement of a sensor cell
typedef struct record {
	short patch;
	short cell;
	int32_t value;
} record_t;

static void transmit_char(int fd, char code) {
	if ( write(fd, &code, 1) < 0 ) {
		WARNING("Cannot write to device");
	}
}

static int is_record_start(uint8_t *p) {
//	return ((p[0] & 0xF0) == 0x10) && ((p[RECORD_SIZE] & 0xF0) == 0x10);
	return (p[0] == RECORD_START) && (p[RECORD_SIZE] == RECORD_START);
}

inline static int32_t convert_24to32(uint8_t *src) {
	// input is bytes in big-endian: MSB, middle, LSB
	int32_t v = src[0];
	v <<= 8;
	v |= src[1];
	v <<= 8;
	v |= src[2];
	if ( v & 0x00800000 ) {
		v |= 0xFF000000;
	}
	return v;
}

static void get_record(struct record *dst, uint8_t *src) {
	dst->patch = (src[1] >> 4);
	dst->cell = src[1] & 0x0F;

	/* // src[2..4] is 24 bits of network order (big endian) signed value. */
	/* // Here we assume a little-endian host, reverse the order, and sign */
	/* // extend to 32 bits. */
	/* uint8_t stage[4]; */
	/* stage[0] = src[4]; */
	/* stage[1] = src[3]; */
	/* stage[2] = src[2]; */
	/* stage[3] = (stage[2] & (1<<7)) ? 0xFF : 0x00; */

	/* //dst->value = *(int32_t *)stage; */
	/* /\* uint8_t stage[4] = {}; *\/ */
	/* /\* memcpy(&stage[1], &src[2], 3); *\/ */
	/* /\* stage[0] = (stage[1] & (1<<7)) ? 0xFF : 0x00; *\/ */
	/* int32_t value = 0; */
	/* memcpy(&value, stage, 4); */
	/* //dst->value = value; */
	dst->value = convert_24to32(&src[2]);

//#ifdef DEBUG
//#if (DEBUG >= 2)
	//printf(" %02X %02X %02X %02X %02X patch=%01X cell=%01X value=%08X (%d)\n", src[0], src[1], src[2], src[3], src[4], dst->patch, dst->cell, dst->value, dst->value);
//#endif
//#endif
}

// Wrapper for read(2)
static size_t read_bytes(int fd, void *dst, size_t count) {
	size_t pos = 0;
	size_t read_count = 0;
	ssize_t bytes_read;
	do {
		if ( (bytes_read = read(fd, dst + pos, count - pos)) < 0 ) {
			FATAL("Error reading from device:\n%s", strerror(errno));
		}
		pos += bytes_read;
		read_count += bytes_read;
	} while ( pos < count );
	return read_count;
}

//--------------------------------------------------------------------

#define RING_AT(s,p,c) ( (s)->rings[((s)->num_cells)*(p) + (c)] )

// Allocates and initializes ring buffers
static int
skin_allocate(skin_t *skin, int patches, int cells) {
	if ( !(skin->rings = malloc(patches*cells*sizeof(*skin->rings))) ) {
		WARNING("Cannot allocate ring buffers");
		return 0;
	}
	for ( int p=0; p<patches; ++p ) {
		for ( int c=0; c<cells; ++c ) {
			if ( !ring_init(&RING_AT(skin, p, c), skin->history) ) {
				WARNING("Cannot initialize ring buffer");
				free(skin->rings);
				return 0;
			}
		}
	}
	return 1;
}

int
skin_init(skin_t *skin, int patches, int cells, const char *device, int history) {
	DEBUGMSG("skin_init()");
	if ( !skin )
		return 0;
	skin->num_patches = patches;
	skin->num_cells = cells;
	skin->device = device;
	skin->history = history;
	skin->total_bytes = 0;
	skin->total_records = 0;
	skin->log = NULL;
	return skin_allocate(skin, patches, cells);
}

void
skin_free(skin_t *skin) {
	DEBUGMSG("skin_free()");
	if ( !skin ) {
		return;
	}
	for ( int p=0; p < skin->num_patches; ++p ) {
		for ( int c=0; c < skin->num_cells; ++c ) {
			ring_free(&RING_AT(skin, p, c));
		}
	}
	free(skin->rings);
}

//--------------------------------------------------------------------

// pthread function, reads from device
static void *
skin_reader(void *args) {
	DEBUGMSG("skin_reader()");
	skin_t *skin = args;
	int fd;
	uint8_t buffer[BUFFER_SIZE];
	struct record record;

	if ( (fd = open(skin->device, O_RDWR)) < 0 ) {
		WARNING("Cannot open device: %s", skin->device);
		return NULL;
	}
	transmit_char(fd, STOP_CODE);

	// Start logger
	FILE *log = NULL;
	if ( skin->log ) {
		if ( !(log = fopen(skin->log, "wb")) ) {
			WARNING("Cannot open log file %s\n%s", skin->log, strerror(errno));
			return NULL;
		}
	}

	//struct timeval before, now;
	//long long bytes_before = total_bytes;
	//long long records_before = records_read;
	//gettimeofday(&before, NULL);

	transmit_char(fd, START_CODE);
	skin->total_bytes += read_bytes(fd, buffer, BUFFER_SIZE);

	for ( int pos=0; !skin->shutdown; ) {
		/* if ( cmdline.verbose && records_read % 10000 == 0 ) {  */
		/* 	gettimeofday(&now, NULL); */
		/* 	struct timeval delta; */
		/* 	delta.tv_sec = now.tv_sec - before.tv_sec; */
		/* 	if ( now.tv_usec < before.tv_usec ) { */
		/* 		delta.tv_sec--; */
		/* 		delta.tv_usec = 1000000 + now.tv_usec - before.tv_usec; */
		/* 	} else { */
		/* 		delta.tv_usec = now.tv_usec - before.tv_usec; */
		/* 	} */
		/* 	const long long bytes_since = total_bytes - bytes_before; */
		/* 	const double time_delta = delta.tv_sec + 1e-6*delta.tv_usec; */
		/* 	const double rate_KB = bytes_since/time_delta/1024; */
		/* 	const long long records_since = records_read - records_before; */
		/* 	const double records_per_sec = records_since/time_delta; */
			
		/* 	printf("reading %.1f KB/s, %.0f cell/s\n", rate_KB, records_per_sec); */
		/* 	bytes_before = total_bytes; */
		/* 	records_before = records_read; */
		/* 	memcpy(&before, &now, sizeof(now)); */
		/* } */

	next_pos:
		if ( pos + RECORD_SIZE > BUFFER_SIZE ) {
			// If out of space, roll back the tape and refill it
			memmove(buffer, buffer + pos, BUFFER_SIZE - pos);
			pos = BUFFER_SIZE - pos;
			skin->total_bytes += read_bytes(fd, buffer + pos, BUFFER_SIZE - pos);
		}
		if ( !is_record_start(buffer + pos) ) {
			pos++;
			goto next_pos;
		}
		get_record(&record, buffer + pos);
		if ( log ) {
			fwrite(buffer + pos, sizeof(*buffer), RECORD_SIZE, log);
		}
		pos += RECORD_SIZE;

		// Note: patch numbers from device start at 1
		if ( record.patch > 0 && record.patch - 1 < skin->num_patches && record.cell < skin->num_cells ) {
			//printf("patch=%d  cell=%d  value=%.0f\n", record.patch, record.cell, RING_AT(skin, record.patch - 1, record.cell).expavg);
			pthread_mutex_lock(&skin->lock);
			ring_write(&RING_AT(skin, record.patch - 1, record.cell), record.value);
			pthread_mutex_unlock(&skin->lock);
			skin->total_records++;
		}
	}
	transmit_char(fd, STOP_CODE);
	if ( log ) {
		fclose(log);
	}
	return skin;
}

int
skin_start(skin_t *skin) {
	DEBUGMSG("skin_start()");
	skin->shutdown = 0;
	if ( pthread_mutex_init(&skin->lock, NULL) != 0 ) {
		WARNING("Cannot initilize mutex");
		return 0;
	}
  if ( pthread_create(&skin->reader, NULL, skin_reader, skin) != 0 ) {
		WARNING("Cannot start reader thread");
		return 0;
	}
	return 1;
}

void
skin_wait(skin_t *skin) {
	DEBUGMSG("skin_wait()");
	pthread_join(skin->reader, NULL);
	pthread_mutex_destroy(&skin->lock);
}

void
skin_stop(skin_t *skin) {
	DEBUGMSG("skin_stop()");
	skin->shutdown = 1;
}

void
skin_get_history(skin_t *skin, ring_data_t *dst, int patch, int cell) {
	pthread_mutex_lock(&skin->lock);
	ring_get_history(&RING_AT(skin, patch - 1, cell), dst);
	pthread_mutex_unlock(&skin->lock);
}

int
skin_set_alpha(skin_t *skin, double alpha) {
	for ( int p=0; p < skin->num_patches; ++p )
		for ( int c=0; c < skin->num_cells; ++c )
			if ( !ring_set_alpha(&RING_AT(skin, p, c), alpha) )
				return 0;
	return 1;
}

double
skin_get_expavg(skin_t *skin, int patch, int cell) {
	double avg;
	pthread_mutex_lock(&skin->lock);
	avg = RING_AT(skin, patch - 1, cell).expavg;
	pthread_mutex_unlock(&skin->lock);
	return avg;
}

void
skin_log_stream(skin_t *skin, const char *filename) {
	if ( skin ) {
		skin->log = filename;
	}
}

void
skin_calibrate_start(skin_t *skin) {
	DEBUGMSG("skin_calibrate_start()");
	pthread_mutex_lock(&skin->lock);
	for ( int p=0; p < skin->num_patches; ++p )
		for ( int c=0; c < skin->num_cells; ++c )
			ring_calibrate_start(&RING_AT(skin, p, c));
	pthread_mutex_unlock(&skin->lock);
}

void
skin_calibrate_stop(skin_t *skin) {
	DEBUGMSG("skin_calibrate_stop()");
	pthread_mutex_lock(&skin->lock);
	for ( int p=0; p < skin->num_patches; ++p )
		for ( int c=0; c < skin->num_cells; ++c )
			ring_calibrate_stop(&RING_AT(skin, p, c));
	pthread_mutex_unlock(&skin->lock);
}

//EOF
