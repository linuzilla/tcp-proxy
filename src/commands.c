//
// Created by saber on 5/8/22.
//

#include <pthread.h>
#include <signal.h>
#include "logger.h"
#include "global_vars.h"
#include "sysconf.h"
#include "cmdlintf.h"
#include "commands.h"
#include "packet_analyzer.h"
#include "context.h"

static struct logger_t *logger = &excalibur_common_logger;
static struct packet_analyzer_t *packetAnalyzer = NULL;
static struct auto_blacklist_service_t *blacklistService = NULL;
static struct proxying_service_t *proxyingService = NULL;
static struct system_config_t *conf;

static int cmd_echo (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "echo");
    cmd->print ("%s\n", args != NULL ? args : "");
    return 1;
}

static int cmd_date (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "date");
    time_t now;

    now = time (NULL);
    cmd->print ("%s", ctime (&now));
    return 1;
}


static int cmd_logout (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "exit");
    cmd->print ("**** Exit ****\n");
    return 0;
}

static int cmd_shutdown (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "shutdown");
    cmd->terminate();
    cmd->print ("**** Shutdown ****\n");
    pthread_kill (*global_vars.command_thread, SIGINT);
    pthread_kill (*global_vars.main_thread, SIGINT);
    return 0;
}

static int cmd_uptime (struct cmdlintf_t *cmd, const char *args) {
    const double duration = difftime (time (NULL), *global_vars.app_boot_time);

    int day = duration / 86400;
    int seconds = (long) duration % 86400L;
    int hour = seconds / 3600;
    int min = (seconds % 3600) / 60;
    int sec = seconds % 60;

    if (day > 0) {
        cmd->print ("Uptime: %d day(s), %02d:%02d:%02d\n",
                    day, hour, min, sec);
    } else {
        cmd->print ("Uptime:  %02d:%02d:%02d\n",
                    hour, min, sec);
    }
    return 1;
}

static int cmd_set_log_level (struct cmdlintf_t *cmd, enum log_priority_t priority) {
    const char *priorityName = logger_get_priority_name (priority);
    logger->setPriority (priority);
    cmd->print ("Logging level: %s\n", priorityName);
    return 1;
}

static int cmd_logging_trace (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_trace);
}

static int cmd_logging_debug (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_debug);
}

static int cmd_logging_info (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_info);
}

static int cmd_logging_notice (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_notice);
}

static int cmd_logging_warning (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_notice);
}

static int cmd_logging_error (struct cmdlintf_t *cmd, const char *args) {
    return cmd_set_log_level (cmd, log_error);
}

static int cmd_fall_back_channel (struct cmdlintf_t *cmd, const char *args) {
    if (args == NULL) {
        cmd->print ("usage: set fall back channel <channel> (current = %d)\n", proxyingService->get_fallback_channel ());
    } else {
        int channel = atoi (args);

        if (proxyingService->set_fallback_channel (channel) == channel) {
            cmd->print ("default channel set to %d\n", channel);
        } else {
            cmd->print ("failed to set default channel to %d\n", channel);
        }
    }
    return 1;
}

static int cmd_default_channel (struct cmdlintf_t *cmd, const char *args) {
    if (args == NULL) {
        cmd->print ("usage: set default channel <channel> (current = %d)\n", proxyingService->get_default_channel ());
    } else {
        int channel = atoi (args);

        if (proxyingService->set_default_channel (channel) == channel) {
            cmd->print ("default channel set to %d\n", channel);
        } else {
            cmd->print ("failed to set default channel to %d\n", channel);
        }
    }
    return 1;
}

static int cmd_load_module (struct cmdlintf_t *cmd, const char *args) {
    switch (packetAnalyzer->load_packet_analyzer (false, args)) {

    case 0:
        cmd->print ("module loaded successfully\n");
        break;
    case 1:
        cmd->print ("module not load-on-boot\n");
        break;
    case 2:
        cmd->print ("failed to loading module\n");
        break;
    case 3:
        cmd->print ("module file name required\n");
        break;
    default:
        cmd->print ("unknow error\n");
        break;

    }

    return 1;
}

static int cmd_unload_module (struct cmdlintf_t *cmd, const char *args) {
    if (packetAnalyzer->unload ()) {
        cmd->print ("module unload successfully\n");
    } else {
        cmd->print ("failed to unload module unload module\n");
    }
    return 1;
}

static int cmd_packet_analyzer_mode_safe (struct cmdlintf_t *cmd, const char *args) {
    packetAnalyzer->set_safe_mode (true);
    cmd->print ("packet analyzer mode: safe\n");
    return 1;
}

static int cmd_packet_analyzer_mode_fast (struct cmdlintf_t *cmd, const char *args) {
    packetAnalyzer->set_safe_mode (false);
    cmd->print ("packet analyzer mode: fast\n");
    return 1;
}

static int cmd_packet_analyzer_mode (struct cmdlintf_t *cmd, const char *args) {
    cmd->print ("packet analyzer mode: %s\n",
                packetAnalyzer->get_safe_mode () ? "safe" : "fast");
    return 1;
}

static int cmd_enable_packet_analyzer (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "Enable packet analyzer");
    if (packetAnalyzer->set_enable (true)) {
        cmd->print ("packet analyzer: enabled\n");
    } else {
        cmd->print ("packet analyzer: NOT enabled\n");
    }
    return 1;
}

static int cmd_disable_packet_analyzer (struct cmdlintf_t *cmd, const char *args) {
    logger->notice (__FILE__, __LINE__, "Disable packet analyzer");
    if (packetAnalyzer->set_enable (false)) {
        cmd->print ("packet analyzer: disable\n");
    } else {
        cmd->print ("packet analyzer: NOT disable\n");
    }
    return 1;
}

void register_commands (struct cmdlintf_t *cmd) {
    struct application_context_t *application_context = get_application_context();

    logger = application_context->get_logger();

    conf = (struct system_config_t *) application_context->get_bean (SYSTEM_CONFIG_DEFAULT_CONTEXT_NAME);
    packetAnalyzer = (struct packet_analyzer_t *) application_context->get_bean (PACKET_ANALYZER_DEFAULT_CONTEXT_NAME);
    blacklistService = (struct auto_blacklist_service_t *) application_context->get_bean (AUTO_BLACKLIST_DEFAULT_CONTEXT_NAME);
    proxyingService = (struct proxying_service_t *) application_context->get_bean (PROXYING_SERVICE_DEFAULT_CONTEXT_NAME);

    cmd->regcmd();

    cmd->add ("exit", false, cmd_logout, "Exit", 0, 1);
    cmd->add ("shutdown", true, cmd_shutdown, "Shutdown", 0, 1);
    cmd->add ("date", true, cmd_date, "Date", 0, 1);
    cmd->add ("echo", false, cmd_echo, "Echo", 0, 1);
    cmd->add ("uptime", true, cmd_uptime, "Uptime", 0, 1);
    cmd->add ("set logging level trace", true, cmd_logging_trace, "Log level = trace", 0, 1);
    cmd->add ("set logging level debug", true, cmd_logging_debug, "Log level = debug", 0, 1);
    cmd->add ("set logging level info", true, cmd_logging_info, "Log level = info", 0, 1);
    cmd->add ("set logging level notice", true, cmd_logging_notice, "Log level = notice", 0, 1);
    cmd->add ("set logging level warning", true, cmd_logging_warning, "Log level = warning", 0, 1);
    cmd->add ("set logging level error", true, cmd_logging_error, "Log level = error", 0, 1);
    cmd->add ("set fall back channel", true, cmd_fall_back_channel, "setting fall back channel", 1, 1);
    cmd->add ("set default channel", true, cmd_default_channel, "setting default channel", 1, 1);
    cmd->add ("load module", true, cmd_load_module, "load module", 1, 1);
    cmd->add ("unload module", true, cmd_unload_module, "unload module", 0, 1);
    cmd->add ("analyzer enable", true, cmd_enable_packet_analyzer, "enable packet analyzer", 0, 1);
    cmd->add ("analyzer disable", true, cmd_disable_packet_analyzer, "enable packet analyzer", 0, 1);
    cmd->add ("analyzer mode safe", true, cmd_packet_analyzer_mode_safe, "enable packet analyzer safe mode", 0, 1);
    cmd->add ("analyzer mode fast", true, cmd_packet_analyzer_mode_fast, "enable packet analyzer fast mode", 0, 1);
    cmd->add ("show analyzer mode", true, cmd_packet_analyzer_mode, "packet analyzer mode", 0, 1);
}