/**
 * vim: set nolist tabstop=8 softtabstop=0 noexpandtab shiftwidth=8 colorcolumn=0 :
 *
 * This function was lifted from Vinyl Cache (lib/libvinyl/vtim.c) and modified
 * ever so little to fit ESPHome.
 *
 * The formatting of this function intentionally does not conform to ESPHome
 * standards. It is kept original so as to make future rebasing, should it
 * become necessary, easier.
 */

/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Semi-trivial functions to handle HTTP header timestamps according to
 * RFC 2616 section 3.3.
 *
 * We must parse four different formats:
 *       000000000011111111112222222222
 *       012345678901234567890123456789
 *       ------------------------------
 *	"Sun, 06 Nov 1994 08:49:37 GMT"		RFC822 & RFC1123
 *	"Sunday, 06-Nov-94 08:49:37 GMT"	RFC850
 *	"Sun Nov  6 08:49:37 1994"		ANSI-C asctime()
 *	"1994-11-06T08:49:37"			ISO 8601
 *
 * And always output the RFC1123 format.
 *
 * So why are these functions hand-built ?
 *
 * Because the people behind POSIX were short-sighted morons who didn't think
 * anybody would ever need to deal with timestamps in multiple different
 * timezones at the same time -- for that matter, convert timestamps to
 * broken down UTC/GMT time.
 *
 * We could, and used to, get by by smashing our TZ variable to "UTC" but
 * that ruins the LOCALE for VMODs.
 *
 */

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "vtim.h"

static const char *TAG = "httpstime.vtim_parse";

/* relax vtim parsing */
unsigned VTIM_postel = 0;

static const char * const weekday_name[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char * const more_weekday[] = {
	"day", "day", "sday", "nesday", "rsday", "day", "urday"
};

static const char * const month_name[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const int days_in_month[] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const int days_before_month[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

#define FAIL()	\
	do { return (0); } while (0)

#ifndef assert
#define assert(expr)							\
	do {								\
		if (!(expr)) {						\
			ESP_LOGE(TAG, "assertion failed: %s", #expr);	\
			FAIL();						\
		}							\
	} while(0)
#endif

#define DIGIT(mult, fld)					\
	do {							\
		if (*p < '0' || *p > '9')			\
			FAIL();					\
		fld += (*p - '0') * mult;			\
		p++;						\
	} while(0)

#define MUSTBE(chr)						\
	do {							\
		if (*p != chr)					\
			FAIL();					\
		p++;						\
	} while(0)

#define WEEKDAY()						\
	do {							\
		int i;						\
		for (i = 0; i < 7; i++) {			\
			if (!memcmp(p, weekday_name[i], 3)) {	\
				weekday = i;			\
				break;				\
			}					\
		}						\
		if (i == 7)					\
			FAIL();					\
		p += 3;						\
	} while(0)


#define MONTH()							\
	do {							\
		int i;						\
		for (i = 0; i < 12; i++) {			\
			if (!memcmp(p, month_name[i], 3)) {	\
				month = i + 1;			\
				break;				\
			}					\
		}						\
		if (i == 12)					\
			FAIL();					\
		p += 3;						\
	} while(0)

#define TIMESTAMP()						\
	do {							\
		DIGIT(10, hour);				\
		DIGIT(1, hour);					\
		MUSTBE(':');					\
		DIGIT(10, min);					\
		DIGIT(1, min);					\
		MUSTBE(':');					\
		DIGIT(10, sec);					\
		DIGIT(1, sec);					\
	} while(0)

vtim_real
VTIM_parse(const char *p)
{
	vtim_real t;
	int month = 0, year = 0, weekday = -1, mday = 0;
	int hour = 0, min = 0, sec = 0;
	int d, leap;

	if (p == NULL || *p == '\0')
		FAIL();

	while (*p == ' ')
		p++;

	if (*p >= '0' && *p <= '9') {
		/* ISO8601 -- "1994-11-06T08:49:37" */
		DIGIT(1000, year);
		DIGIT(100, year);
		DIGIT(10, year);
		DIGIT(1, year);
		MUSTBE('-');
		DIGIT(10, month);
		DIGIT(1, month);
		MUSTBE('-');
		DIGIT(10, mday);
		DIGIT(1, mday);
		MUSTBE('T');
		TIMESTAMP();
	} else {
		WEEKDAY();
		assert(weekday >= 0 && weekday <= 6);
		if (*p == ',') {
			/* RFC822 & RFC1123 - "Sun, 06 Nov 1994 08:49:37 GMT" */
			p++;
			MUSTBE(' ');
			if (VTIM_postel && *p && p[1] == ' ')
				DIGIT(1, mday);
			else {
				DIGIT(10, mday);
				DIGIT(1, mday);
			}
			MUSTBE(' ');
			MONTH();
			MUSTBE(' ');
			DIGIT(1000, year);
			DIGIT(100, year);
			DIGIT(10, year);
			DIGIT(1, year);
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			MUSTBE('G');
			MUSTBE('M');
			MUSTBE('T');
		} else if (*p == ' ') {
			/* ANSI-C asctime() -- "Sun Nov  6 08:49:37 1994" */
			p++;
			MONTH();
			MUSTBE(' ');
			if (*p != ' ')
				DIGIT(10, mday);
			else
				p++;
			DIGIT(1, mday);
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			DIGIT(1000, year);
			DIGIT(100, year);
			DIGIT(10, year);
			DIGIT(1, year);
		} else if (!memcmp(p, more_weekday[weekday],
		    strlen(more_weekday[weekday]))) {
			/* RFC850 -- "Sunday, 06-Nov-94 08:49:37 GMT" */
			p += strlen(more_weekday[weekday]);
			MUSTBE(',');
			MUSTBE(' ');
			DIGIT(10, mday);
			DIGIT(1, mday);
			MUSTBE('-');
			MONTH();
			MUSTBE('-');
			DIGIT(10, year);
			DIGIT(1, year);
			year += 1900;
			if (year < 1969)
				year += 100;
			MUSTBE(' ');
			TIMESTAMP();
			MUSTBE(' ');
			MUSTBE('G');
			MUSTBE('M');
			MUSTBE('T');
		} else
			FAIL();
	}

	while (*p == ' ')
		p++;

	if (*p != '\0')
		FAIL();

	if (sec < 0 || sec > 60)	/* Leapseconds! */
		FAIL();
	if (min < 0 || min > 59)
		FAIL();
	if (hour < 0 || hour > 23)
		FAIL();
	if (month < 1 || month > 12)
		FAIL();
	if (mday < 1 || mday > days_in_month[month - 1])
		FAIL();
	if (year < 1899)
		FAIL();

	leap =
	    ((year) % 4) == 0 && (((year) % 100) != 0 || ((year) % 400) == 0);

	if (month == 2 && mday > 28 && !leap)
		FAIL();

	if (sec == 60)			/* Ignore Leapseconds */
		sec--;

	t = ((hour * 60.) + min) * 60. + sec;

	d = (mday - 1) + days_before_month[month - 1];

	if (month > 2 && leap)
		d++;

	d += (year % 100) * 365;	/* There are 365 days in a year */

	if ((year % 100) > 0)		/* And a leap day every four years */
		d += (((year % 100) - 1) / 4);

	d += ((year / 100) - 20) *	/* Days relative to y2000 */
	    (100 * 365 + 24);		/* 24 leapdays per year in a century */

	d += ((year - 1) / 400) - 4;	/* And one more every 400 years */

	/*
	 * Now check weekday, if we have one.
	 * 6 is because 2000-01-01 was a saturday.
	 * 10000 is to make sure the modulus argument is always positive
	 */
	if (weekday != -1 && (d + 6 + 7 * 10000) % 7 != weekday)
		FAIL();

	t += d * 86400.;

	t += 10957. * 86400.;		/* 10957 days frm UNIX epoch to y2000 */

	return (t);
}
