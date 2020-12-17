// skintalk.c -*-C-*- 
//
// Skin serial communication interface

#ifndef __UTIL_H
#define __UTIL_H

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

#endif /// __UTIL_H
