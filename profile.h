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

struct profile {
	const char *csvfile;
	struct patch_profile *patch[PROFILE_MAXPATCHES];
};

struct patch_profile {
	int id;  // patch ID
	int baseline[PROFILE_CELLS];
	double c0[PROFILE_CELLS];  // intercept
	double c1[PROFILE_CELLS];  // linear coefficient
	double c2[PROFILE_CELLS];  // quadratic coefficient
};

// Get calibration value from struct profile *prof
#define profile_baseline(prof, p, c)  ( (prof)->patch[p]->baseline[c] )
#define profile_c0(prof, p, c)        ( (prof)->patch[p]->c0[c] )
#define profile_c1(prof, p, c)        ( (prof)->patch[p]->c1[c] )
#define profile_c2(prof, p, c)        ( (prof)->patch[p]->c2[c] )

// Reads calibration profile from CSV file into p
int profile_read(struct profile *p, const char *csvfile);

// Calibration profile
void profile_init(struct profile *p);
void profile_free(struct profile *p);

#endif // PROFILE_H_
