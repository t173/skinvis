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

#define PARSE_ERR(msg, ...) do { FATAL("line %d: " msg, line_num, ##__VA_ARGS__); } while (0)

#define pp_cell_param(p, c, param) ( (p)->param[(p)->cell_idx[c]] )

// Initial maximum patch or cell ID (doubles as needed)
#define INITIAL_MAX_PATCH 4
#define INITIAL_MAX_CELL 4
#define INITIAL_PATCH_ALLOC 4

static void profile_init(struct profile *p);
static void profile_enlarge(struct profile *p, int new_max);
static struct patch_profile *profile_get_patch(struct profile *p, int patch_id);
static struct patch_profile *patch_profile_new(int patch_id, int max_cell);
static struct patch_profile *patch_profile_enlarge(struct patch_profile *p, int new_max);

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
profile_read(struct profile *p, const char *csvfile)
{
	FILE *f;
	char *line = NULL;
	size_t len = 0;
	ssize_t line_len;
	int patch_id = 0;
	int cell_id = 0;
	const char *const DELIM = ",";
	struct patch_profile *current = NULL;

	static const char *headers[] = { "patch", "cell", "baseline", "c0", "c1", "c2", NULL };

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
				if ( !headers[col] || !strcmp(tok, headers[col]) ) {
					PARSE_ERR("Column header mismatch, column %d: %s", col, tok);
				}
				continue;
			}

			switch ( col ) {
			case 0:  // patch ID
				patch_id = get_long(tok);
				if ( patch_id < 0 ) {
					PARSE_ERR("Invalid patch number %d", patch_id);
				}
				current = profile_get_patch(p, patch_id);
				break;

			case 1:  // cell ID
				cell_id = get_long(tok);
				if ( cell_id < 0 ) {
					PARSE_ERR("Invalid cell number %d", cell_id);
				}
				
				// Enlarge patch profile if needed
				if ( cell_id >= current->max_cell_id ) {
					patch_profile_enlarge(current, MAX(cell_id, 2*current->max_cell_id));
				}

				// Allocate cell index
				if ( current->cell_idx[cell_id] < 0 ) {
					current->cell_idx[cell_id] = current->num_cells++;
				}
				break;

			case 2:  // baseline value
				pp_cell_param(current, cell_id, baseline) = get_long(tok);
				break;

			case 3: // c0
				pp_cell_param(current, cell_id, c0) = get_double(tok);
				break;

			case 4: // c1
				pp_cell_param(current, cell_id, c1) = get_double(tok);
				break;

			case 5: // c2
				pp_cell_param(current, cell_id, c2) = get_double(tok);
				break;

			default:
				PARSE_ERR("Too many columns");
				break;
			}
		}  // column
	}  // line
	free(line);
	fclose(f);
	return p->num_patches;
}

void
profile_tare(struct profile *p)
{
	for ( int i=0; i<PROFILE_MAXPATCHES; i++ ) {
		if ( p->patch[i] ) {
			memset(p->patch[i]->baseline, 0, sizeof(p->patch[i]->baseline));
		}
	}
}

static void
profile_init(struct profile *p)
{
	if ( !p ) return;
	memset(p, 0, sizeof(p));
	p->csvfile = NULL;
	p->num_patches = 0;

	// patch is array of pointers to struct patch_profile
	ALLOCN(p->patch, INITIAL_PATCH_ALLOC);
	p->alloc = INITIAL_PATCH_ALLOC;

	// patch_idx maps (user) patch ID to index of patch[], or -1 if not used
	const int max_patch_id = INITIAL_MAX_PATCH;
	p->max_patch_id = max_patch_id;
	ALLOCN(p->patch_idx, max_patch_id);
	for ( int i=0; i < max_patch_id; i++ ) {
		p->patch_idx[i] = -1;
	}
}

static void
profile_enlarge(struct profile *p, int new_max)
{
	if ( !p || new_max <= p->max_patch_id )
		return;
	REALLOC(p->patch, new_max);
	REALLOC(p->patch_idx, new_max);
	for ( int i=p->max_patch_id; i < new_max; i++ ) {
		p->patch[i] = NULL;
		p->patch_idx[i] = -1;
	}
	p->max_patch_id = new_max;
}


static struct patch_profile *
patch_profile_new(int patch_id)
{
	struct patch_profile *p;
	const int max_cell = INITIAL_MAX_CELL;
	ALLOC(p);
	memset(p, 0, sizeof(*p));
	p->num_cells = 0;
	p->patch_id = id;
	p->max_cell_id = max_cell;
	ALLOCN(p->cell_idx, max_cell);
	ALLOCN(p->baseline, max_cell);
	ALLOCN(p->c0, max_cell);
	ALLOCN(p->c1, max_cell);
	ALLOCN(p->c2, max_cell);
	return p;
}

// Gets (or creates) patch_profile within a profile with the given patch_id
static struct patch_profile *
profile_get_patch(struct profile *p, int patch_id)
{
	struct patch_profile *ret;
	
	// Enable larger patch IDs if needed
	if ( patch_id >= p->max_patch_id ) {
		profile_enlarge(p, MAX(patch_id, 2*p->max_patch_id));
	}

	// Enlarge patch allocation if needed
	if ( p->alloc >= p->num_patches ) {
		p->alloc *= 2;
		REALLOC(p->patch, p->alloc);
	}

	if ( p->patch_idx[patch_id] < 0 ) {
		p->patch_idx[patch_id] = p->num_patches++;
		p->patch[p->patch_idx[patch_id]] = patch_profile_new(patch_id);
	}
	return p->patch[p->patch_idx[patch_id]];
}

static struct patch_profile *
patch_profile_enlarge(struct patch_profile *p, int new_max)
{
	if ( !p ) return NULL;
	if ( new_max <= p->max_cell_id ) return p;
	
	REALLOC(p->cell_idx, new_max_cell);
	REALLOC(p->baseline, new_max_cell);
	REALLOC(p->c0, new_max_cell);
	REALLOC(p->c1, new_max_cell);
	REALLOC(p->c2, new_max_cell);
	
	for ( int i=p->max_cell_id; i < new_max; i++ ) {
		p->cell_idx[i] = -1;
	}
	p->max_cell_id = new_max_cell;
	return p;
}

static void
patch_profile_free(struct patch_profile *p)
{
	if ( p ) {
		free(p->patch[n].cell_idx);
		free(p->patch[n].baseline);
		free(p->patch[n].c0);
		free(p->patch[n].c1);
		free(p->patch[n].c2);
		free(p->patch[n]);
	}
}

void
profile_free(struct profile *p)
{
	for ( int n=0; n < p->num_patches; n++ ) {
		patch_profile_free(p->patch[n]);
		free(p->patch[n]);
	}
	free(p->patch_idx);
	free(p->patch);
}

void
profile_set_baseline(struct profile *p, int patch, int cell, double value)
{
	if ( !p->patch[patch] ) {
		p->patch[patch] = patch_profile_new(patch);
	}
	p->patch[patch]->baseline[cell] = value;
}

//EOF
