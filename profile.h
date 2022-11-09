// profile.h -*-C-*-
//
// Calibration profile

#ifndef PROFILE_H_
#define PROFILE_H_

// Maximum number of patches supported
#define PROFILE_MAXPATCHES  8

// Maximum number of cells
#define PROFILE_CELLS       16

struct profile {
	const char *csvfile;
	struct patch_profile *patch[PROFILE_MAXPATCHES];
	int num_patches;
};

struct patch_profile {
	int id;  // patch ID

	// Baseline calibration
	int baseline[PROFILE_CELLS];

	// Dynamic range calibration
	double c0[PROFILE_CELLS];     // intercept
	double c1[PROFILE_CELLS];     // linear coefficient
	double c2[PROFILE_CELLS];     // quadratic coefficient
};

// Get calibration value from struct profile prof
#define profile_baseline(prof, p, c)  ( (prof).patch[(p)]->baseline[(c)] )
#define profile_c0(prof, p, c)        ( (prof).patch[(p)]->c0[(c)] )
#define profile_c1(prof, p, c)        ( (prof).patch[(p)]->c1[(c)] )
#define profile_c2(prof, p, c)        ( (prof).patch[(p)]->c2[(c)] )

// Reads calibration profile from CSV file into p
int profile_read(struct profile *p, const char *csvfile);

// Zero baseline calibration values
void profile_tare(struct profile *p);

// Calibration profile
void profile_init(struct profile *p);
void profile_free(struct profile *p);

#endif // PROFILE_H_
