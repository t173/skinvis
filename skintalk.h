// skintalk.h -*-C-*-
//
// Skin sensor prototype communication

#ifndef SKINTALK_H_
#define SKINTALK_H_

#include <pthread.h>
#include <stdint.h>
#include "profile.h"

// Value of a single skin cell
typedef int32_t cell_t;

// Management of a skin sensor device
struct skin {
	int num_patches;         // number of sensor patches
	int num_cells;           // number of tactile sensors per patch
	cell_t *value;           // array of cell values

	struct profile profile;  // dynamic range calibration profile

	double alpha;            // alpha value for exponential averaging
	const char *device;      // communication device to use
	const char *log;         // log record stream to filename
	const char *debuglog;    // log debugging data to filename

	// Reader thread management
	pthread_t reader;        // serial reader and processing thread
	pthread_mutex_t lock;    // mutex lock to protect against reader
	int shutdown;            // whether trying to shutdown device
	int calibrating;         // whether performing baseline calibration
	long long *calib_sum;    // batch sum while calibrating
	int *calib_count;        // batch count while calibrating

	// Performance statistics
	long long total_bytes;   // odometer of bytes read from device
	long long total_records; // number of records correctly parsed
};

// Get value of cell c from patch p of struct skin *s
#define skin_cell(s, p, c) ((s)->value[(p)*((s)->num_cells) + (c)])

int skin_init(struct skin *skin, int patches, int cells, const char *device);
void skin_free(struct skin *skin);
int skin_start(struct skin *skin);
void skin_wait(struct skin *skin);
void skin_stop(struct skin *skin);

// Informs the struct skin to log the raw stream to a file.  Note that this
// must be set before calling skin_start()
void skin_log_stream(struct skin *skin, const char *filename);

// Log debugging information to file
void skin_debuglog_stream(struct skin *skin, const char *filename);

int skin_set_alpha(struct skin *skin, double alpha);

// Baseline calibration on live system
void skin_calibrate_start(struct skin *skin);
void skin_calibrate_stop(struct skin *skin);

// Loads calibration profile from CSV file
void skin_read_profile(struct skin *skin, const char *csv);

cell_t skin_get_calibration(struct skin *skin, int patch, int cell);

#endif // SKINTALK_H_
