// profile.h -*-C-*-
//
// Calibration profile

#ifndef PROFILE_H_
#define PROFILE_H_

struct profile {
	const char *csvfile;
	int num_patches;
	struct patch_profile **patch;
	size_t alloc;  // allocation size of *patch

	int max_patch_id;
	int *patch_idx;  // map match ID to index of *patch
};

struct patch_profile {
	int patch_id;

	int num_cells;
	int max_cell_id;
	int *cell_idx;  // map cell ID to index of below arrays
	
	// Baseline calibration
	int *baseline;

	// Dynamic range calibration
	double *c0;     // intercept
	double *c1;     // linear coefficient
	double *c2;     // quadratic coefficient
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
void profile_set_baseline(struct profile *p, int patch, int cell, double value);

// Calibration profile
//void profile_init(struct profile *p);
void profile_free(struct profile *p);

#endif // PROFILE_H_
