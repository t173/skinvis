// util.h -*-C-*-
//
// Utility macros

#ifndef UTIL_H_
#define UTIL_H_

#define FATAL(msg, ...) do {\
		fprintf(stderr, "FATAL: " msg "\n", ##__VA_ARGS__); \
		exit(EXIT_FAILURE); }	while (0)

#define WARNING(msg, ...) do {\
		fprintf(stderr, "WARNING: " msg "\n", ##__VA_ARGS__); }	while (0)

#ifdef DEBUG
#define DEBUGMSG(msg, ...) do {\
		fprintf(stderr, msg "\n", ##__VA_ARGS__); }	while (0)
#else
#define DEBUGMSG(msg, ...)
#endif

#define ALLOC(dst) do {\
		if ( ( (dst) = malloc(sizeof(*(dst)))) == NULL ) \
			FATAL("Cannot allocate %zd bytes", sizeof(*(dst))); } while (0)

#define REALLOC(dst, sz) do {\
		if ( ( (dst) = realloc((dst), (sz))) == NULL ) \
			FATAL("Cannot reallocate %zd bytes", (sz)); } while (0)

#define ALLOCN(dst, n) do {\
		if ( ( (dst) = calloc((n), sizeof(*(dst))) ) == NULL ) \
			FATAL("Cannot allocate %zd bytes", (n)*sizeof(*(dst))); } while (0)

#define MIN(a,b) ( (a) <  (b) ? (a) : (b) )
#define MAX(a,b) ( (a) >= (b) ? (a) : (b) )

#endif /// UTIL_H_
