// profile.h -*-C-*-
//
// Calibration profile

#ifndef PROFILE_H_
#define PROFILE_H_

struct profile {
	const char *csvfile;
	int num_patches;
	struct patch_profile **patch;
	ssize_t alloc;  // allocation size of *patch

	int max_patch_id;
	int *patch_idx;  // map ID to index of *patch
};

struct patch_profile {
	int patch_id;
	int num_cells;

	int *cell_idx;  // map cell ID to index of below arrays
	int max_cell_id;
	
	// Baseline calibration
	int *baseline;

	// Dynamic range calibration
	double *c0;     // intercept
	double *c1;     // linear coefficient
	double *c2;     // quadratic coefficient
};

// Get calibration value from struct patch_profile pp
#define pp_baseline(pp, c)  ( (pp)->baseline[(pp)->cell_idx[(c)]] )
#define pp_c0(pp, c)        ( (pp)->c0[(pp)->cell_idx[(c)]] )
#define pp_c1(pp, c)        ( (pp)->c1[(pp)->cell_idx[(c)]] )
#define pp_c2(pp, c)        ( (pp)->c2[(pp)->cell_idx[(c)]] )

int profile_read(struct profile *p, const char *csvfile);
void profile_write(struct profile *p, const char *csvfile);

// Zero baseline calibration values
void profile_tare(struct profile *p);
void profile_set_baseline(struct profile *p, int patch, int cell, double value);

// Calibration profile
void profile_init(struct profile *p);
void profile_free(struct profile *p);

struct patch_profile *find_patch_profile(struct profile *p, int patch_id);

#endif // PROFILE_H_
