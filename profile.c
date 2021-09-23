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

static inline struct patch_profile *profile_patch_new(int id) {
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
	int patch = 0;
	int cell = 0;
	const char *const DELIM = ",";
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
			case 0:  // patch ID
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

			case 1:  // cell ID
				cell = get_long(tok);
				if ( cell < 0 || cell >= PROFILE_CELLS ) {
					FATAL("line %d: Invalid cell number %d", line_num, cell);
				}
				break;

			case 2:  // baseline value
				current->baseline[cell] = get_long(tok);
				break;

			case 3: // c0
				current->c0[cell] = get_double(tok);
				break;

			case 4: // c1
				current->c1[cell] = get_double(tok);
				break;

			case 5: // c2
				current->c2[cell] = get_double(tok);
				break;

			default:
				FATAL("line %d: Too many columns, expected %d", line_num, num_cols);
				break;
			}
		}

		if ( line_num == 1 ) {
			num_cols = col;
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
