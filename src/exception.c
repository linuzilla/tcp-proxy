//
// Created by saber on 3/18/22.
//

#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include "exception.h"

static jmp_buf jump_buffer_env;
static volatile bool caught;

static void exception_handler (int signo) {
    caught = true;
    longjmp (jump_buffer_env, 1);
}

bool try_catch (void f (va_list ap), ...) {
    va_list ap;

    va_start (ap, f);

    caught = false;
    signal (SIGSEGV, &exception_handler);

    setjmp (jump_buffer_env);

    if (! caught) {
        f (ap);
    }

    signal (SIGSEGV, SIG_DFL);
    return caught;
}