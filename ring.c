// ring.c -*-C-*- 
//
// A ring buffer

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ring.h"
#include "util.h"

int
ring_init(ring_t *ring, int capacity) {
	memset(ring, 0, sizeof(*ring));
	if ( !(ring->buf = calloc(capacity, sizeof(*ring->buf))) ) {
		WARNING("Cannot allocate %d size ring", capacity);
		return 0;
	}
	ring->capacity = capacity;
	ring->alpha = 0.5;
	ring->calibration = 0;
	return 1;
}

void
ring_free(ring_t *ring) {
	free(ring->buf);
}

inline void
ring_write(ring_t *ring, ring_data_t value) {
	if ( ring->calibrating ) {
		ring->calib_batch += value;
		ring->calib_count++;
	} else {
		const ring_data_t cvalue = value - ring->calibration;
		ring->buf[ring->pos++] = cvalue;
		ring->pos %= ring->capacity;
		const double alpha = ring->alpha;
		ring->expavg = alpha*cvalue + (1 - alpha)*ring->expavg;
	}
}

inline void
ring_get_history(ring_t *ring, ring_data_t *dst) {
	memcpy(dst, ring->buf + ring->pos, (ring->capacity - ring->pos)*sizeof(*ring->buf));
	memcpy(dst + ring->capacity - ring->pos, ring->buf, ring->pos*sizeof(*ring->buf));
}

int
ring_set_alpha(ring_t *ring, double alpha) {
	if ( !ring || alpha <= 0 || alpha > 1 ) {
		return 0;
	}
	ring->alpha = alpha;
	return 1;
}

void
ring_calibrate_start(ring_t *ring) {
	ring->calibration = 0;
	ring->calib_batch = 0;
	ring->calib_count = 0;
	ring->calibrating = 1;
}

void
ring_calibrate_stop(ring_t *ring) {
	ring->calibration = ring->calib_batch/(ring->calib_count + 1);
	ring->calibrating = 0;
	for ( int i=0; i<ring->capacity; ++i ) {
		ring->buf[i] = ring->calibration;
	}
	ring->expavg = 0;
	ring->pos = 0;
}

void
ring_calibrate_disable(ring_t *ring) {
	ring->calibration = 0;
	ring->calibrating = 0;
}

//EOF
