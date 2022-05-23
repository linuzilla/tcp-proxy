//
// Created by saber on 5/5/22.
//

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "sysconf.h"
#include "logger.h"
#include "exception.h"
#include "packet_analyzer.h"

static struct system_config_t *sysconf;
static struct logger_t *logger = &excalibur_common_logger;
static struct pluggable_packet_analyzer_t *pluggableAnalyzer = NULL;
static void *library;
static int reference_count = 0;
static volatile bool plugin_enable = false;
static bool auto_unload = false;
static volatile bool safe_mode = false;

struct analyze_param_t {
    struct connection_info *info;
    bool fromClient;
    char *buffer;
    const ssize_t len;
    uint64_t *return_value;
};

struct module_loader_t {
    bool (*load) (const char *file);
    void (*unload) (void);
};

static bool module_loader (const char *file, const char *symbol) {
    if (pluggableAnalyzer == NULL) {
        const char *err;

        library = dlopen (file, RTLD_LAZY);
        err = dlerror();    /* Clear any existing error */

        if (library == NULL || err != NULL) {
            logger->error (__FILE__, __LINE__, "loading %s: %s", file, err);
            return false;
        }

        plugin_enable = false;
        auto_unload = false;

        pluggableAnalyzer = dlsym (library, symbol);

        if ((err = dlerror()) != NULL)  {
            logger->error (__FILE__, __LINE__, "loading %s: %s", file, err);
            return false;
        }

        logger->notice (__FILE__, __LINE__, "plugin loaded: %s (%s)", file, symbol);

        pluggableAnalyzer->init (get_application_context());
        return true;
    } else {
        return false;
    }
}

static bool module_unload () {
    if (pluggableAnalyzer != NULL) {
        if (reference_count <= 0) {
            pluggableAnalyzer = NULL;

            if (library != NULL) {
                if (dlclose (library) == 0) {
                    library = NULL;
                    logger->notice (__FILE__, __LINE__, "plugin unloaded");
                } else {
                    return false;
                }
            }
            return true;
        } else {
            auto_unload = true;
            logger->notice (__FILE__, __LINE__, "plugin busy");
        }
    }
    return false;
}

static bool set_enable (const bool on_off) {
    plugin_enable = on_off && pluggableAnalyzer != NULL;

    return plugin_enable;
}

static void set_safe_mode (const bool on_off) {
    safe_mode = on_off;
}

static bool get_safe_mode (void) {
    return safe_mode;
}

static void* analyzer_allocate () {
    if (pluggableAnalyzer != NULL && plugin_enable) {
        void *ptr = pluggableAnalyzer->allocate();
        if (ptr != NULL) {
            reference_count++;
        }
        return ptr;
    }
    return NULL;
}

static void analyzer_release (void *data) {
    if (data != NULL && pluggableAnalyzer != NULL) {
        pluggableAnalyzer->release (data);
        reference_count--;

        if (reference_count == 0 && auto_unload) {
            module_unload();
        }
    }
}

static void safe_analyze_packet (va_list ap) {
    struct analyze_param_t *param = va_arg (ap, struct analyze_param_t *);

    *param->return_value = pluggableAnalyzer->analyze (
                               param->info,
                               param->fromClient,
                               param->buffer,
                               param->len);
}

static uint64_t analyze_packet (struct connection_info *info, bool fromClient, char *buffer, const ssize_t len) {
    uint64_t return_value = 0L;

    if (plugin_enable && info->packet_analyzer_data != NULL && pluggableAnalyzer != NULL) {
        if (safe_mode) {
            struct analyze_param_t param = {
                .info = info,
                .fromClient = fromClient,
                .buffer = buffer,
                .len = len,
                .return_value = &return_value,
            };

            if (try_catch (safe_analyze_packet, &param)) {
                plugin_enable = false;
                auto_unload = true;
                logger->warning (__FILE__, __LINE__, "exception caught, disable plugin");
            } else {
                return return_value;
            }
        } else {
            return_value = pluggableAnalyzer->analyze (
                               info,
                               fromClient,
                               buffer,
                               len);
        }
    }
    return return_value;
}

static int load_packet_analyzer (bool boot, const char *module_name) {
    if (boot) {
        int load_on_boot = sysconf->int_or_default ("load-plugin-on-boot", 0);

        if (load_on_boot == 0) {
            return 1;
        }
    }

    if (module_name == NULL) {
        module_name = sysconf->str_or_default ("packet-analyzer-plugin", "/usr/local/libexec/tcp-proxy/libpkanalyzer.so");
    }
    if (module_name != NULL) {
        if (module_loader (module_name, PACKET_ANALYZER_MODULE_NAME)) {
            if (boot) {
                if (sysconf->int_or_default ("enable-plugin-on-boot", 0) != 0) {
                    set_enable (true);
                }
            }
            return 0;
        } else {
            return 2;
        }
    } else {
        return 3;
    }
}

static const char *const context_name (void) {
    return PACKET_ANALYZER_DEFAULT_CONTEXT_NAME;
}

static void post_construct (void) {
    logger->trace (__FILE__, __LINE__, "%s:%d %s", __FILE__, __LINE__, __FUNCTION__ );
}

static struct packet_analyzer_t instance = {
    .context = {
        .header = {
            .magic = CONTEXT_MAGIC_NUMBER,
            .version_major = CONTEXT_MAJOR_VERSION,
            .version_minor = CONTEXT_MINOR_VERSION,
        },
        .name = context_name,
        .post_construct = post_construct,
        .depends_on = NULL,
    },
    .load_packet_analyzer = load_packet_analyzer,
    .load = module_loader,
    .unload = module_unload,
    .allocate = analyzer_allocate,
    .release = analyzer_release,
    .analyze_packet = analyze_packet,
    .set_enable = set_enable,
    .set_safe_mode = set_safe_mode,
    .get_safe_mode = get_safe_mode,
};

struct packet_analyzer_t *init_packet_analyzer (void) {
    logger = get_application_context()->get_logger ();
    sysconf = (struct system_config_t *) get_application_context()->get_bean (SYSTEM_CONFIG_DEFAULT_CONTEXT_NAME);

    load_packet_analyzer (true, NULL);

    return &instance;
}
