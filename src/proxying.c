//
// Created by Mac Liu on 11/30/21.
//

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <strings.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "global_vars.h"
#include "proxying.h"
#include "sysconf.h"
#include "events.h"
#include "db_service.h"
#include "logger.h"
#include "utils.h"
#include "auto_blacklist.h"
#include "packet_analyzer.h"

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>

struct remote_server_t {
    char *host;
    int port;
};

static struct system_config_t *system_conf;
static struct logger_t *logger = &excalibur_common_logger;
static struct packet_analyzer_t *packetAnalyzer = NULL;
static struct auto_blacklist_service_t *blacklistService = NULL;
static struct event_loop_t *ev;
static struct database_service_t *db_svc;
static int64_t connection_counter = 0L;
static struct remote_server_t *remote_servers;
static int number_of_remote_servers = 0;
static int default_server = 0;
static int connection_threshold = 5;
static int persist_threshold = 100;
static int max_allowed_requests = 6;
static uint32_t user_counter = 0;

static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t info_mux = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pool_mux = PTHREAD_MUTEX_INITIALIZER;
static struct connection_info *infos = NULL;
static struct connection_info *pool = NULL;
static int number_of_entries = 0;
static int number_of_allocation = 0;
static struct connection_info **expiring_holder = NULL;
static int size_of_expiring_holder = 0;
static long max_persistent_time = 86400L;
static int on_failed_channel = 0;


static void free_proxy_request_data (struct db_proxy_request_t *request) {
    if (request != NULL) {
        if (request->account != NULL) {
            free (request->account);
            request->account = NULL;
        }
        free (request);
    }
}

static struct connection_info *allocate_connection_info() {
    pthread_mutex_lock (&pool_mux);
    struct connection_info *entry;

    if (pool != NULL) {
        logger->trace (__FILE__, __LINE__, "reuse free entry");
        entry = pool;
        pool = pool->next;
    } else {
        entry = malloc (sizeof (struct connection_info));
        number_of_allocation++;
        logger->debug (__FILE__, __LINE__, "allocate new entry (%d allocation entries)", number_of_allocation);
    }

    pthread_mutex_unlock (&pool_mux);
    return entry;
}

static void free_connection_info (struct connection_info *entry) {
    pthread_mutex_lock (&pool_mux);
    entry->next = pool;
    pool = entry;
    logger->trace (__FILE__, __LINE__, "free entry");
    pthread_mutex_unlock (&pool_mux);
}

static void attach_connection_info_entry (struct connection_info *entry) {
    pthread_mutex_lock (&info_mux);
    number_of_entries++;
    entry->next = infos;
    entry->prev = NULL;
    entry->in_chain = true;
    if (infos != NULL) {
        infos->prev = entry;
    }
    infos = entry;
    logger->trace (__FILE__, __LINE__, "attach entry (%d)", number_of_entries);
    pthread_mutex_unlock (&info_mux);
}

static void detach_connection_info_entry (struct connection_info *entry) {
    if (entry->in_chain) {
        pthread_mutex_lock (&info_mux);
        number_of_entries--;
        if (entry == infos) {
            infos = infos->next;
            if (infos != NULL) {
                infos->prev = NULL;
            }
        } else {
            entry->prev->next = entry->next;
            if (entry->next != NULL) {
                entry->next->prev = entry->prev;
            }
        }
        pthread_mutex_unlock (&info_mux);

        entry->in_chain = false;
        logger->trace (__FILE__, __LINE__, "detach entry (%d)", number_of_entries);
    }
}

static void close_event (struct connection_info *info, bool idle) {
    if (!info->in_chain) {
        ev->remove_event (info->server_handle);
        ev->remove_event (info->client_handle);
        shutdown (info->client_fd, SHUT_RDWR);
        shutdown (info->server_fd, SHUT_RDWR);
        close (info->client_fd);
        close (info->server_fd);

        packetAnalyzer->release (info->packet_analyzer_data);

        struct timeval tv;
        gettimeofday (&tv, NULL);
        double elapsed = elapsed_time (&tv, &info->started);

        int count = ev->count();

        if (info->request_in_db != NULL) {
            logger->notice (__FILE__, __LINE__,
                            "Connection closed [%ld]: (account[%d]: %s / %s, S: %ld (%d), R: %ld (%d), elapsed: %.2f, %d event(s) left)",
                            info->connection_id,
                            info->nth_user,
                            info->request_in_db->account != NULL ? info->request_in_db->account : "(null)",
                            info->remote_ip,
                            info->bytesSent,
                            info->requestCount,
                            info->bytesReceived,
                            info->responseCount,
                            elapsed,
                            count);
            db_svc->connection_close (info->insert_id, info->bytesReceived, info->requestCount, idle);

            free_proxy_request_data (info->request_in_db);
            info->request_in_db = NULL;
        } else {
            char buffer[32];

            if (info->attempts > 1) {
                sprintf (buffer, " %d attempt(s),", info->attempts);
            } else {
                buffer[0] = '\0';
            }
            logger->notice (__FILE__, __LINE__,
                            "Connection closed [%ld]: [ %s,%s sent: %ld (%d), received: %ld (%d), elapsed: %.2f, %d event(s) left ]",
                            info->connection_id,
                            info->remote_ip,
                            buffer,
                            info->bytesSent,
                            info->requestCount,
                            info->bytesReceived,
                            info->responseCount,
                            elapsed,
                            count);
        }

        free (info->remote_ip);

        db_svc->done();
    }
}

static void tell_time (time_t start_time) {
    static uint32_t last_user_counter = 0;
    static int64_t last_connection_counter = 0L;
    static time_t last_time = 0L;

    const time_t now = time (NULL);

    const double duration = difftime (now, start_time);

    int day = duration / 86400;
    int seconds = (long) duration % 86400L;
    int hour = seconds / 3600;
    int min = (seconds % 3600) / 60;
    int sec = seconds % 60;


    double duration2 = difftime (now, last_time != 0L ? last_time : start_time);

    double rps_recent = (double) (connection_counter - last_connection_counter) / duration2;
    double rps_total = (double) connection_counter / duration;

    if (day > 0) {
        logger->notice (__FILE__, __LINE__,
                        "Uptime: %d day(s), %02d:%02d:%02d, events: %d, # of users: %u / %u (total), entries: %d / %d, RPS: %.2f / %.2f (total)",
                        day, hour, min, sec,
                        ev->count(), (user_counter - last_user_counter), user_counter,
                        number_of_entries, number_of_allocation,
                        rps_recent, rps_total);
    } else {
        logger->notice (__FILE__, __LINE__,
                        "Uptime: %02d:%02d:%02d, events: %d, # of users: %u / %u (total), entries: %d / %d, RPS: %.2f / %.2f (total)",
                        hour, min, sec,
                        ev->count(), (user_counter - last_user_counter), user_counter,
                        number_of_entries, number_of_allocation,
                        rps_recent, rps_total);
    }

    last_user_counter = user_counter;
    last_connection_counter = connection_counter;
    last_time = now;
}

void clean_idle_connections (const struct timeval *tv, double timeout) {
    int n = 0;

    pthread_mutex_lock (&info_mux);

    if (number_of_entries > 0) {
        if (expiring_holder != NULL && size_of_expiring_holder < number_of_entries) {
            free (expiring_holder);
            expiring_holder = NULL;
        }
        if (expiring_holder == NULL) {
            expiring_holder = malloc (number_of_entries * sizeof (struct connection_info *));
            logger->debug (__FILE__, __LINE__, "Enlarge expiring holder from %d to %d", size_of_expiring_holder,
                           number_of_entries);
            size_of_expiring_holder = number_of_entries;
        }

        struct connection_info *ptr;

        for (ptr = infos, n = 0; ptr != NULL && n < number_of_entries; ptr = ptr->next, n++) {
            expiring_holder[n] = ptr;
        }
    }
    pthread_mutex_unlock (&info_mux);

    int i, counter = 0;

    for (i = 0; i < n; i++) {
        struct connection_info *ptr = expiring_holder[i];
        double duration = elapsed_time (tv, &ptr->recent);

        if (duration > timeout) {
            if (pthread_mutex_trylock (&worker_mutex) != 0) {
                logger->info (__FILE__, __LINE__, "Expiring thread: wait a moment");
                pthread_mutex_lock (&worker_mutex);
            }

            pthread_mutex_lock (&expiring_holder[i]->mutex);
            detach_connection_info_entry (expiring_holder[i]);
            logger->info (__FILE__, __LINE__, "Expiring %s, duration: %.2f", expiring_holder[i]->remote_ip, duration);

            close_event (expiring_holder[i], true);

            pthread_mutex_unlock (&expiring_holder[i]->mutex);
            free_connection_info (expiring_holder[i]);
            counter++;

            pthread_mutex_unlock (&worker_mutex);
        } else {
            logger->debug (__FILE__, __LINE__, "NO Expiring %s, duration: %.2f, timeout: %.2f",
                           expiring_holder[i]->remote_ip, duration, timeout);
        }
    }

    if (counter > 0) {
        logger->notice (__FILE__, __LINE__, "Expire %d entries (entries = %d)", counter, number_of_entries);
    } else {
        logger->trace (__FILE__, __LINE__, "NO entry to be expired (entries = %d)", number_of_entries);
    }

    if (tv->tv_sec / 60 % 15 == 0) {
        tell_time (*global_vars.app_boot_time);
    }
}

static int init_socket (int port) {
    int fd;
    struct sockaddr_in6 myaddr;
    socklen_t myaddrLen = sizeof myaddr;
    char on = 1;
    struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;

    if ((fd = socket (PF_INET6, SOCK_STREAM, 0)) < 0) {
        perror ("socket");
        return -1;
    }

    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof on);
    setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, sizeof on);

    bzero (&myaddr, sizeof myaddr);
    myaddr.sin6_family = AF_INET6;
    myaddr.sin6_addr = in6addr_any;
    myaddr.sin6_port = htons (port);

    if (bind (fd, (struct sockaddr *) &myaddr, myaddrLen) < 0) {
        perror ("bind");
        close (fd);
        fd = -1;
    } else {
        if (getsockname (fd, (struct sockaddr *) &myaddr, &myaddrLen) < 0) {
            perror ("getsockname");
            close (fd);
            fd = -1;
        }
    }

    return fd;
}

static int init_socket_v4 (int port) {
    int fd;
    struct sockaddr_in myAddress;
    socklen_t myAddressLen = sizeof myAddress;
    char on = 1;

    if ((fd = socket (PF_INET, SOCK_STREAM, 0)) < 0) {
        perror ("socket");
        return -1;
    }

    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof on);
    setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &on, sizeof on);

    bzero (&myAddress, sizeof myAddress);
    myAddress.sin_family = AF_INET;
    myAddress.sin_addr.s_addr = INADDR_ANY;
    myAddress.sin_port = htons (port);

    if (bind (fd, (struct sockaddr *) &myAddress, myAddressLen) < 0) {
        logger->error (__FILE__, __LINE__, "bind (%s): %s", __FUNCTION__, strerror (errno));
        close (fd);
        fd = -1;
    } else {
        if (getsockname (fd, (struct sockaddr *) &myAddress, &myAddressLen) < 0) {
            logger->error (__FILE__, __LINE__, "getsockname (%s): %s", __FUNCTION__, strerror (errno));
            close (fd);
            fd = -1;
        }
    }

    return fd;
}

static int connect_host (const char *ip, const int port) {
    int fd;
    struct sockaddr_in remoteAddress;
    struct hostent *hp;
    int len;

    if ((fd = init_socket_v4 (0)) < 0) return -1;

    memset (&remoteAddress, 0, sizeof (remoteAddress));

    if (inet_pton (AF_INET, ip, &remoteAddress.sin_addr) != 0) {
        remoteAddress.sin_family = AF_INET;
    } else if ((hp = gethostbyname (ip)) != NULL) {
        remoteAddress.sin_family = hp->h_addrtype;
        len = hp->h_length;

        if (len > sizeof remoteAddress.sin_addr) {
            len = sizeof remoteAddress.sin_addr;
        }

        memcpy (&remoteAddress.sin_addr, hp->h_addr_list[0], len);
    } else {
        perror (ip);
        return -1;
    }

    remoteAddress.sin_port = htons (port);

    if (connect (fd, (struct sockaddr *) &remoteAddress, sizeof remoteAddress) < 0) {
        logger->error (__FILE__, __LINE__, "connect to [%s:%d]: %s", ip, port, strerror (errno));
        close (fd);
        return -1;
    }

    return fd;
}

static bool check_remote_ip_in_whitelist (const char *remote_ip) {
    static int list_size = -1;
    static char **white_list = NULL;
    int i;

    if (list_size == -1) {
        list_size = 0;
        white_list = system_conf->string_list ("white-list-ip-prefix", &list_size);
    }

    for (i = 0; i < list_size; i++) {
        if (strncasecmp (remote_ip, white_list[i], strlen (white_list[i])) == 0) {
            return true;
        }
    }
    return false;
}


static void do_proxying (const int source, const int destination, struct connection_info *info) {
    char buffer[32768];
//    static pthread_mutex_t proxying_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock (&info->mutex);
    if (info->in_chain) {
        gettimeofday (&info->recent, NULL);
        const ssize_t len = read (source, buffer, sizeof buffer);
        const bool fromClient = info->client_fd == source;
        bool close_connection = false;

        if (len > 0) {
            ssize_t writeTotal = 0;
            size_t leftLen = len;

            if (logger->getPriority() >= log_trace) {
                if (fromClient) {
                    logger->trace (__FILE__, __LINE__, "proxying %d -> %d, size: %ld, [ from: %s ]",
                                   source, destination, len, info->remote_ip);
                } else {
                    logger->trace (__FILE__, __LINE__, "proxying %d -> %d, size: %ld, [ to: %s ]",
                                   source, destination, len, info->remote_ip);
                }
            }

            packetAnalyzer->analyze_packet (info, fromClient, buffer, len);

            if (fromClient && info->requestCount > max_allowed_requests) {
                close_connection = true;
                logger->warning (__FILE__, __LINE__, "close connection for [ %s ]: sending too many requests (%d times)",
                                 info->remote_ip, info->requestCount + 1);
            } else {
                while (leftLen > 0) {
                    ssize_t writeLen = write (destination, &buffer[writeTotal], leftLen);

                    if (writeLen > 0) {
                        leftLen -= writeLen;
                        writeTotal += writeLen;
                    } else {
                        close_connection = true;
                        logger->warning (__FILE__, __LINE__, "Failed to write to destination: %s, len = %d [ %s %s ]",
                                         strerror (errno),
                                         writeLen,
                                         info->client_fd == source ? "from" : "to",
                                         info->remote_ip);
                        break;
                    }
                }

                if (fromClient) {
                    info->bytesSent += writeTotal;
                    info->requestCount++;
                } else {
                    info->bytesReceived += writeTotal;
                    info->responseCount++;
                }
            }
        } else {
            close_connection = true;
        }

        if (close_connection) {
            detach_connection_info_entry (info);
            close_event (info, false);
            if (!info->in_chain) {
                free_connection_info (info);
            }
        }
    }
    pthread_mutex_unlock (&info->mutex);
}

static void proxy_from_server_to_client (const int fd, void *args) {
    struct connection_info *info = args;

    pthread_mutex_lock (&worker_mutex);
    do_proxying (info->server_fd, info->client_fd, info);
    pthread_mutex_unlock (&worker_mutex);
}

static void proxy_from_client_to_server (const int fd, void *args) {
    struct connection_info *info = args;

    pthread_mutex_lock (&worker_mutex);
    do_proxying (info->client_fd, info->server_fd, info);
    pthread_mutex_unlock (&worker_mutex);
}

static void accepting_request (const int fdc, const int64_t connection_id) { // {{{
    struct sockaddr_in6 rmaddr;
    socklen_t rmaddrLen = sizeof rmaddr;
    char str[INET6_ADDRSTRLEN];
    struct in_addr remote_ipv4_address;
    bool is_v4 = false;


    if (getpeername (fdc, (struct sockaddr *) &rmaddr, &rmaddrLen) != 0) {
        logger->debug (__FILE__, __LINE__, "getpeername (%s): %s", __FUNCTION__, strerror (errno));
        close (fdc);
        return;
    }


    pthread_mutex_lock (&worker_mutex);
    uint32_t *ptr = (uint32_t *) &rmaddr.sin6_addr;

    if (rmaddr.sin6_family == PF_INET6 && *ptr == 0 && * (ptr + 1) == 0 && * (ptr + 2) == 0xffff0000) {
        remote_ipv4_address.s_addr = * (ptr + 3);
        is_v4 = true;
    }

    const char *remote_ip = inet_ntop (AF_INET6, & (rmaddr.sin6_addr), str, INET6_ADDRSTRLEN);

    struct db_proxy_request_t *request_in_db = db_svc->check_available (remote_ip);
    int channel = -1;
    bool blacklisted = false;
    bool auto_blacklisted = false;
    int access_counter = 0;
    struct ip_access_entry_t *entry = NULL;

    if (request_in_db != NULL) {
        channel = request_in_db->channel;
    } else if (check_remote_ip_in_whitelist (remote_ip)) {
        channel = default_server;

        if (db_svc->connection_blacklisted (remote_ip) > 0) {
            blacklisted = true;
            channel = -1;
        }

        if (is_v4) {
            entry = blacklistService->find_and_increase (&remote_ipv4_address);

            if (entry != NULL) {
                access_counter = entry->counter;
            }

            if (!blacklisted) {
                if (on_failed_channel != default_server &&
                        access_counter > connection_threshold - 7 &&
                        access_counter <= connection_threshold &&
                        access_counter % 2 == 0 &&
                        db_svc->fail_guessing (remote_ip)) {
                    channel = on_failed_channel;
                    logger->notice (__FILE__, __LINE__,
                                    "%s: failure detected on %s, try channel: %d", inet_ntoa (remote_ipv4_address), remote_ip, channel);
                }

                if (access_counter > connection_threshold) {
                    if (db_svc->check_vip (remote_ip) == 0) {
                        if (access_counter > persist_threshold) {
                            if (db_svc->add_ip_to_auto_blacklist (remote_ip) > 0) {
                                logger->notice (__FILE__, __LINE__,
                                                "%s: threshold reached, add to blacklist database",
                                                inet_ntoa (remote_ipv4_address));
                            }
                        }
                        auto_blacklisted = true;
                        channel = -1;
                    }
                }

                if (entry != NULL && !auto_blacklisted && channel >= 0) {
                    struct timeval now;
                    gettimeofday (&now, NULL);
                    long elapsed = (long) elapsed_time (&now, &entry->log_time);

                    if (elapsed > 86400L * 2) {
                        logger->notice (__FILE__, __LINE__,
                                        "%s: elapsed time: %.2f day, connection: %d, success: %d",
                                        inet_ntoa (remote_ipv4_address), elapsed / 86400.,
                                        entry->counter, entry->success_counter);
                    }

                    if (elapsed > max_persistent_time) {
                        if (db_svc->check_vip (remote_ip) == 0) {
                            if (db_svc->add_ip_to_auto_blacklist (remote_ip) > 0) {
                                logger->notice (__FILE__, __LINE__,
                                                "%s: persistent connection threshold reached, add to blacklist database",
                                                inet_ntoa (remote_ipv4_address));
                            }
                        }
                    }
                }
            }
        }
    }

    if (channel >= 0) {
        channel = channel < number_of_remote_servers ? channel : 0;

        if (request_in_db != NULL) {
            logger->notice (__FILE__, __LINE__,
                            "Connect from [%ld]: %s (%d) [account: %s, channel: %d]",
                            connection_id,
                            remote_ip,
                            ntohs (rmaddr.sin6_port),
                            request_in_db->account != NULL ? request_in_db->account : "(null)",
                            channel);
        } else {
            logger->debug (__FILE__, __LINE__,
                           "Connect from [%ld]: %s (%d) [channel: %d]",
                           connection_id,
                           remote_ip,
                           ntohs (rmaddr.sin6_port),
                           channel);
            if (entry != NULL) {
                entry->success_counter++;
            }
        }
        int proxy_fd = connect_host (remote_servers[channel].host, remote_servers[channel].port);

        if (proxy_fd >= 0) {
            struct connection_info *info = allocate_connection_info();

            info->client_fd = fdc;
            info->server_fd = proxy_fd;
            info->requestCount = 0;
            info->responseCount = 0;
            info->bytesSent = 0L;
            info->bytesReceived = 0L;
            info->connection_id = connection_id;
            info->request_in_db = request_in_db;
            info->in_chain = false;
            info->insert_id = 0;
            info->remote_ip = strdup (remote_ip);
            info->attempts = access_counter;
            info->packet_analyzer_data = packetAnalyzer->allocate();
            gettimeofday (&info->started, NULL);
            gettimeofday (&info->recent, NULL);
            pthread_mutex_init (&info->mutex, NULL);

            info->client_handle = ev->add_event (info->client_fd, proxy_from_client_to_server, info);
            info->server_handle = ev->add_event (info->server_fd, proxy_from_server_to_client, info);

            if (request_in_db != NULL) {
                info->insert_id = db_svc->connection_established (request_in_db->sn, request_in_db->account, remote_ip);
                info->nth_user = ++user_counter;
            }

            attach_connection_info_entry (info);
        } else {
            shutdown (fdc, SHUT_RDWR);
            close (fdc);
            logger->info (__FILE__, __LINE__,
                          "Connect from [%ld]: %s (%d) [ %s:%d - remote server not responding ]",
                          connection_id,
                          remote_ip,
                          ntohs (rmaddr.sin6_port),
                          remote_servers[channel].host, remote_servers[channel].port);

            free_proxy_request_data (request_in_db);
        }
    } else {

        if (auto_blacklisted) {
            shutdown (fdc, SHUT_RDWR);
            close (fdc);

            logger->notice (__FILE__, __LINE__,
                            "Block connection from: %s [ %d attempts, Auto blacklist ]",
                            remote_ip, access_counter);
        } else if (blacklisted) {
            shutdown (fdc, SHUT_RDWR);
            close (fdc);

            bool notice = false;

            if (entry != NULL) {
                struct timeval tv;
                gettimeofday (&tv, NULL);
                if (elapsed_time (&tv, &entry->log_time) > 1800.) {
                    notice = true;
                    gettimeofday (&entry->log_time, NULL);
                }
            }

            logger->log (__FILE__, __LINE__, notice ? log_notice : log_debug,
                         "Block connection from: %s [ %d attempts, blacklisted ]",
                         remote_ip, access_counter);
        } else {
            int port = ntohs (rmaddr.sin6_port);

            logger->trace (__FILE__, __LINE__,
                           "Connect from [%ld]: %s (%d) [ drop ]",
                           connection_id,
                           remote_ip,
                           port);

            shutdown (fdc, SHUT_RDWR);
            close (fdc);
        }
        db_svc->connection_not_allowed (remote_ip);
    }
    pthread_mutex_unlock (&worker_mutex);
}

static void main_listener (const int fd, void *args) {
    struct sockaddr_in6 client_addr;
    socklen_t client_len = sizeof client_addr;

    int conn_sock = accept (fd, (struct sockaddr *) &client_addr, &client_len);

    if (conn_sock == -1) {
        perror ("accept");
    } else {
        logger->trace (__FILE__, __LINE__, "accept (%d) [fd=%d]", conn_sock, fd);

        accepting_request (conn_sock, ++connection_counter);
    }
}

static void *proxy_main (void *args) {
    struct application_context_t *application_context = get_application_context();

    system_conf = (struct system_config_t *) application_context->get_bean (SYSTEM_CONFIG_DEFAULT_CONTEXT_NAME);
    blacklistService = (struct auto_blacklist_service_t *) application_context->get_bean (AUTO_BLACKLIST_DEFAULT_CONTEXT_NAME);
    packetAnalyzer = (struct packet_analyzer_t *) application_context->get_bean (PACKET_ANALYZER_DEFAULT_CONTEXT_NAME);
    db_svc = (struct database_service_t *) application_context->get_bean (DATABASE_SERVICE_DEFAULT_CONTEXT_NAME);


    ev = new_event_loop (logger);

    const int port = system_conf->int_or_default ("port", 80);
    default_server = system_conf->int_or_default ("default-server", 0);
    number_of_remote_servers = 0;
    char **servers = system_conf->string_list ("servers", &number_of_remote_servers);

    connection_threshold = system_conf->int_or_default ("threshold", 5);
    persist_threshold = system_conf->int_or_default ("persist-threshold", 5);
    max_persistent_time = system_conf->int_or_default ("max-persistent-day", 5) * 86400L;
    max_allowed_requests = system_conf->int_or_default ("max-allowed-requests", 6);

    on_failed_channel = system_conf->int_or_default ("on-failed-channel", 0);
    db_svc->reload_product_names ();

    if (number_of_remote_servers > 0) {
        int i, j, errornumber;
        PCRE2_SIZE erroroffset;
        const char *pattern = "^\\s*(.*):(\\d+)\\s*$";

        pcre2_code *re = pcre2_compile (
                             (PCRE2_SPTR) pattern,  /* the pattern */
                             PCRE2_ZERO_TERMINATED, /* indicates pattern is zero-terminated */
                             0,                     /* default options */
                             &errornumber,          /* for error number */
                             &erroroffset,          /* for error offset */
                             NULL);                 /* use default compile context */

        if (re == NULL) {
            PCRE2_UCHAR buffer[256];

            pcre2_get_error_message (errornumber, buffer, sizeof (buffer));
            logger->error (__FILE__, __LINE__, "PCRE2 compilation failed at offset %d: %s\n", (int) erroroffset, buffer);
            return NULL;
        }

        remote_servers = malloc (number_of_remote_servers * sizeof (struct remote_server_t));

        for (i = 0; i < number_of_remote_servers; i++) {
            pcre2_match_data *match_data = pcre2_match_data_create_from_pattern (re, NULL);
            PCRE2_SPTR subject = (PCRE2_SPTR) servers[i];

            int rc = pcre2_match (
                         re,                   /* the compiled pattern */
                         subject,              /* the subject string */
                         strlen (servers[i]), /* the length of the subject */
                         0,                    /* start at offset 0 in the subject */
                         0,                    /* default options */
                         match_data,           /* block for storing the result */
                         NULL);

            if (rc < 0) {
                switch (rc) {
                case PCRE2_ERROR_NOMATCH:
                    logger->error (__FILE__, __LINE__, "No match");
                    break;
                /*
                Handle other special cases if you like
                */
                default:
                    logger->error (__FILE__, __LINE__, "Matching error %d", rc);
                    break;
                }
                pcre2_match_data_free (match_data); /* Release memory used for the match */
                pcre2_code_free (re); /* data and the compiled pattern. */
                return NULL;
            } else {
                PCRE2_SIZE *ovector = pcre2_get_ovector_pointer (match_data);

                for (j = 0; j < rc; j++) {
                    PCRE2_SPTR substring_start = subject + ovector[2 * j];
                    size_t substring_length = ovector[2 * j + 1] - ovector[2 * j];

                    switch (j) {
                    case 1:
                        remote_servers[i].host = strndup ((char *) substring_start, substring_length);
                        break;
                    case 2:
                        remote_servers[i].port = atoi ((char *) substring_start);
                        break;
                    }
                }
                logger->debug (__FILE__, __LINE__, "host: %s, port: %d", remote_servers[i].host,
                               remote_servers[i].port);
            }
            pcre2_match_data_free (match_data);
        }
        pcre2_code_free (re);
    }

    fprintf (stderr, "Listen on: %d\n", port);

    int sockfd = init_socket (port);

    if (sockfd >= 0) {
        listen (sockfd, 5);

        int index = ev->add_event (sockfd, main_listener, NULL);

        while (!system_conf->terminated() && ev->looping() >= 0) {
        }

        logger->warning (__FILE__, __LINE__, "proxying terminated");

        system_conf->terminate();

        struct timeval tv;
        gettimeofday (&tv, NULL);

        clean_idle_connections (&tv, -1.0);
        ev->remove_event (index);
        shutdown (sockfd, SHUT_RDWR);
        close (sockfd);
    } else {
        system_conf->terminate();
        exit (EXIT_FAILURE);
    }

    return NULL;
}

static int get_default_channel (void) {
    return default_server;
}

static int get_fallback_channel (void) {
    return on_failed_channel;
}

static int set_default_channel (const int channel) {
    if (channel >= 0 && channel < number_of_remote_servers) {
        default_server = channel;
        logger->notice (__FILE__, __LINE__, "Default channel set to %d", channel);
    } else {
        logger->notice (__FILE__, __LINE__, "Failed to set default channel %d (out of range)", channel);
    }
    return default_server;
}

static int set_fallback_channel (const int channel) {
    if (channel >= 0 && channel < number_of_remote_servers) {
        on_failed_channel = channel;
        logger->notice (__FILE__, __LINE__, "Fallback channel set to %d", channel);
    } else {
        logger->notice (__FILE__, __LINE__, "Failed to set fallback channel %d (out of range)", channel);
    }
    return on_failed_channel;
}

static int start_proxying (pthread_t *thread) {
    return pthread_create (thread, NULL, proxy_main, NULL);
}

static const char *const context_name (void) {
    return PROXYING_SERVICE_DEFAULT_CONTEXT_NAME;
}

static void post_construct (void) {
    logger->trace (__FILE__, __LINE__, "%s:%d %s", __FILE__, __LINE__, __FUNCTION__ );
}

static struct proxying_service_t instance = {
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
    .start_proxying = start_proxying,
    .clean_idle_connection = clean_idle_connections,
    .set_default_channel = set_default_channel,
    .set_fallback_channel = set_fallback_channel,
    .get_default_channel = get_default_channel,
    .get_fallback_channel = get_fallback_channel,
};

struct proxying_service_t * init_proxying_service () {
    logger = get_application_context()->get_logger ();
    return &instance;
}
