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

	enum {
		S_INIT=0,    // number of patches expected
		S_PATCH_ID,  // patch ID line expected
		S_CELL_ID    // cell ID line expected
	} state = S_INIT;

	int line_num;
	for ( line_num=1; (line_len = getline(&line, &len, f)) > 0; line_num++ ) {
		// Strip EOL
		while ( line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r') ) {
			line[--line_len] = 0;
		}

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
					lo->patch[current_patch].cell_id[current_cell] = get_long(tok);
				} else if ( col == 2 ) {
					lo->patch[current_patch].x[current_cell] = get_double(tok);
				} else if ( col == 3 ) {
					lo->patch[current_patch].y[current_cell] = get_double(tok);
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
	lo->csvfile = NULL;
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
