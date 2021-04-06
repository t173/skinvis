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

#endif /// UTIL_H_
