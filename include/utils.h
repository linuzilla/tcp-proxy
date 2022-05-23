#ifndef __UTILS_H_
#define __UTILS_H_

#include <sys/types.h>
#include <time.h>
#include "logger.h"

void tvsub (struct timeval *tdiff, const struct timeval *t1, const struct timeval *t0);
double elapsed_time (const struct timeval *t1, const struct timeval *t0);
char * text2macaddr (const char *str, unsigned char *macaddr);
char * print_ether  (const unsigned char *mac);
char * print_mac    (const unsigned char *mac);
char * print_ip     (const unsigned char *ipstr);
char * timet_2_mysql_datetime (const time_t *ptr);
int    check_byte_ending (void);
void uptime (struct logger_t *logger, time_t start_time);


#endif
