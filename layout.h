// layout.h -*-C-*-
//
// Sensor cell layout

#ifndef LAYOUT_H_
#define LAYOUT_H_

struct layout {
	const char *csvfile;
	int num_patches;
	int total_cells;
	struct patch_layout *patch;  // array of size num_patches

	int max_patch_id;
	int *patch_idx;  // map patch ID to index of *patch
};

struct patch_layout {
	int patch_id;   // patch ID
	int num_cells;  // number of cells this patch
  
	int *cell_id;   // cell IDs, size num_cells
	int max_cell_id;

	double *x, *y;  // x,y positions of cells
	double xmin, xmax;
	double ymin, ymax;
};

// Reads layout from CSV file
int layout_read(struct layout *lo, const char *csvfile);

/**
 * Layout file has the following (text) format:
 * 
 * <num_patches>
 * <patch_id>,<num_cells>
 * <cell_id>,<position x>,<position y>
 * ...
 * <patch_id>,<num_cells>
 * <cell_id>, <position x>,<position y>
 * ...
 **/


void layout_init(struct layout *lo, int num_patches);
void layout_free(struct layout *lo);

struct patch_layout *get_patch_layout(struct layout *lo, int patch);

void patch_layout_init(struct patch_layout *pl, int id, int num_cells);
void patch_layout_free(struct patch_layout *pl);

#endif // LAYOUT_H_
