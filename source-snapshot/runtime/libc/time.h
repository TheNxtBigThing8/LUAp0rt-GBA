/* Freestanding stand-in for <time.h>. Only what mbc.c's MBC3 RTC needs.
   localtime() is really gmtime() -- the PS5 timezone is not consulted, which
   costs an RTC game nothing but a fixed offset. */
#ifndef LUACOREEMU_TIME_H
#define LUACOREEMU_TIME_H

#include <stddef.h>

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t     time(time_t *t);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);

#endif
