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

// (x,y) position of each cell on octocan
// Based on order [[ 8, 10, 13, 15], [ 9, 11, 12, 14], [ 1, 3, 4, 6], [ 0, 2, 5, 7]]
double skincell_posx[] = {-1.5, -1.5, -0.5, -0.5,  0.5,  0.5,  1.5,  1.5, -1.5, -1.5, -0.5, -0.5,  0.5,  0.5,  1.5,  1.5};
double skincell_posy[] = { 1.5,  0.5,  1.5,  0.5,  0.5,  1.5,  0.5,  1.5, -1.5, -0.5, -1.5, -0.5, -0.5, -1.5, -0.5, -1.5};
#define POSX_MIN -1.5
#define POSX_MAX  1.5
#define POSY_MIN -1.5
#define POSY_MAX  1.5

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

// Record some event with a value to debug log
#define EVENT(s, ev, val, ...) do {					\
		if ( (s)->debuglog ) {					\
			struct timespec now; get_time(&now);		\
			fprintf((s)->debuglog, "%ld.%09ld," ev "," val "\n", (long)now.tv_sec, (long)now.tv_nsec, ##__VA_ARGS__); \
		} } while (0)

int placement[] = {8, 10, 13, 15,  9, 11, 12, 14,  1,  3,  4,  6,  0,  2,  5,  7};

// A single raw value from a sensor cell
struct skin_record {
	short patch;
	short cell;
	int32_t value;
};

static void
exp_avg(double *dst, double value, double alpha)
{
	*dst = alpha*value + (1 - alpha)*(*dst);
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

	ret = select(1, NULL, &set, NULL, &timeout);
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
get_record(struct skin_record *dst, uint8_t *src)
{
	// Patch numbers from device start at 1, but cell numbers start at 0
	dst->patch = (src[1] >> 4) - 1;
	dst->cell = src[1] & 0x0F;
	dst->value = convert_24to32(&src[2]);
}

// Wrapper for read(2)
static size_t
read_bytes(struct skin *skin, void *dst, size_t count)
{
	size_t pos = 0;
	size_t read_count = 0;
	ssize_t bytes_read;
	do {
		if ( (bytes_read = read(skin->device_fd, dst + pos, count - pos)) < 0 ) {
			FATAL("Error reading from device:\n%s", strerror(errno));
		}
		if ( skin->debuglog ) {
			struct timespec now; get_time(&now);
			fprintf(skin->debuglog, "%ld.%09ld,read,", (long)now.tv_sec, (long)now.tv_nsec);
			for ( int i=0; i<bytes_read; i++ ) {
				fprintf(skin->debuglog, "%02hhX", ((unsigned char *)(dst + pos))[i]);
			}
			fprintf(skin->debuglog, "\n");
		}
		pos += bytes_read;
		read_count += bytes_read;
	} while ( pos < count );
	return read_count;
}

//--------------------------------------------------------------------

int
skin_init_octocan(struct skin *skin)
{
	return skin_init(skin, "/dev/octocan", 8, 16);
}

int
skin_init(struct skin *skin, const char *device, int patches, int cells)
{
	DEBUGMSG("skin_init()");
	if ( !skin )
		return 0;
	memset(skin, 0, sizeof(*skin));
	skin->num_patches = patches;
	skin->num_cells = cells;
	skin->device = device;
	skin->alpha = 1.0;
	skin->pressure_alpha = 0.5;
	profile_init(&skin->profile);
	ALLOCN(skin->value, patches*cells);
	ALLOCN(skin->pressure, patches);
	skin->log = NULL;
	skin->debuglog = NULL;

	// Open device
	if ( (skin->device_fd = open(skin->device, O_RDWR)) < 0 ) {
		WARNING("Cannot open device: %s", skin->device);
		return 0;
	}
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
	free(skin->pressure);
	profile_free(&skin->profile);
}

void
write_csv_header(struct skin *skin)
{
	if ( !skin || !skin->log )
		return;

	// Note: internal patch numbers start at 0, external (device/user)
	// start at 1, so here we write 1-based patch numbers
	fprintf(skin->log, "time");
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_cells; c++ ) {
			fprintf(skin->log, ",patch%d_cell%d", p + 1, c);
		}
	}
	fprintf(skin->log, "\n");
}

static void
write_csv_row(struct skin *skin)
{
	if ( !skin->log )
		return;

	struct timespec now;
	get_time(&now);
	fprintf(skin->log, "%ld.%09ld", (long)now.tv_sec, (long)now.tv_nsec);
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_cells; c++ ) {
			fprintf(skin->log, ",%g", skin_cell(skin, p, c));
		}
	}
	fprintf(skin->log, "\n");
	//fflush(f);
}

//--------------------------------------------------------------------

static inline int
get_bucket(struct skin *skin, int patch, int cell)
{
	return patch*skin->num_cells + cell;
}

static skincell_t
scale_value(struct skin *skin, int patch, int cell, int32_t rawvalue)
{
	if ( !skin->profile.num_patches )
		return (skincell_t)rawvalue;

	struct patch_profile *p = skin->profile.patch[patch];
	skincell_t value = rawvalue - p->baseline[cell];
	if ( p->c1[cell] == 0.0 ) {
		return 0;
	} else {
		return p->c0[cell] + value*(p->c1[cell] + value*p->c2[cell]);
	}
}

// Records a value to a specific cell
void
skin_cell_write(struct skin *skin, int patch, int cell, int32_t rawvalue)
{
	if ( skin->calibrating ) {
		const int i = get_bucket(skin, patch, cell);
		//pthread_mutex_lock(&skin->lock);
		skin->calib_sum[i] += rawvalue;
		skin->calib_count[i]++;
		//pthread_mutex_unlock(&skin->lock);
	} else {
		skincell_t value = scale_value(skin, patch, cell, rawvalue);
		pthread_mutex_lock(&skin->lock);
		//skin_cell(skin, patch, cell) = skin->alpha*value + (1 - skin->alpha)*skin_cell(skin, patch, cell);
		exp_avg(&skin_cell(skin, patch, cell), value, skin->alpha);
		pthread_mutex_unlock(&skin->lock);
	}
}

// pthread function, reads from serial
static void *
skin_reader(void *args)
{
	DEBUGMSG("skin_reader()");
	struct skin *skin = args;
	uint8_t buffer[BUFFER_SIZE];
	struct skin_record record;

	transmit_char(skin->device_fd, STOP_CODE);
	transmit_char(skin->device_fd, START_CODE);
	skin->total_bytes += read_bytes(skin, buffer, BUFFER_SIZE);

	int advanced = 0;
	for ( int pos=0; !skin->shutdown; ) {
		if ( pos + RECORD_SIZE > BUFFER_SIZE ) {
			// If out of space, rewind the tape and refill it
			EVENT(skin, "rewind", "%d", pos);
			const int scrap = BUFFER_SIZE - pos;
			memmove(buffer, buffer + pos, scrap);
			skin->total_bytes += read_bytes(skin, buffer + scrap, BUFFER_SIZE - scrap);
			pos = 0;
		}

		if ( !is_record_start(buffer + pos) ) {
			pos++;
			advanced++;
			continue;
		}
		if ( advanced > 0 ) {
			EVENT(skin, "misalign", "%d", advanced);
			skin->misalignments++;
			advanced = 0;
		}

		get_record(&record, buffer + pos);
		skin->total_records++;
		pos += RECORD_SIZE;

		EVENT(skin, "parse", "%d.%d=%d", record.patch, record.cell, record.value);
		if ( record.patch >= skin->num_patches || record.cell >= skin->num_cells ) {
			EVENT(skin, "drop", "%d.%d", record.patch, record.cell);
			skin->dropped_records++;
			continue;
		}
		skin_cell_write(skin, record.patch, record.cell, record.value);

		// Append to log if last column for CSV row
		if ( skin->log && !skin->calibrating && record.patch == skin->num_patches - 1 && record.cell == skin->num_cells - 1 ) {
			//pthread_mutex_lock(&skin->lock);
			write_csv_row(skin);
			//pthread_mutex_unlock(&skin->lock);
		}
	}
	transmit_char(skin->device_fd, STOP_CODE);
	if ( skin->log ) {
		fflush(skin->log);
	}
	if ( skin->debuglog ) {
		fflush(skin->debuglog);
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
	skin->reader = 0;
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

int
skin_set_pressure_alpha(struct skin *skin, double alpha)
{
	if ( skin && alpha > 0 && alpha <= 1 ) {
		skin->pressure_alpha = alpha;
		return 1;
	}
	return 0;
}

void
skin_log_stream(struct skin *skin, const char *filename)
{
	if ( !skin || !filename )
		return;
	if ( !(skin->log = fopen(filename, "wt")) ) {
		WARNING("Cannot open log file %s\n%s", filename, strerror(errno));
	} else {
		DEBUGMSG("Logging to %s", filename);
		write_csv_header(skin);
	}
}

void
skin_debuglog_stream(struct skin *skin, const char *filename)
{
	if ( !skin || !filename )
		return;
	if ( !(skin->debuglog = fopen(filename, "wt")) ) {
		WARNING("Cannot open debugging log file %s\n%s", filename, strerror(errno));
	} else {
		DEBUGMSG("Logging debugging information to %s", filename);
	}
	fprintf(skin->debuglog, "time,event,value\n");
}

void
skin_calibrate_start(struct skin *skin)
{
	DEBUGMSG("skin_calibrate_start()");
	if ( !skin->reader ) {
		WARNING("Not reading from device (Try skin_start)");
		return;
	}
	if ( skin->calibrating || skin->calib_sum || skin->calib_count ) {
		WARNING("Calibration already in progress");
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
	int warned = 0;
	pthread_mutex_lock(&skin->lock);
	skin->calibrating = 0;
	for ( int p=0; p < skin->num_patches; p++ ) {
		for ( int c=0; c < skin->num_cells; c++ ) {
			const int i = get_bucket(skin, p, c);
			skincell_t value = 0;
			if ( skin->calib_count[i] > 0 ) {
				value = skin->calib_sum[i]/skin->calib_count[i];
			} else {
				if ( !warned ) {
					WARNING("No calibration samples recorded");
					warned = 1;
				}
			}
			profile_set_baseline(&skin->profile, p, c, value);
			EVENT(skin, "baseline", "%d.%d=%d", p, c, profile_baseline(skin->profile, p, c));
		}
	}
	free(skin->calib_sum);
	skin->calib_sum = NULL;
	free(skin->calib_count);
	skin->calib_count = NULL;
	pthread_mutex_unlock(&skin->lock);
}

int
skin_read_profile(struct skin *skin, const char *csv)
{
	DEBUGMSG("skin_read_profile(\"%s\")", csv);
	if ( skin->calibrating )
		skin_calibrate_stop(skin);

	// Read profile from CSV file
	int ret = profile_read(&skin->profile, csv);
	DEBUGMSG("Read %d patch profiles", ret);
	return ret;
}

skincell_t
skin_get_calibration(struct skin *skin, int patch, int cell)
{
	// Patch number from user starts at 1
	pthread_mutex_lock(&skin->lock);
	skincell_t ret = profile_baseline(skin->profile, patch, cell);
	pthread_mutex_unlock(&skin->lock);
	return ret;
}

int
skin_get_state(struct skin *skin, skincell_t *dst)
{
	pthread_mutex_lock(&skin->lock);
	memcpy(dst, skin->value, skin->num_patches*skin->num_cells*sizeof(*skin->value));
	pthread_mutex_unlock(&skin->lock);
	return skin->num_patches;
}

int
skin_get_patch_state(struct skin *skin, int patch, skincell_t *dst)
{
	pthread_mutex_lock(&skin->lock);
	memcpy(dst, skin->value + patch*skin->num_cells, skin->num_cells*sizeof(*skin->value));
	pthread_mutex_unlock(&skin->lock);
	return 1;
}

int
skin_get_patch_pressure(struct skin *skin, int patch, struct skin_pressure *dst)
{
	const int num_cells = skin->num_cells;
	skincell_t state[num_cells];
	struct skin_pressure p = {};
	skin_get_patch_state(skin, patch, state);
	for ( int c=0; c<num_cells; c++ ) {
		if ( state[c] > SKIN_PRESSURE_MAX )
			state[c] = SKIN_PRESSURE_MAX;
		state[c] /= SKIN_PRESSURE_MAX;
		p.magnitude += state[c];
	}
	//p.magnitude = p.magnitude < 0 ? 0 : p.magnitude;
	p.magnitude = p.magnitude < 0 ? -p.magnitude : p.magnitude;
	for ( int c=0; c<num_cells; c++ ) {
		double norm = p.magnitude == 0.0 ? 1 : state[c]/p.magnitude;
		p.x += norm*skincell_posx[c];
		p.y += norm*skincell_posy[c];
	}
	p.magnitude *= SKIN_PRESSURE_MAX;
	p.x = p.x < POSX_MIN ? POSX_MIN : (p.x > POSX_MAX ? POSX_MAX : p.x);
	p.y = p.y < POSY_MIN ? POSY_MIN : (p.y > POSY_MAX ? POSY_MAX : p.y);
	exp_avg(&skin->pressure[patch].magnitude, p.magnitude, skin->pressure_alpha);
	exp_avg(&skin->pressure[patch].x, p.x, skin->pressure_alpha);
	exp_avg(&skin->pressure[patch].y, p.y, skin->pressure_alpha);
	memcpy(dst, &skin->pressure[patch], sizeof(skin->pressure[patch]));
	return 1;
}

/* int */
/* skin_get_pressure(struct skin *skin, struct skin_pressure *dst) */
/* { */
/* 	return 1; */
/* } */

//EOF
