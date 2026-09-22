/* Freestanding stand-in for <string.h>. Implemented in src/shim.c. */
#ifndef LUACOREEMU_STRING_H
#define LUACOREEMU_STRING_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strdup(const char *s);
char  *strtok(char *s, const char *delim);
char  *strerror(int e);

#endif

void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);

int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strdup(const char *s);
char  *strtok(char *s, const char *delim);
char  *strerror(int e);

#endif
