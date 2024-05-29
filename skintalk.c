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
#include "layout.h"

#define STOP_CODE  "0"
#define START_CODE "1"

/* #define STOP_CODE  "S" */
/* #define START_CODE "X" */
/* #define MUX_CODE "M" */
/* #define CALIB_CODE "C" */

#define RECORD_SIZE 5

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

// A single (parsed) raw value from a sensor cell
struct skin_record {
	short patch;
	short cell;
	int32_t value;
};

// Get cell c from patch p of struct skin *s
#define skin_cell(s, p, c) ( (s)->value[(s)->idx[(p)][(c)]] )

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
	dst->patch = (src[1] >> 4);
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

/* int */
/* skin_init_octocan(struct skin *skin) */
/* { */
/* 	return skin_init(skin, "/dev/octocan", 8, 16); */
/* } */

/* int */
/* skin_init(struct skin *skin, const char *device, int patches, int cells) */
/* { */
/* 	DEBUGMSG("skin_init()"); */
/* 	if ( !skin ) */
/* 		return 0; */
/* 	memset(skin, 0, sizeof(*skin)); */
/* 	skin->num_patches = patches; */
/* 	skin->total_cells = patches*cells; */
/* 	skin->device = device; */
/* 	skin->alpha = 1.0; */
/* 	skin->pressure_alpha = 0.5; */
/* 	profile_init(&skin->profile); */
/* 	ALLOCN(skin->value, patches*cells); */
/* 	ALLOCN(skin->pressure, patches); */
/* 	skin->log = NULL; */
/* 	skin->debuglog = NULL; */

/* 	// Open device */
/* 	if ( (skin->device_fd = open(skin->device, O_RDWR | O_NONBLOCK)) < 0 ) { */
/* 		WARNING("Cannot open device: %s", skin->device); */
/* 		return 0; */
/* 	} */
/* 	return 1; */
/* } */

int
skin_from_layout(struct skin *skin, const char *device, const char *lofile)
{
	DEBUGMSG("skin_from_layout()");
	if ( !skin || !lofile )
		return 0;
	memset(skin, 0, sizeof(*skin));
	skin->device = device;
	skin->alpha = 1.0;
	skin->pressure_alpha = 0.5;
	profile_init(&skin->profile);

	if ( !layout_read(&skin->layout, lofile) )
		return 0;

	skin->num_patches = skin->layout.num_patches;
	ALLOCN(skin->pressure, skin->num_patches);
	skin->log = NULL;
	skin->debuglog = NULL;

	// Build indexing map (physical addr -> array index)
	ALLOCN(skin->idx, skin->layout.max_patch_id);
	int count = 0;
	for ( int p=0; p < skin->num_patches; p++ ) {
		const struct patch_layout *pl = &skin->layout.patch[p];
		ALLOCN(skin->idx[pl->patch_id], pl->max_cell_id);

		// Sentinel value <0 for "unused"
		for ( int i=0; i < pl->max_cell_id; i++ ) {
			skin->idx[pl->patch_id][i] = -1;
		}
		for ( int i=0; i < pl->num_cells; i++ ) {
			skin->idx[pl->patch_id][pl->cell_id[i]] = count++;
		}
	}

	skin->total_cells = count;
	ALLOCN(skin->value, count);
	if ( skin->layout.total_cells != count ) {
		WARNING("Layout reports %d cells instead of %d", skin->layout.total_cells, count);
	}

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
	for ( int p=0; p < skin->num_patches; p++ ) {
		free(skin->idx[skin->layout.patch[p].patch_id]);
	}
	free(skin->idx);
	free(skin->value);
	free(skin->pressure);
	profile_free(&skin->profile);
	layout_free(&skin->layout);
}

void
write_csv_header(struct skin *skin)
{
	if ( !skin || !skin->log )
		return;

	fprintf(skin->log, "time");
	for ( int p=0; p < skin->num_patches; p++ ) {
		const int patch_id = skin->layout.patch[p].patch_id;
		for ( int c=0; c < skin->layout.patch[p].num_cells; c++ ) {
			fprintf(skin->log, ",patch%d_cell%d", patch_id, skin->layout.patch[p].cell_id[c]);
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
		for ( int c=0; c < skin->layout.patch[p].num_cells; c++ ) {
			fprintf(skin->log, ",%g", skin_cell(skin, p, c));
		}
	}
	fprintf(skin->log, "\n");
	//fflush(f);
}

//--------------------------------------------------------------------

static skincell_t
scale_value(struct skin *skin, int patch, int cell, int32_t rawvalue)
{
	/* if ( !skin->profile.num_patches ) */
	/* 	return (skincell_t)rawvalue; */

	struct patch_profile *p = find_patch_profile(&skin->profile, patch);
	const int index = p->cell_idx[cell];
	if ( index < 0 )
		return rawvalue;
	skincell_t value = rawvalue - p->baseline[index];
	if ( p->c1[index] == 0.0 ) {
		return 0;
	} else {
		return p->c0[index] + value*(p->c1[index] + value*p->c2[index]);
	}
}

// Records a value to a specific cell
void
skin_cell_write(struct skin *skin, int patch, int cell, int32_t rawvalue)
{
	if ( skin->calibrating ) {
		const int i = skin->idx[patch][cell];
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

static void
write_code(struct skin *skin, const char *s)
{
	if ( write(skin->device_fd, s, strlen(s)) < 0 ) {
		WARNING("Cannot write to device");
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

	write_code(skin, STOP_CODE);
	sleep(1);
	/* write_code(skin, MUX_CODE); */
	/* sleep(1); */
	/* write_code(skin, CALIB_CODE); */
	/* sleep(1); */
	write_code(skin, START_CODE);
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
		pos += RECORD_SIZE;
		EVENT(skin, "parse", "%d.%d=%d", record.patch, record.cell, record.value);

		enum addr_check chk = address_check(skin, record.patch, record.cell);
		skin->addr_tally[chk]++;
		switch ( chk ) {
		case ADDR_VALID:
		default:
			break;
		case ADDR_PATCH_OOR:
			EVENT(skin, "patch out of range", "%d", record.patch);
			continue;
		case ADDR_PATCH_INV:
			EVENT(skin, "patch invalid", "%d", record.patch);
			continue;
		case ADDR_CELL_OOR:
			EVENT(skin, "cell out of range", "%d", record.cell);
			continue;
		case ADDR_CELL_INV:
			EVENT(skin, "cell invalid", "%d", record.cell);
			continue;
		}
		skin_cell_write(skin, record.patch, record.cell, record.value);

		// Append to log if last column for CSV row
		if ( skin->log && !skin->calibrating
			 && record.patch == skin->layout.max_patch_id
			 && record.cell == skin->layout.patch[skin->layout.patch_idx[record.patch]].max_cell_id ) {
			//pthread_mutex_lock(&skin->lock);
			write_csv_row(skin);
			//pthread_mutex_unlock(&skin->lock);
		}
	}
	write_code(skin, STOP_CODE);
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
		WARNING("Not reading from device (try skin_start)");
		return;
	}
	if ( skin->calibrating || skin->calib_sum || skin->calib_count ) {
		WARNING("Calibration already in progress");
		return;
	}
	pthread_mutex_lock(&skin->lock);
	skin->calibrating = 1;
	ALLOCN(skin->calib_sum, skin->total_cells);
	ALLOCN(skin->calib_count, skin->total_cells);
	profile_tare(&skin->profile);
	pthread_mutex_unlock(&skin->lock);
}

void
skin_calibrate_stop(struct skin *skin)
{
	DEBUGMSG("skin_calibrate_stop()");
	pthread_mutex_lock(&skin->lock);
	skin->calibrating = 0;
	const struct layout *lo = &skin->layout;
	for ( int p=0; p < lo->num_patches; p++ ) {
		const struct patch_layout *pl = &lo->patch[p];
		const int patch_id = pl->patch_id;
		struct patch_profile *pp = skin_get_patch_profile(skin, patch_id);
		int patch_zeros = 0;
		for ( int c=0; c < pl->num_cells; c++ ) {
			const int cell_id = pl->cell_id[c];
			skincell_t value = 0;
			const int i = skin->idx[patch_id][cell_id];
			if ( skin->calib_count[i] > 0 ) {
				value = skin->calib_sum[i]/skin->calib_count[i];
			} else {
				patch_zeros++;
				continue;
			}
			pp_baseline(pp, cell_id) = value;
			EVENT(skin, "baseline", "%d.%d=%d", patch_id, cell_id, pp_baseline(pp, cell_id));
		}
		if ( patch_zeros > 0 ) {
			WARNING("No calibration samples from %d cells of patch %d", patch_zeros, patch_id);
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

int
skin_save_profile(struct skin *skin, const char *csv)
{
	DEBUGMSG("skin_save_profile(\"%s\")", csv);
	if ( skin->calibrating )
		skin_calibrate_stop(skin);
	profile_write(&skin->profile, csv);
	return 1;
}

struct patch_layout *
skin_get_patch_layout(struct skin *skin, int patch)
{
	if ( !skin || patch < 0 || patch > skin->layout.max_patch_id ) {
		return NULL;
	}
	int index = skin->layout.patch_idx[patch];
	if ( index < 0 ) {
		return NULL;
	}
	return &skin->layout.patch[index];
}

struct patch_profile *
skin_get_patch_profile(struct skin *skin, int patch)
{
	if ( !skin || patch < 0 || patch > skin->profile.max_patch_id ) {
		return NULL;
	}
	int index = skin->profile.patch_idx[patch];
	if ( index < 0 ) {
		return NULL;
	}
	return skin->profile.patch[index];
}

skincell_t
skin_get_calibration(struct skin *skin, int patch, int cell)
{
	// Patch number from user starts at 1
	struct patch_profile *pp = skin_get_patch_profile(skin, patch);
	pthread_mutex_lock(&skin->lock);
	skincell_t ret = pp_baseline(pp, cell);
	pthread_mutex_unlock(&skin->lock);
	return ret;
}

/* int */
/* skin_get_state(struct skin *skin, skincell_t *dst) */
/* { */
/* 	pthread_mutex_lock(&skin->lock); */
/* 	memcpy(dst, skin->value, skin->total_cells*sizeof(*skin->value)); */
/* 	pthread_mutex_unlock(&skin->lock); */
/* 	return skin->num_patches; */
/* } */

int
skin_get_patch_state(struct skin *skin, int patch, skincell_t *dst)
{
	const struct patch_layout *pl = skin_get_patch_layout(skin, patch);
	pthread_mutex_lock(&skin->lock);
	for ( int c=0; c < pl->num_cells; c++ ) {
		dst[c] = skin_cell(skin, patch, pl->cell_id[c]);
	}
	pthread_mutex_unlock(&skin->lock);
	return 1;
}

int
skin_get_patch_pressure(struct skin *skin, int patch, struct skin_pressure *dst)
{
	const struct patch_layout *pl = skin_get_patch_layout(skin, patch);
	const int num_cells = pl->num_cells;
	skincell_t state[num_cells];
	struct skin_pressure p = {};

	skin_get_patch_state(skin, patch, state);
	skincell_t sum = 0;
	for ( int c=0; c < num_cells; c++ ) {
		if ( state[c] > SKIN_PRESSURE_MAX ) {
			state[c] = 1;
		} else if ( state[c] < 0 ) {
			state[c] = 0;
		} else {
			state[c] /= SKIN_PRESSURE_MAX;
		}
		sum += state[c];
	}

	if ( sum != 0 ) {
		for ( int c=0; c < num_cells; c++ ) {
			const double norm = state[c]/sum;
			p.x += norm*pl->x[c];
			p.y += norm*pl->y[c];
		}
		p.magnitude = sum*SKIN_PRESSURE_MAX;
		p.x = p.x < pl->xmin ? pl->xmin : (p.x > pl->xmax ? pl->xmax : p.x);
		p.y = p.y < pl->ymin ? pl->ymin : (p.y > pl->ymax ? pl->ymax : p.y);
	}

	struct skin_pressure *sp = &skin->pressure[skin->layout.patch_idx[patch]];
	exp_avg(&sp->magnitude, p.magnitude, skin->pressure_alpha);
	exp_avg(&sp->x, p.x, skin->pressure_alpha);
	exp_avg(&sp->y, p.y, skin->pressure_alpha);
	memcpy(dst, sp, sizeof(*sp));
	return 1;
}


skincell_t
skin_get_patch_mean(struct skin *skin, int patch)
{
	const struct patch_layout *pl = skin_get_patch_layout(skin, patch);
	skincell_t sum = 0;

	pthread_mutex_lock(&skin->lock);
	for ( int c=0; c < pl->num_cells; c++ ) {
		sum += skin_cell(skin, patch, pl->cell_id[c]);
	}
	pthread_mutex_unlock(&skin->lock);

	return sum/pl->num_cells;
}

enum addr_check
address_check(struct skin *skin, int patch, int cell)
{
	const struct layout *lo = &skin->layout;
	if ( patch < 0 || patch > lo->max_patch_id )
		return ADDR_PATCH_OOR;
	if ( lo->patch_idx[patch] < 0 )
		return ADDR_PATCH_INV;

	struct patch_layout *pl = &lo->patch[lo->patch_idx[patch]];
	if ( cell < 0 || cell > pl->max_cell_id )
		return ADDR_CELL_OOR;
	if ( skin->idx[patch][cell] < 0 )
		return ADDR_CELL_INV;
	return ADDR_VALID;
}

//EOF
