/*
 *	cmdlintf.h	(Command Line Interface)
 *
 *	Copyright (c) 2004, Jiann-Ching Liu
 */

#ifndef __CMDLINE_INTF_H__
#define __CMDLINE_INTF_H__

#include <pthread.h>
#include <stdbool.h>
#include "logger.h"
#include "context.h"
//struct cmdlintf_pd_t;

#define COMMAND_LINE_INTERFACE_DEFAULT_CONTEXT_NAME "cmdline-interface"

struct cmdlintf_t {
    context_aware_data_t context;
//    struct cmdlintf_pd_t	*pd;
    int	(*add) (const char *cmd, const bool remote_command,
                int (*cmdfunc) (struct cmdlintf_t *, const char *),
                const char *doc, const int args,
                const int cmdtype);
    char * (*socket_name) (const char *filename);

    void (*set_prompt) (const char *str);
    int	(*set_timeout) (const int sec);
    int	(*start_server) (void (*on_success) (void));
    bool (*start_server_in_thread) (pthread_t *thread);
    int	(*start_client) (void);
    int	(*cli) (void);
    int	(*print) (const char *fmt, ...);
    void (*regcmd) (void);

    void (*set_login_callback) (void (*cbk) (struct cmdlintf_t *));
    int	(*execute) (char *cmd, const int cmdtype);
    void (*terminate) (void);
} __attribute__ ((packed)) /*__packed*/;

extern struct cmdlintf_t * init_cmdline_interface (const int bufsize);

#endif
