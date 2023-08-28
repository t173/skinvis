// skintalk.h -*-C-*-
//
// Skin sensor prototype communication

#ifndef SKINTALK_H_
#define SKINTALK_H_

#include <pthread.h>
#include <stdint.h>
#include "profile.h"
#include "layout.h"

// Value of a single skin cell
typedef double skincell_t;

#define SKIN_PRESSURE_MAX 100

struct skin_pressure {
	double magnitude;
	double x, y;
};

// Management of a skin sensor device
struct skin {
	int num_patches;         // number of sensor patches
	int num_cells;           // max number of tactile sensors per patch
	skincell_t *value;       // array of cell values
	double alpha;            // alpha value for exponential averaging

	struct profile profile;  // dynamic range calibration profile
	struct layout layout;    // layout of cells in patches

	double pressure_alpha;   // alpha for smoothing presure calculations
	struct skin_pressure *pressure;

	const char *device;      // communication device to use
	int device_fd;           // file descriptor for device

	FILE *log;               // log record stream to filename
	FILE *debuglog;          // log debugging info to filename

	// Reader thread management
	pthread_t reader;        // serial reader and processing thread
	pthread_mutex_t lock;    // mutex lock to protect against reader
	int shutdown;            // whether trying to shutdown device

	// Baseline calibration
	int calibrating;         // whether performing baseline calibration
	long long *calib_sum;    // batch sum while calibrating
	int *calib_count;        // batch count while calibrating

	// Performance statistics
	long long total_bytes;     // odometer of bytes read from device
	long long total_records;   // number of records correctly parsed
	long long dropped_records; // invalid records dropped
	long long misalignments;   // misalignment advances
};

// Get value of cell c from patch p of struct skin *s
#define skin_cell(s, p, c) ((s)->value[(p)*((s)->num_cells) + (c)])

int skin_init_octocan(struct skin *skin);
int skin_init(struct skin *skin, const char *device, int patches, int cells);
int skin_from_layout(struct skin *skin, const char *device, const char *lofile);
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
int skin_set_pressure_alpha(struct skin *skin, double alpha);

// Baseline calibration on live system
void skin_calibrate_start(struct skin *skin);
void skin_calibrate_stop(struct skin *skin);

// Loads calibration profile from CSV file
int skin_read_profile(struct skin *skin, const char *csv);

skincell_t skin_get_calibration(struct skin *skin, int patch, int cell);

// Writes the latest state of all cells to dst. Values are (patch,
// cell) in row major order.
int skin_get_state(struct skin *skin, skincell_t *dst);
int skin_get_patch_state(struct skin *skin, int patch, skincell_t *dst);

//int skin_get_pressure(struct skin *skin, struct skin_pressure *dst);
int skin_get_patch_pressure(struct skin *skin, int patch, struct skin_pressure *dst);

#endif // SKINTALK_H_
