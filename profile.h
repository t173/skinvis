// profile.h -*-C-*-
//
// Calibration profile

#ifndef PROFILE_H_
#define PROFILE_H_

// Maximum number of patches supported
#define PROFILE_MAXPATCHES  8

// Expected number of cells and data points
#define PROFILE_CELLS       16
#define PROFILE_COEFS       3

typedef struct profile {
	const char *csvfile;
	struct patch_profile *patch[PROFILE_MAXPATCHES];
} profile_t;

struct patch_profile {
	int id;  // patch ID
	int baseline[PROFILE_CELLS];
	double c0[PROFILE_CELLS];  // intercept
	double c1[PROFILE_CELLS];  // linear coefficient
	double c2[PROFILE_CELLS];  // quadratic coefficient
};

// Reads calibration profile from CSV file into p
int profile_read(profile_t *p, const char *csvfile);

// Calibration profile
void profile_init(profile_t *p);
void profile_free(profile_t *p);

#endif // PROFILE_H_
