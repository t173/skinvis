// skintalk.h -*-C-*-
//
// Skin sensor prototype communication

#ifndef SKINTALK_H_
#define SKINTALK_H_

#include <pthread.h>
#include <stding.h>
//#include "ring.h"
#include "profile.h"

// Value of a single skin cell
typedef int32_t skincell_t;

// Management of a skin sensor device
struct skin {
	int num_patches;       // number of sensor patches
	int num_cells;         // number of tactile sensors per patch
	skincell_t *cell;      // array of cell values
	double alpha;          // alpha value for exponential averaging
	
	const char *device;    // communication device to use
//	ring_t *rings;         // ring buffers storing sensor values 
//	int history;           // size of ring buffers
	const char *log;       // log record stream to filename
	const char *debuglog;  // log debugging data to filename

	// Reader thread management
	pthread_t reader;
	pthread_mutex_t lock;
	int shutdown;
	int calibrating;       // whether currently baseline calibrating

	struct profile profile;     // dynamic range calibration profile

	// Performance statistics
	long long total_bytes;   // odometer of bytes read from device
	long long total_records; // number of records accepted
};

int skin_init(struct skin *skin, int patches, int cells, const char *device, int history);
void skin_free(struct skin *skin);
int skin_start(struct skin *skin);
void skin_wait(struct skin *skin);
void skin_stop(struct skin *skin);

// Informs the struct skin to log the raw stream to a file.  Note that this
// must be set before calling skin_start()
void skin_log_stream(struct skin *skin, const char *filename);

// Log debugging information to file
void skin_debuglog_stream(struct skin *skin, const char *filename);

//void skin_get_history(struct skin *skin, ring_data_t *dst, int patch, int cell);
//double skin_get_expavg(struct skin *skin, int patch, int cell);
int skin_set_alpha(struct skin *skin, double alpha);

void skin_calibrate_start(struct skin *skin);
void skin_calibrate_stop(struct skin *skin);

// Loads calibration profile from CSV file
void skin_read_profile(struct skin *skin, const char *csv);

ring_data_t skin_get_calibration(struct skin *skin, int patch, int cell);

#endif // SKINTALK_H_
