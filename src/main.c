
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <pwd.h>
#include "global_vars.h"
#include "context.h"
#include "min_timer.h"
#include "sysconf.h"
#include "proxying.h"
#include "db_service.h"
#include "logger.h"
#include "syslog_appender.h"
#include "auto_blacklist.h"
#include "cmdlintf.h"
#include "commands.h"
#include "packet_analyzer.h"

static struct application_context_t *application_context = NULL;

static struct system_config_t *conf = NULL;
static struct auto_blacklist_service_t *blacklistService = NULL;
static struct logger_t *logger = &excalibur_common_logger;
static struct database_service_t *db_svc = NULL;
struct proxying_service_t *proxyingService = NULL;

static pthread_t main_thread = 0L;
static pthread_t command_thread = 0L;

static pthread_t proxy_thread;
static struct minute_timer_t *minute_timer;

static double expiring_timeout = 0.;

static enum log_priority_t current_log_priority = log_notice;

static int last_min = -1;

static time_t app_boot_time;

struct global_vars_t global_vars = {
    .main_thread = &main_thread,
    .command_thread = &command_thread,
    .app_boot_time = &app_boot_time,
};

static void cron (const struct timeval *tv, const struct tm *tm) {
    if (tm->tm_min != last_min) {
        last_min = tm->tm_min;
        db_svc->close_idle (tv, tm);
        proxyingService->clean_idle_connection (tv, expiring_timeout);
        blacklistService->expiring();
    }
}

static void run_timer (void) {
    minute_timer->start (cron);
}

static void interrupt (int signal_no) {
    system_config->terminate();

    pthread_t thread = pthread_self();

    if (pthread_equal (thread, main_thread)) {
        pthread_kill (proxy_thread, signal_no);
        if (blacklistService != NULL) {
            blacklistService->terminate();
        }
        exit (0);
    } else if (pthread_equal (thread, proxy_thread)) {
    }
}

static void log_level_change (int signal_no) {
    pthread_t thread = pthread_self();

    if (pthread_equal (thread, main_thread)) {
        switch (signal_no) {
        case SIGUSR1:
            if (current_log_priority > log_warning) {
                current_log_priority--;
            }
            break;

        case SIGUSR2:
            if (current_log_priority < log_trace) {
                current_log_priority++;
            }
            break;
        }
        logger->setPriority (current_log_priority);
        logger->warning (__FILE__, __LINE__, "Logging priority: %s", logger_get_priority_name (current_log_priority));
    }
}

static void usage (char **argv) {
    fprintf (stderr, "Usage: %s [-c config][--client]\n",
             argv[0]);
}


static void show_login (struct cmdlintf_t *cmd) {
    cmd->print ("tcp-proxy version " APP_VERSION "\n\n");
}

static void testing () {
}

static void check_or_mkdir (const char * const full_path, struct passwd *entry) {
    if (full_path != NULL) {
        const char *ptr = strrchr (full_path, '/');

        if (ptr != NULL) {
            char *folder = strndup (full_path, ptr - full_path);

            if (access (folder, R_OK) != 0) {
                mode_t prev_mask = umask (002);

                if (mkdir (folder, 0775) == 0) {
                    chown (folder, -1, entry->pw_gid);
                }

                umask (prev_mask);
            }

            free (folder);
        }
    }
}

int main (int argc, char *argv[]) {
    const static char *const config_files[] = {
        "tcp-proxy.conf",
        "/usr/local/etc/tcp-proxy.conf",
    };
    const char *config_file = NULL;
    int client_flag = 0;
    int daemon_flag = 1;
    int testing_flag = 0;
    const struct option long_options[] = {
        {"client",   no_argument,       &client_flag,  1},
        {"testing",  no_argument,       &testing_flag, 1},
        {"no-daemon", no_argument,       &daemon_flag,  0},
        {"config",   required_argument, 0,             'c'},
        {0, 0,                          0,             0}
    };

    application_context = get_application_context ();
    logger = application_context->get_logger ();

    app_boot_time = time (NULL);

    fprintf (stderr, "tcp-proxy v" APP_VERSION " (c) 2022 written by Mac Liu <linuzilla@gmail.com>\n\n");

    while (true) {
        int c;
        int option_index = 0;

        if ((c = getopt_long (argc, argv, "c:dv", long_options, &option_index)) == -1) {
            break;
        }

        switch (c) {
        case 0:
            break;
        case 'c':
            config_file = optarg;
            break;
        default: /* '?' */
            usage (argv);
            exit (EXIT_FAILURE);
        }
    }

    if (config_file == NULL) {
        int i;

        for (i = 0; i < (sizeof (config_files) / sizeof (char *)); i++) {
            if (access (config_files[i], R_OK) == 0) {
                config_file = config_files[i];
                break;
            }
        }
    }

    struct cmdlintf_t *cmd = init_cmdline_interface (65536);
    int errcode = 0;

    if ((errcode = application_context->populate (cmd)) != 0) {
        logger->warning (__FILE__, __LINE__, "cmdline interface: %s", application_context->error_message (errcode));
    }

    if (cmd == NULL) {
        exit (EXIT_FAILURE);
    }

    if ((conf = new_system_config (config_file)) == NULL) {
        exit (EXIT_FAILURE);
    } else {
        const char *socket_file = conf->str_or_default ("socket-name", "/var/run/tcp-proxy/tcp-proxy.sock");
        cmd->socket_name (socket_file);
        cmd->set_prompt ("proxy> ");

        application_context->populate (conf);

        const char * const run_as = conf->str ("run-as");

        if (run_as != NULL && strlen (run_as) > 0) {
            struct passwd *entry;

            if ((entry = getpwnam (run_as)) != NULL) {
                check_or_mkdir (socket_file, entry);

                setregid (entry->pw_gid, entry->pw_gid);
                setreuid (entry->pw_uid, entry->pw_uid);
            } else {
                fprintf (stderr, "user: %s not found\n", run_as);
                exit (EXIT_FAILURE);
            }
        }

        if (client_flag) {
            application_context->auto_wiring ();

            register_commands (cmd);
            cmd->start_client();
            while (cmd->cli() != 0);
            exit (EXIT_SUCCESS);
        }

        const bool run_as_daemon = daemon_flag && conf->int_or_default ("daemon", 0) != 0;

        if (run_as_daemon) {
            if (fork() > 0) exit (0);
            chdir ("/");
            setsid();
            signal (SIGHUP, SIG_IGN);
            close (STDIN_FILENO);
            close (STDOUT_FILENO);
        }

        expiring_timeout = (double) conf->int_or_default ("expiring-timeout", 180);

        const int hash_size = conf->int_or_default ("hash-size", 521);
        const int monitor_period = conf->int_or_default ("monitor-period", 86400);

        fprintf (stderr, "Auto-Blacklist: hash size: %d, monitor: %d second(s)\n", hash_size, monitor_period);

        blacklistService = new_auto_blacklist_service (hash_size, monitor_period);
        application_context->populate (blacklistService);

        const char *logfile_name = "log-file";
        const char *logfile = conf->str (logfile_name);

        if (logfile != NULL) {
            if (! testing_flag && strcmp ("<<syslog>>", logfile) == 0) {
                logger->clearAppender ();
                fprintf (stderr, "Logging: syslog\n");
                logger->addAppender (&excalibur_syslog_appender);
            } else if (strcmp ("<<console>>", logfile) == 0) {
                logger->clearAppender ();
                fprintf (stderr, "Logging: console\n");
                logger->addAppender (&excalibur_console_appender);
            }
        } else {
            int i, n = 0;
            char **ptr = conf->string_list (logfile_name, &n);

            if (n > 0 && ptr != NULL) {
                logger->clearAppender ();

                for (i = 0; i < n; i++) {
                    if (! testing_flag && strcmp ("<<syslog>>", ptr[i]) == 0) {
                        fprintf (stderr, "Logging: syslog\n");
                        logger->addAppender (&excalibur_syslog_appender);
                    } else if (strcmp ("<<console>>", ptr[i]) == 0) {
                        fprintf (stderr, "Logging: console\n");
                        logger->addAppender (&excalibur_console_appender);
                    }
                }
            }
        }

        const char *const log_priority_name = conf->str ("log-priority");

        if (log_priority_name != NULL) {
            current_log_priority = logger_get_priority_by_name (log_priority_name);
            fprintf (stderr, "Logging priority: %s\n", logger_get_priority_name (current_log_priority));
        }

        logger->setPriority (current_log_priority);

        db_svc = new_database_service (conf);

        if (testing_flag) {
            testing();
            return EXIT_SUCCESS;
        }

        proxyingService = init_proxying_service();

        application_context->populate (db_svc);
        application_context->populate (init_packet_analyzer ());
        application_context->populate (proxyingService);
//        db_svc = new_database_service (system_conf);
//        packetAnalyzer = init_packet_analyzer (logger);
//        db_svc = new_database_service (conf);
//        db_svc->set_logger (logger);
//        application_context->populate (db_svc);

        minute_timer = new_minite_timer();

        main_thread = pthread_self();

        register_commands (cmd);

        cmd->set_login_callback (show_login);

        if (application_context->auto_wiring ()) {
            if (!cmd->start_server_in_thread (&command_thread)) {
                exit (EXIT_FAILURE);
            }

            proxyingService->start_proxying (&proxy_thread);

            signal (SIGINT, interrupt);
            signal (SIGTERM, interrupt);
            signal (SIGHUP, SIG_IGN);
            signal (SIGUSR1, log_level_change);
            signal (SIGUSR2, log_level_change);

            run_timer();

            pthread_join (proxy_thread, NULL);
        }
        blacklistService->terminate();
    }

    return EXIT_SUCCESS;
}
