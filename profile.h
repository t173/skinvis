// profile.h -*-C-*-
//
// Calibration profile

#ifndef PROFILE_H_
#define PROFILE_H_

// Maximum number of patches supported
#define PROFILE_MAXPATCHES  8

// Expected number of cells and data points
#define PROFILE_CELLS       16
#define PROFILE_POINTS      1

typedef struct profile {
	const char *csvfile;
	struct patch_profile *patch[PROFILE_MAXPATCHES];
} profile_t;

struct patch_profile {
	int id;
	int baseline[PROFILE_CELLS][PROFILE_POINTS];
	int active[PROFILE_CELLS][PROFILE_POINTS];
	double force[PROFILE_CELLS][PROFILE_POINTS];
};

// Reads calibration profile from CSV file into p
int profile_read(profile_t *p, const char *csvfile);

// Calibration profile
void profile_init(profile_t *p);
void profile_free(profile_t *p);

#endif // PROFILE_H_
