// layout.c -*-C-*-
//
// Sensor cell layout

#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "layout.h"

#define COMMENT_CHAR '#'

static long
get_long(const char *tok)
{
	long ret;
	char *end;
	ret = strtol(tok, &end, 10);
	if ( *end != '\0' ) {
		FATAL("Integer expected but found: %s", tok);
	}
	return ret;
}

static double
get_double(const char *tok)
{
	double ret;
	char *end;
	ret = strtod(tok, &end);
	if ( *end != '\0' ) {
		FATAL("Float expected but found: %s", tok);
	}
	return ret;
}

int
layout_read(struct layout *lo, const char *csvfile)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t line_len;
	int patch_id = 0;
	int num_patches = 0;
	int current_patch = 0;
	int patches_remaining = 0;

	int num_cells = 0;
	int current_cell = 0;
	const char *const DELIM = ", ";
	struct patch_layout *current = NULL;

	if ( !(f = fopen(csvfile, "rt")) ) {
		FATAL("Cannot open file: %s\n%s", csvfile, strerror(errno));
	}

	lo->csvfile = csvfile;
	lo->max_patch_id = 0;

	enum {
		S_INIT=0,    // number of patches expected
		S_PATCH_ID,  // patch ID line expected
		S_CELL_ID    // cell ID line expected
	} state = S_INIT;

	int line_num;
	for ( line_num=1; (line_len = getline(&line, &len, f)) > 0; line_num++ ) {
		// Strip EOL and trailing spaces
		while ( line_len > 0 &&
				(line[line_len - 1] == '\n' ||
				 line[line_len - 1] == '\r' ||
				 line[line_len - 1] == ' ' ) ) {
			line[--line_len] = 0;
		}

		// Skip blank lines
		if ( line_len == 0 || line[0] == COMMENT_CHAR )
			continue;

		int col = 1;
		for ( char *tok = strtok(line, DELIM); tok; tok = strtok(NULL, DELIM), col++ ) {
			if ( state == S_INIT ) {
				if ( col == 1 ) {
					num_patches = patches_remaining = get_long(tok);
					layout_init(lo, num_patches);
					lo->csvfile = csvfile;
					current_patch = 0;
					state = S_PATCH_ID;
					break;
				} else {
					goto parse_error;
				}
			} else if ( state == S_PATCH_ID ) {
				if ( current_patch == num_patches ) {
					goto parse_error;
				}
				if ( col == 1 ) {
					patch_id = get_long(tok);
					lo->max_patch_id = MAX(patch_id, lo->max_patch_id);
				} else if ( col == 2 ) {
					num_cells = get_long(tok);
					current = &lo->patch[current_patch];
					patch_layout_init(current, patch_id, num_cells);
					current_cell = 0;
					state = S_CELL_ID;
					break;
				} else {
					goto parse_error;
				}
			} else if ( state == S_CELL_ID ) {
				if ( col == 1 ) {
					const int cell_id = get_long(tok);
					current->cell_id[current_cell] = cell_id;
					current->max_cell_id = MAX(cell_id, current->max_cell_id);
					lo->total_cells++;
				} else if ( col == 2 ) {
					const double x = get_double(tok);
					current->x[current_cell] = x;
					
					if ( current_cell == 0 ) {
						current->xmin = current->xmax = x;
					} else {
						current->xmin = MIN(x, current->xmin);
						current->xmax = MAX(x, current->xmax);
					}
				} else if ( col == 3 ) {
					const double y = get_double(tok);
					current->y[current_cell] = y;
					if ( current_cell == 0 ) {
						current->ymin = current->ymax = y;
					} else {
						current->ymin = MIN(y, current->ymin);
						current->ymax = MAX(y, current->ymax);
					}
					if ( ++current_cell == num_cells ) {
						current_patch++;
						state = S_PATCH_ID;
						break;
					}
				} else {
					goto parse_error;
				}
			} else { // invalid state
				goto parse_error;
			}
		}  // each token
	}  // each line

	// Build map of patch_id to index of lo->patch (<0 invalid)
	ALLOCN(lo->patch_idx, lo->max_patch_id + 1);
	for ( int i=0; i<=lo->max_patch_id; i++ ) {
		lo->patch_idx[i] = -1;
	}
	for ( int p=0, pidx=0; p<lo->num_patches; p++ ) {
		lo->patch_idx[lo->patch[p].patch_id] = pidx++;
	}

	free(line);
	fclose(f);
	return current_patch;

 parse_error:
	FATAL("Parse error of layout %s (line %d)", csvfile, line_num);
	return 0;
}


void
layout_init(struct layout *lo, int num_patches)
{
	memset(lo, 0, sizeof(*lo));
	lo->num_patches = num_patches;
	ALLOCN(lo->patch, num_patches);
}

void
layout_free(struct layout *lo)
{
	if ( lo ) {
		for ( int p=0; p < lo->num_patches; p++ ) {
			patch_layout_free(&lo->patch[p]);
		}
		free(lo->patch);
		free(lo->patch_idx);
	}
}

struct patch_layout *
get_patch_layout(struct layout *lo, int patch)
{
    const int index = lo->patch_idx[patch];
	if ( index < 0 ) {
		return NULL;
	} else {
		return &lo->patch[index];
	}
}

void
patch_layout_init(struct patch_layout *pl, int id, int num_cells)
{
	pl->patch_id = id;
	pl->num_cells = num_cells;
	ALLOCN(pl->cell_id, num_cells);
	ALLOCN(pl->x, num_cells);
	ALLOCN(pl->y, num_cells);
	pl->xmin = pl->ymin = 0;
	pl->xmax = pl->ymax = 0;
}

void
patch_layout_free(struct patch_layout *pl)
{
	if ( pl ) {
		free(pl->cell_id);
		free(pl->x);
		free(pl->y);
	}
}

//EOF
