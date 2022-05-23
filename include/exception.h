//
// Created by saber on 3/18/22.
//

#ifndef TCP_PROXY_EXCEPTION_H
#define TCP_PROXY_EXCEPTION_H

#include <stdbool.h>
#include <stdarg.h>

extern bool try_catch (void f (va_list ap), ...);

#endif //TCP_PROXY_EXCEPTION_H
