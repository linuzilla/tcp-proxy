#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "utils.h"
#include "logger.h"

#define NUM_OF_BUCKET	20

static char	anybuffer[NUM_OF_BUCKET][32];
static int	idx = 0;


void tvsub (struct timeval *tdiff, const struct timeval *t1, const struct timeval *t0) {
    tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
    tdiff->tv_usec = t1->tv_usec - t0->tv_usec;

    if (tdiff->tv_usec < 0) {
        tdiff->tv_sec--, tdiff->tv_usec += 1000000;
    }
}

double elapsed_time (const struct timeval *t1, const struct timeval *t0) {
    struct timeval td;

    tvsub (&td, t1, t0);

    return td.tv_sec + (td.tv_usec / 1000000.);
}

void uptime (struct logger_t *logger, time_t start_time) {
    double duration = difftime (time (NULL), start_time);

    int day = duration / 86400;
    int seconds = (long) duration % 86400L;
    int hour = seconds / 3600;
    int min = (seconds % 3600) / 60;
    int sec = seconds % 60;

    if (day > 0) {
        logger->notice (__FILE__, __LINE__, "Uptime: %d day(s), %02d:%02d:%02d", day, hour, min, sec);
    } else {
        logger->notice (__FILE__, __LINE__, "Uptime: %02d:%02d:%02d", hour, min, sec);
    }
}

int check_byte_ending (void) {
    char		buffer[4] = { 1, 2, 3, 4 };
    void		*vptr = buffer;
    u_int32_t	*iptr = (u_int32_t *) buffer;
    int		i, bsum, lsum;

    vptr += 3;

    if (* ( (char *) vptr) != buffer[3]) {
        return 0;
    }

    for (i = bsum = lsum = 0;  i < 4; i++) {
        bsum = (bsum * 256) + buffer[i];
        lsum = (lsum * 256) + buffer[3 - i];
    }

    if (bsum == *iptr) {
        fprintf (stderr, "big endian");
        return 1;
    } else if (lsum == *iptr) {
        fprintf (stderr, "little endian");
        return -1;
    } else {
        return 0;
    }
}

char * print_ip (const unsigned char *ipstr) {
    idx = (idx + 1) % NUM_OF_BUCKET;

    sprintf (anybuffer[idx], "%u.%u.%u.%u",
             ipstr[0], ipstr[1], ipstr[2], ipstr[3]);

    return anybuffer[idx];
}

char * print_ether (const unsigned char *mac) {
    idx = (idx + 1) % NUM_OF_BUCKET;

    sprintf (anybuffer[idx], "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    return anybuffer[idx];
}

char * print_mac (const unsigned char *mac) {
    idx = (idx + 1) % NUM_OF_BUCKET;

    sprintf (anybuffer[idx], "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    return anybuffer[idx];
}

char * timet_2_mysql_datetime (const time_t *ptr) {
    struct tm	*tptr = localtime (ptr);

    idx = (idx + 1) % NUM_OF_BUCKET;

    sprintf (anybuffer[idx], "%04d-%02d-%02d %02d:%02d:%02d",
             tptr->tm_year + 1900,
             tptr->tm_mon + 1,
             tptr->tm_mday,
             tptr->tm_hour,
             tptr->tm_min,
             tptr->tm_sec);

    return anybuffer[idx];
}

char * text2macaddr (const char *str, unsigned char *macaddr) {
    char		*ptr = (char *) macaddr;
    int		j, k, dot, len;

    len = strlen (str);

    for (j = dot = 0; j < len; j++) if (str[j] == ':') {
            dot++;
        }

    if ( (dot != 0) && (dot != 5) ) {
        return NULL;
    }
    if ( (dot == 0) && (len != 12) ) {
        return NULL;
    }


    if (ptr == NULL) {
        idx = (idx + 1) % NUM_OF_BUCKET;
        ptr = anybuffer[idx];
    }

    if (dot == 0) {
        for (k = 0; k < 6; k++) {
            for (j = k * 2, dot = 0; j < k * 2 + 2; j++) {
                dot *= 16;

                if ( (str[j] >= '0') && (str[j] <= '9') ) {
                    dot += (str[j] - '0');
                } else if ( (str[j] >= 'A') && (str[j] <= 'F') ) {
                    dot += ( (str[j] - 'A') + 10);
                } else if ( (str[j] >= 'a') && (str[j] <= 'f') ) {
                    dot += ( (str[j] - 'a') + 10);
                } else {
                    return NULL;
                }
            }

            ptr[k++] = dot;
        }
    } else {
        for (j = k = dot = 0; (j < len) && (k < 6); j++) {
            if ( (str[j] >= '0') && (str[j] <= '9') ) {
                dot *= 16;
                dot += (str[j] - '0');
            } else if ( (str[j] >= 'A') && (str[j] <= 'F') ) {
                dot *= 16;
                dot += ( (str[j] - 'A') + 10);
            } else if ( (str[j] >= 'a') && (str[j] <= 'f') ) {
                dot *= 16;
                dot += ( (str[j] - 'a') + 10);
            } else if (str[j] == ':') {
                ptr[k++] = dot;
                dot = 0;
            } else {
                printf ("%c character\n", str[j]);
                return NULL;
            }
        }
        if (k != 5) {
            return NULL;
        }
        ptr[k] = dot;
    }
    // printf ("ok: ether= %s\n", print_ether (ptr));

    return ptr;
}
