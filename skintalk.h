// skintalk.h -*-C-*-
//
// Skin sensor prototype communication

#ifndef SKINTALK_H_
#define SKINTALK_H_

#include <pthread.h>
#include "ring.h"

// Management of a skin sensor device
typedef struct skin {
	int num_patches;       // number of sensor patches
	int num_cells;         // number of tactile sensors per patch
	const char *device;    // communication device to use
	ring_t *rings;         // ring buffers storing sensor values 
	int history;           // size of ring buffers
	const char *log;       // log record stream to filename
	const char *debuglog;  // log debugging data to filename

	// Reader thread management
	pthread_t reader;
	pthread_mutex_t lock;
	int shutdown;
	int calibrating;       // whether currently calibrating

	// Performance statistics
	long long total_bytes;   // odometer of bytes read from device
	long long total_records; // number of records accepted
} skin_t;

int skin_init(skin_t *skin, int patches, int cells, const char *device, int history);
void skin_free(skin_t *skin);
int skin_start(skin_t *skin);
void skin_wait(skin_t *skin);
void skin_stop(skin_t *skin);

// Informs the skin_t to log the raw stream to a file.  Note that this
// must be set before calling skin_start()
void skin_log_stream(skin_t *skin, const char *filename);

// Log debugging information to file
void skin_debuglog_stream(skin_t *skin, const char *filename);

void skin_get_history(skin_t *skin, ring_data_t *dst, int patch, int cell);
double skin_get_expavg(skin_t *skin, int patch, int cell);
int skin_set_alpha(skin_t *skin, double alpha);

void skin_calibrate_start(skin_t *skin);
void skin_calibrate_stop(skin_t *skin);

ring_data_t skin_get_calibration(skin_t *skin, int patch, int cell);

#endif // SKINTALK_H_
