// skintalk.c -*-C-*-
//
// Skin serial communication interface

#define _XOPEN_SOURCE 700

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
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#include "util.h"
#include "skintalk.h"
#include "profile.h"

#define STOP_CODE   '0'  // stop octocan
#define START1_CODE '1'  // start with original protocol
#define START2_CODE '2'  // start, but including sequence numbers

#define START_CODE START1_CODE

// Size of a cell record in bytes
#if START_CODE == START2_CODE
#define RECORD_SIZE 9
#else
#define RECORD_SIZE 5
#endif

// Size of read buffer
//#define BUFFER_SIZE 4096
#define BUFFER_SIZE 128

// Magic number at start of each record
#define RECORD_START 0x55

// Record some event with a value to debugging log
#define DEBUG_LOG(f, msg, ...) do { \
  if ( f ) { \
    struct timespec now; get_time(&now); \
    fprintf(f, "%ld.%09ld," msg "\n", (long)now.tv_sec, (long)now.tv_nsec, ##__VA_ARGS__); \
  } } while (0)

// A single measurement of a sensor cell
struct record {
	short patch;
	short cell;
	int32_t value;
};

static void
transmit_char(int fd, char code)
{
	fd_set set;
	static struct timeval timeout = {
		.tv_sec = 3,
		.tv_usec = 0
	};
	int ret;
	FD_ZERO(&set);
	FD_SET(fd, &set);

	ret = select(fd + 1, NULL, &set, NULL, &timeout);
	if ( ret < 0 ) {
		FATAL("select(2) error: %s", strerror(errno));
	} else if ( ret == 0 ) {
		WARNING("Timed out while writing to device");
	} else if ( write(fd, &code, 1) < 0 ) {
		WARNING("Cannot write to device");
	}
}

static inline int
is_record_start(uint8_t *p)
{
	return (p[0] == RECORD_START) && (p[RECORD_SIZE] == RECORD_START);
}

static int32_t
convert_24to32(uint8_t *src)
{
	// input is 24 bits (3 bytes) in big-endian: MSB, middle, LSB
	int32_t v = src[0];
	v <<= 8;
	v |= src[1];
	v <<= 8;
	v |= src[2];
	if ( v & 0x00800000 ) {
		//v |= 0xFF000000;
		v = -(~v & 0x00FFFFFF);
	}
	return v;
}

static void
get_record(struct record *dst, uint8_t *src)
{
	dst->patch = (src[1] >> 4);
	dst->cell = src[1] & 0x0F;
	dst->value = convert_24to32(&src[2]);
}

// Wrapper for read(2)
static size_t
read_bytes(int fd, void *dst, size_t count)
{
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

int
skin_init(struct skin *skin, int patches, int cells, const char *device)
{
	DEBUGMSG("skin_init()");
	if ( !skin )
		return 0;
	memset(skin, 0, sizeof(*skin));
	skin->num_patches = patches;
	skin->num_cells = cells;
	skin->device = device;
	profile_init(&skin->profile);
	ALLOCN(skin->value, patches*cells);
	return 1;
}

void
skin_free(struct skin *skin)
{
	DEBUGMSG("skin_free()");
	if ( !skin ) {
		return;
	}
	free(skin->value);
	profile_free(&skin->profile);
}

void
write_csv_header(struct skin *skin, FILE *f)
{
	// Note: internal patch numbers start at 0, external (device/user)
	// start at 1, so here we write 1-based patch numbers
	fprintf(f, "time");
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_cells; c++ ) {
			fprintf(f, ",patch%d_cell%d", p + 1, c);
		}
	}
	fprintf(f, "\n");
}

static void
get_time(struct timespec *dst)
{
	static int warned = 0;
	if ( clock_gettime(CLOCK_REALTIME, dst) < 0 && !warned ) {
		WARNING("clock_gettime() failed: %s", strerror(errno));
		warned = 1;
	}
}

void
write_csv_row(struct skin *skin, FILE *f, FILE *debuglog)
{
	struct timespec now;
	get_time(&now);
	fprintf(f, "%ld.%09ld", (long)now.tv_sec, (long)now.tv_nsec);
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_cells; c++ ) {
			fprintf(f, ",%d", skin_cell(skin, p, c));
		}
	}
	fprintf(f, "\n");
	//fflush(f);
}

//--------------------------------------------------------------------

static inline int
get_bucket(struct skin *skin, int patch, int cell)
{
	if ( skin->num_patches == 1 ) {
		return cell;
	} else {
		return patch*skin->num_cells + cell;
	}
}

// Records a value to a specific cell
void
skin_cell_write(struct skin *skin, int patch, int cell, cell_t value)
{
	if ( skin->calibrating ) {
		const int i = get_bucket(skin, patch, cell);
		skin->calib_sum[i] += value;
		skin->calib_count[i]++;
	} else {
		pthread_mutex_lock(&skin->lock);
		skin_cell(skin, patch, cell) = value;
		skin->total_records++;
		pthread_mutex_unlock(&skin->lock);
	}
}

// pthread function, reads from serial
static void *
skin_reader(void *args)
{
	DEBUGMSG("skin_reader()");
	struct skin *skin = args;
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
		if ( !(log = fopen(skin->log, "wt")) ) {
			WARNING("Cannot open log file %s\n%s", skin->log, strerror(errno));
		} else {
			DEBUGMSG("Logging to %s", skin->log);
			write_csv_header(skin, log);
		}
	}

	FILE *debuglog = NULL;
	if ( skin->debuglog ) {
		if ( !(debuglog = fopen(skin->debuglog, "wt")) ) {
			WARNING("Cannot open debugging log file %s\n%s", skin->debuglog, strerror(errno));
		} else {
			DEBUGMSG("Logging debugging information to %s", skin->debuglog);
		}
		fprintf(debuglog, "time,event,value\n");
	}

	transmit_char(fd, START_CODE);
	skin->total_bytes += read_bytes(fd, buffer, BUFFER_SIZE);

	int advanced = 0;
	for ( int pos=0; !skin->shutdown; ) {
		if ( pos + RECORD_SIZE > BUFFER_SIZE ) {
			// If out of space, roll back the tape and refill it
			DEBUG_LOG(debuglog, "rollback,%d", pos);
			int scrap = BUFFER_SIZE - pos;
			memmove(buffer, buffer + pos, scrap);
			skin->total_bytes += read_bytes(fd, buffer + scrap, BUFFER_SIZE - scrap);
			pos = 0;
		}

		if ( !is_record_start(buffer + pos) ) {
			pos++;
			advanced++;
			continue;
		}
		if ( advanced > 0 ) {
			DEBUG_LOG(debuglog, "advance,%d", advanced);
			advanced = 0;
		}

		get_record(&record, buffer + pos);
		pos += RECORD_SIZE;

		int patch = 0;
		if ( record.cell < skin->num_cells ) {
			if ( skin->num_patches > 1 && record.patch > 0 && record.patch <= skin->num_patches ) {
				// Patch numbers from device start at 1
				patch = record.patch - 1;
			}

			skin_cell_write(skin, patch, record.cell, record.value);

			// Append to log
			if ( log && !skin->calibrating && patch + 1 == skin->num_patches && record.cell + 1 == skin->num_cells ) {
				pthread_mutex_lock(&skin->lock);
				write_csv_row(skin, log, debuglog);
				pthread_mutex_unlock(&skin->lock);
			}
		}
	}
	transmit_char(fd, STOP_CODE);
	if ( log ) {
		fclose(log);
	}
	if ( debuglog ) {
		fclose(debuglog);
	}
	return skin;
}

int
skin_start(struct skin *skin)
{
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
skin_wait(struct skin *skin)
{
	DEBUGMSG("skin_wait()");
	pthread_join(skin->reader, NULL);
	pthread_mutex_destroy(&skin->lock);
}

void
skin_stop(struct skin *skin)
{
	DEBUGMSG("skin_stop()");
	skin->shutdown = 1;
}

int
skin_set_alpha(struct skin *skin, double alpha)
{
	if ( skin && alpha > 0 && alpha <= 1 ) {
		skin->alpha = alpha;
		return 1;
	}
	return 0;
}

void
skin_log_stream(struct skin *skin, const char *filename)
{
	if ( skin )
		skin->log = filename;
}

void
skin_debuglog_stream(struct skin *skin, const char *filename)
{
	if ( skin )
		skin->debuglog = filename;
}

void
skin_calibrate_start(struct skin *skin)
{
	DEBUGMSG("skin_calibrate_start()");
	if ( skin->calibrating || skin->calib_sum || skin->calib_count ) {
		WARNING("Already calibrating!");
		return;
	}
	pthread_mutex_lock(&skin->lock);
	skin->calibrating = 1;
	ALLOCN(skin->calib_sum, skin->num_patches*skin->num_cells);
	ALLOCN(skin->calib_count, skin->num_patches*skin->num_cells);
	profile_tare(&skin->profile);
	pthread_mutex_unlock(&skin->lock);
}

void
skin_calibrate_stop(struct skin *skin)
{
	DEBUGMSG("skin_calibrate_stop()");
	pthread_mutex_lock(&skin->lock);
	skin->calibrating = 0;
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_patches; c++ ) {
			const int i = get_bucket(skin, p, c);
			profile_baseline(skin->profile, p, c) = skin->calib_sum[i]/skin->calib_count[i];
		}
	}
	free(skin->calib_sum);
	free(skin->calib_count);
	skin->calib_sum = NULL;
	skin->calib_count = NULL;
	pthread_mutex_unlock(&skin->lock);
}

void
skin_read_profile(struct skin *skin, const char *csv)
{
	DEBUGMSG("skin_read_profile(\"%s\")", csv);
	if ( skin->calibrating )
		skin_calibrate_stop(skin);

	// Read profile from CSV file
	int patches_read = profile_read(&skin->profile, csv);
	if ( patches_read == 0 ) {
		DEBUGMSG("Read %d patch profiles", patches_read);
	}
}

cell_t
skin_get_calibration(struct skin *skin, int patch, int cell)
{
	// Patch number from user starts at 1
	pthread_mutex_lock(&skin->lock);
	cell_t ret = profile_baseline(skin->profile, patch - 1, cell);
	pthread_mutex_unlock(&skin->lock);
	return ret;
}

//EOF
