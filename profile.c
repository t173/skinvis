// profile.c -*-C-*-
//
// Calibration profile

#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "profile.h"

static long get_long(const char *tok) {
	long ret;
	char *end;
	ret = strtol(tok, &end, 10);
	if ( *end != '\0' ) {
		FATAL("Integer expected but found: %s", tok);
	}
	return ret;
}

static double get_double(const char *tok) {
	double ret;
	char *end;
	ret = strtod(tok, &end);
	if ( *end != '\0' ) {
		FATAL("Float expected but found: %s", tok);
	}
	return ret;
}

static struct patch_profile *profile_patch_new(int id) {
	struct patch_profile *p;
	ALLOC(p);
	memset(p, 0, sizeof(*p));
	p->id = id;
	return p;
}

int profile_read(profile_t *p, const char *csvfile) {
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t line_len;
	int num_cols = 0;
	int num_points = 0;
	int patch = 0;
	int cell = 0;
	int point = 0;
	const char *const DELIM = ",";
	const int ADDR_COLS = 2;
	const int COLS_PER_POINT = 3;
	int patches_found = 0;
	struct patch_profile *current = NULL;

	if ( !(f = fopen(csvfile, "rt")) ) {
		FATAL("Cannot open file: %s\n%s", csvfile, strerror(errno));
	}

	profile_init(p);
	p->csvfile = csvfile;

	for ( int line_num=1; (line_len = getline(&line, &len, f)) > 0; line_num++ ) {
		// Strip EOL
		while ( line[line_len - 1] == '\n' || line[line_len - 1] == '\r' ) {
			line[--line_len] = 0;
		}

		int col = 0;
		for ( char *tok = strtok(line, DELIM); tok; tok = strtok(NULL, DELIM), col++ ) {
			if ( line_num == 1 ) {
				// TODO: verify column order by checking headers
				continue;
			}
			
			switch ( col ) {
			case 0:
				patch = get_long(tok);

				// Note patch IDs start at 1
				if ( patch <= 0 || patch > PROFILE_MAXPATCHES ) {
					FATAL("line %d: Invalid patch number %d (max supported %d)",
								line_num, patch, PROFILE_MAXPATCHES);
				}
				if ( !p->patch[patch - 1] ) {
					p->patch[patch - 1] = profile_patch_new(patch);
					patches_found++;
				}
				current = p->patch[patch - 1];
				break;

			case 1:
				cell = get_long(tok);
				if ( cell < 0 || cell >= PROFILE_CELLS ) {
					FATAL("line %d: Invalid cell number %d", line_num, cell);
				}
				break;

			default:
				point = (col - ADDR_COLS) / COLS_PER_POINT;
				if ( point >= PROFILE_POINTS ) {
					FATAL("line %d: Too many data points", line_num);
				}
				
				switch ( (col - ADDR_COLS) % COLS_PER_POINT ) {
					case 0:
						if ( current )
							current->baseline[cell][point] = get_long(tok);
						break;

					case 1:
						if ( current )
							current->active[cell][point] = get_long(tok);
						break;

					case 2:
						if ( current )
							current->force[cell][point] = get_double(tok);
						break;
				}
				break;
			}
			//printf("line %d col %d: %s\n", line_num, col, tok);
		}

		if ( line_num == 1 ) {
			num_cols = col;
			num_points = (num_cols - ADDR_COLS) / COLS_PER_POINT;
			if ( num_points != PROFILE_POINTS ) {
				FATAL("Profile must have %d data points, found %d", PROFILE_POINTS, num_points);
			}
		}
	}
	free(line);
	fclose(f);
	return patches_found;
}

void profile_init(profile_t *p) {
	p->csvfile = NULL;
	memset(p->patch, 0, sizeof(p->patch));
}

void profile_free(profile_t *p) {
	for ( int n=0; n < PROFILE_MAXPATCHES; n++ )
		free(p->patch[n]);
}

//EOF
