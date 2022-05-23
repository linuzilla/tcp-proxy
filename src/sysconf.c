#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "sysconf.h"
#include "parser.h"
#include "utils.h"
#include "context.h"
#include "logger.h"

#define MAX_INT_STK_LEN        64

enum stack_type_enum {
    IntegerStack,
    StringStack,
    UninitializedStack,
};

struct generic_stack {
    union {
        int intval;
        char *str;
    };
};

struct system_config_t *system_config;
FILE *yyin;

struct sysconf_entry_t {
    char *key;
    enum entry_type_enum entry_type;
    int ivalue;
    char *value;
    int *list;
    char **slist;
    struct sysconf_entry_t *next;
};

static struct logger_t *logger;
static struct sysconf_entry_t *sysconf_root = NULL;
static struct sysconf_entry_t *sysconf_cursor = NULL;
static volatile bool terminate_flag = false;

static enum stack_type_enum stack_type = UninitializedStack;
static struct generic_stack stk[MAX_INT_STK_LEN];
static int sp = 0;

//static struct system_config_data_t singleton_data, *data;

static char *dup_quoted_string (const char *qst) {
    ssize_t len = strlen (qst);

    if (qst[0] == '"' && qst[len - 1] == '"') {
        char *str = malloc (len - 1);

        if (str != NULL) {
            memcpy (str, &qst[1], len - 2);
            str[len - 2] = '\0';
            return str;
        } else {
            return NULL;
        }
    } else {
        return strdup (qst);
    }
}

static void *addentry_string (const char *entry, const char *value) {
    struct sysconf_entry_t *ptr;

    // printf("addentry (%s, %s)\n", entry, value);

    if ((ptr = (struct sysconf_entry_t *) malloc (sizeof (struct sysconf_entry_t))) != NULL) {
        ptr->key = strdup (entry);
        ptr->entry_type = StringValue;
        if (value[0] == '"') {
            ptr->value = dup_quoted_string (value);
        } else {
            ptr->value = strdup (value);
        }
        ptr->ivalue = -1;
        ptr->next = sysconf_root;
        sysconf_root = ptr;
        return ptr;
    }
    return NULL;
}

static void *addentry_int_list (const char *entry, int *int_list, int len) {
    struct sysconf_entry_t *ptr;

    if ((ptr = (struct sysconf_entry_t *) malloc (sizeof (struct sysconf_entry_t))) != NULL) {
        ptr->key = strdup (entry);
        ptr->entry_type = IntegerList;
        ptr->list = int_list;
        ptr->ivalue = len;
        ptr->next = sysconf_root;
        sysconf_root = ptr;
        return ptr;
    }
    return NULL;
}

static void *addentry_string_list (const char *entry, char **string_list, int len) {
    struct sysconf_entry_t *ptr;

    if ((ptr = (struct sysconf_entry_t *) malloc (sizeof (struct sysconf_entry_t))) != NULL) {
        ptr->key = strdup (entry);
        ptr->entry_type = StringList;
        ptr->slist = string_list;
        ptr->ivalue = len;
        ptr->next = sysconf_root;
        sysconf_root = ptr;
        return ptr;
    }
    return NULL;
}

static void addentry_integer (const char *entry, const char *value) {
    int data;
    struct sysconf_entry_t *ptr;

    data = atoi (value);

    if ((ptr = (struct sysconf_entry_t *) addentry_string (entry, value)) != NULL) {
        ptr->ivalue = data;
        ptr->entry_type = IntegerValue;
    }
}

static void addentry_ip (const char *entry, const char *value) {
    addentry_string (entry, value);
}

static void addentry_mac (const char *entry, const char *value) {
    addentry_string (entry, value);
}

static void addentry_flag_on (const char *entry) {
    struct sysconf_entry_t *ptr;

    if ((ptr = addentry_string (entry, "on")) != NULL) {
        ptr->ivalue = 1;
        ptr->entry_type = BooleanValue;
    }
}

static void addentry_flag_off (const char *entry) {
    struct sysconf_entry_t *ptr;

    if ((ptr = addentry_string (entry, "off")) != NULL) {
        ptr->ivalue = 0;
        ptr->entry_type = BooleanValue;
    }
}

static void add_prepared_list (const char *entry) {
    switch (stack_type) {
    case IntegerStack: {
        int i;
        int *list;

        list = calloc (sp, sizeof (int));

        if (list != NULL) {
            for (i = 0; i < sp; i++) {
                list[i] = stk[sp - i - 1].intval;
            }
        }

        addentry_int_list (entry, list, sp);
        break;
    }
    case StringStack: {
        int i;
        char **list;

        list = calloc (sp, sizeof (char *));

        if (list != NULL) {
            for (i = 0; i < sp; i++) {
                list[i] = stk[sp - i - 1].str;
            }
        }

        addentry_string_list (entry, list, sp);

        break;
    }
    default:
        break;
    }
    sp = 0;
    stack_type = UninitializedStack;
}

static void set_list_as_integer (void) {
    stack_type = IntegerStack;
}

static void set_list_as_string (void) {
    stack_type = StringStack;
}

static void list_append_int (char *entry) {
    if (entry != NULL) {
        if (sp < MAX_INT_STK_LEN) {
            stk[sp++].intval = atoi (entry);
        }
        free (entry);
    }
}

static void list_append_str (char *entry) {
    if (entry != NULL) {
        if (sp < MAX_INT_STK_LEN) {
            stk[sp++].str = dup_quoted_string (entry);
        }
        free (entry);
    }
}

//static void set_listen_interface (const char *intf) {
//    data->listen_interface = dup_quoted_string (intf);
//    // printf ("listen on %s\n", data->listen_interface);
//}
//
//static void set_network_netmask (const char *network, const char *netmask) {
//    data->arpguard_network.s_addr = inet_addr (network);
//    data->arpguard_netmask.s_addr = inet_addr (netmask);
//    data->with_arpguard_network = 1;
//}

static struct sysconf_entry_t *sysconf_ptr (const char *key) {
    struct sysconf_entry_t *ptr;

    for (ptr = sysconf_root; ptr != NULL; ptr = ptr->next) {
        if (strcasecmp (key, ptr->key) == 0) {
            return ptr;
        }
    }
    return NULL;
}

static const char * const sysconf_str (const char *key) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        return ptr->value;
    }
    return NULL;
}

static const char * const sysconf_str_or_default (const char *key, const char *defaultValue) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        return ptr->value;
    }
    return defaultValue;
}

static int sysconf_int_or_default (const char *key, const int defaultValue) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        return ptr->ivalue;
    }
    return defaultValue;
}


static enum entry_type_enum data_type (const char *key) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        return ptr->entry_type;
    }
    return IntegerValue;
}

static int *integer_list (const char *key, int *sz) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        if (ptr->entry_type == IntegerList) {
            if (sz != NULL) {
                *sz = ptr->ivalue;
            }
            return ptr->list;
        }
    }
    return NULL;
}

static char **string_list (const char *key, int *sz) {
    struct sysconf_entry_t *ptr;

    if ((ptr = sysconf_ptr (key)) != NULL) {
        if (ptr->entry_type == StringList) {
            if (sz != NULL) {
                *sz = ptr->ivalue;
            }
            return ptr->slist;
        }
    }
    return NULL;
}

static int sysconf_int (const char *key) {
    return sysconf_int_or_default (key, -1);
}

static char *sysconf_get_first_key (void) {
    sysconf_cursor = sysconf_root;

    if (sysconf_cursor != NULL) {
        return sysconf_cursor->key;
    }

    return NULL;
}

static char *sysconf_get_next_key (void) {
    if (sysconf_cursor != NULL) {
        if ((sysconf_cursor = sysconf_cursor->next) != NULL) {
            return sysconf_cursor->key;
        }
    }
    return NULL;
}

static volatile bool terminated() {
    return terminate_flag;
}

static void terminate() {
    terminate_flag = true;
}

//static struct system_config_data_t *get_config_data (void) {
//    return &singleton_data;
//}

static const char *const context_name (void) {
    const static char *const name = SYSTEM_CONFIG_DEFAULT_CONTEXT_NAME;
    return name;
}

static struct system_config_t singleton = {
    .context = {
        .header = {
            .magic = CONTEXT_MAGIC_NUMBER,
            .version_major = CONTEXT_MAJOR_VERSION,
            .version_minor = CONTEXT_MINOR_VERSION,
        },
        .name = context_name,
        .post_construct = NULL,
        .depends_on = NULL,
    },
    .terminate = terminate,
    .terminated = terminated,
    .addentry_flag_off = addentry_flag_off,
    .addentry_integer = addentry_integer,
    .addentry_string = addentry_string,
    .addentry_ip = addentry_ip,
    .addentry_mac = addentry_mac,
    .addentry_flag_on = addentry_flag_on,
    .addentry_flag_off = addentry_flag_off,
    .add_prepared_list = add_prepared_list,
    .set_list_as_integer = set_list_as_integer,
    .set_list_as_string = set_list_as_string,
    .list_append_int = list_append_int,
    .list_append_str = list_append_str,
//    .set_listen_interface = set_listen_interface,
//    .set_network_netmask = set_network_netmask,
    .str = sysconf_str,
    .str_or_default = sysconf_str_or_default,
    .integer = sysconf_int,
    .int_or_default = sysconf_int_or_default,
    .first_key = sysconf_get_first_key,
    .next_key = sysconf_get_next_key,
//    .get_config_data = get_config_data,
    .data_type = data_type,
    .string_list = string_list,
    .integer_list = integer_list,
};


struct system_config_t *new_system_config (const char *filename) {
    if (system_config == NULL) {
        logger = get_application_context()->get_logger();
        system_config = &singleton;
//        data = &singleton_data;
//        memset (&singleton_data, 0, sizeof (singleton_data));

        fprintf (stderr, "Reading config file \"%s\" ... ", filename);

        if ((yyin = fopen (filename, "r")) != NULL) {
            int result = yyparse();
            fclose (yyin);

            if (result != 0) {
                fprintf (stderr, "error\r\n");
                return NULL;
            } else {
                fprintf (stderr, "ok\r\n");
            }
        } else {
            perror ("");
            return NULL;
        }

    }

    return system_config;
}

