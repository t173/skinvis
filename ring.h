// ring.h -*-C-*-
//
// A ring buffer

#ifndef RING_H_
#define RING_H_

#include <stdint.h>

typedef int32_t ring_data_t;

// Calibrated values are scaled by this much
#define CALIBRATED_SCALE 10000

typedef struct ring {
	int pos;
	int capacity;
	ring_data_t *buf;
	double expavg;
	double alpha;

	// Live baseline recalibration
	int calibrating;
	int64_t calib_batch;
	int calib_count;

	// Dynamic scaling parameters
	ring_data_t baseline;
	double c0, c1, c2;
} ring_t;

int ring_init(ring_t *ring, int capacity);
void ring_free(ring_t *ring);
void ring_write(ring_t *ring, ring_data_t value);
void ring_get_history(ring_t *ring, ring_data_t *dst);

// Start/stop calibration cycle
void ring_calibrate_start(ring_t *ring);
void ring_calibrate_stop(ring_t *ring);

// Sets alpha value for exponential averaging, in the range (0..1].
// Alpha determines the "fall off" of averaging; for alpha=1, only the
// most current value is relevant, and for alpha=0, there would be no
// change over time (so is disallowed).  Given invalid argument,
// returns zero.
int ring_set_alpha(ring_t *ring, double alpha);

#endif  // RING_H_
