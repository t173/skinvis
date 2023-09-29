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

#define SKIN_PRESSURE_MAX 1000

struct skin_pressure {
	double magnitude;
	double x, y;
};

enum addr_check {
	ADDR_VALID=0,   // valid address
	ADDR_PATCH_OOR, // patch ID is out of range
	ADDR_PATCH_INV, // patch ID not in layout
	ADDR_CELL_OOR,  // cell ID is out of range
	ADDR_CELL_INV,  // cell ID is not in layout

	ADDR_LENGTH,    // (reserved for max value)
};

// Management of a skin sensor device
struct skin {
	int num_patches;         // number of sensor patches
	int total_cells;         // total number of tactile sensors on device

	struct profile profile;  // dynamic range calibration profile
	struct layout layout;    // layout of cells in patches

	skincell_t *value;       // array of cell values
	int **idx;               // map of [patch][cell] IDs to value[] index

	double alpha;            // alpha for exponential averaging of values
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
	long total_bytes;   // odometer of bytes read from device
	long misalignments;
	long addr_tally[ADDR_LENGTH]; // tally of records by addr_check 
};

//int skin_init_octocan(struct skin *skin);
//int skin_init(struct skin *skin, const char *device, int patches, int cells);
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

int skin_read_profile(struct skin *skin, const char *csv);
int skin_save_profile(struct skin *skin, const char *csv);

struct patch_layout *skin_get_patch_layout(struct skin *skin, int patch);
struct patch_profile *skin_get_patch_profile(struct skin *skin, int patch);

skincell_t skin_get_calibration(struct skin *skin, int patch, int cell);

//int skin_get_state(struct skin *skin, skincell_t *dst);
//int skin_get_pressure(struct skin *skin, struct skin_pressure *dst);

int skin_get_patch_state(struct skin *skin, int patch, skincell_t *dst);
int skin_get_patch_pressure(struct skin *skin, int patch, struct skin_pressure *dst);
skincell_t skin_get_patch_mean(struct skin *skin, int patch);

enum addr_check address_check(struct skin *skin, int patch, int cell);


#endif // SKINTALK_H_
