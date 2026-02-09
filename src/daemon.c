#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "../include/db.h"
#include "../include/ipc.h"

#define SOCKET_PATH "/tmp/todo.sock"
#define BACKLOG 8

static int listen_fd = -1;
static volatile int running = 1;

/* subscribers */
static int *subs = NULL;
static size_t subs_count = 0;
static pthread_mutex_t subs_lock = PTHREAD_MUTEX_INITIALIZER;

void notify_subscribers(const char *msg) {
    pthread_mutex_lock(&subs_lock);
    for (size_t i = 0; i < subs_count; ) {
        int fd = subs[i];
        ssize_t n = send(fd, msg, strlen(msg), 0);
        if (n <= 0) {
            close(fd);
            subs[i] = subs[subs_count-1];
            subs_count--;
            // shrink array
            int *tmp = realloc(subs, subs_count * sizeof(int));
            if (subs_count == 0) { free(tmp); subs = NULL; } else if (tmp) subs = tmp;
        } else {
            i++;
        }
    }
    pthread_mutex_unlock(&subs_lock);
}

void add_subscriber(int fd) {
    pthread_mutex_lock(&subs_lock);
    int *tmp = realloc(subs, (subs_count+1) * sizeof(int));
    if (!tmp) {
        pthread_mutex_unlock(&subs_lock);
        return;
    }
    subs = tmp;
    subs[subs_count++] = fd;
    pthread_mutex_unlock(&subs_lock);
}

void handle_client(int client_fd) {
    FILE *f = fdopen(client_fd, "r+");
    if (!f) {
        close(client_fd);
        return;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        // strip newline
        char *nl = strchr(buf, '\n');
        if (nl) {
            *nl = '\0';
        }
        if (strncmp(buf, "ADD ", 4) == 0) {
            /* ADD title|description|status|priority */
            char *payload = buf + 4;
            char *p1 = strchr(payload, '|');
            char *title = payload;
            char *desc = "";
            char *status = "pending";
            int pr = 0;
            if (p1) {
                *p1 = '\0';
                desc = p1 + 1;
                char *p2 = strchr(desc, '|');
                if (p2) {
                    *p2 = '\0';
                    status = p2 + 1;
                    char *p3 = strchr(status, '|');
                    if (p3) {
                        *p3 = '\0';
                        pr = atoi(p3 + 1);
                    }
                }
            }
            int id = 0;
            if (db_add_task(title, desc, status, pr, &id) == 0) {
                dprintf(client_fd, "OK %d\n", id);
                char notify[512];
                snprintf(notify, sizeof(notify), "NOTIFY ADDED %d|%s|%s|%s|%d\n", id, title, desc, status, pr);
                notify_subscribers(notify);
            } else {
                dprintf(client_fd, "ERR\n");
            }
        } else if (strcmp(buf, "LIST") == 0) {
            char *list = db_list_tasks();
            if (list) {
                dprintf(client_fd, "%s", list);
                free(list);
            } else dprintf(client_fd, "ERR\n");
        } else if (strncmp(buf, "UPDATE ", 7) == 0) {
            /* UPDATE id|title|description|status|priority */
            char *payload = buf + 7;
            char *p = strchr(payload, '|');
            if (!p) { dprintf(client_fd, "ERR\n"); }
            else {
                *p = '\0';
                int id = atoi(payload);
                char *title = p+1;
                char *desc = "";
                char *status = "pending";
                int pr = 0;
                char *p2 = strchr(title, '|');
                if (p2) {
                    *p2 = '\0';
                    desc = p2 + 1;
                    char *p3 = strchr(desc, '|');
                    if (p3) {
                        *p3 = '\0';
                        status = p3 + 1;
                        char *p4 = strchr(status, '|');
                        if (p4) {
                            *p4 = '\0';
                            pr = atoi(p4 + 1);
                        }
                    }
                }
                if (db_update_task(id, title, desc, status, pr) == 0) {
                    dprintf(client_fd, "OK\n");
                    char notify[512];
                    snprintf(notify, sizeof(notify), "NOTIFY UPDATED %d\n", id);
                    notify_subscribers(notify);
                } else dprintf(client_fd, "ERR\n");
            }
        } else if (strncmp(buf, "DELETE ", 7) == 0) {
            int id = atoi(buf+7);
            if (db_delete_task(id) == 0) {
                dprintf(client_fd, "OK\n");
                char notify[128];
                snprintf(notify, sizeof(notify), "NOTIFY DELETED %d\n", id);
                notify_subscribers(notify);
            } else dprintf(client_fd, "ERR\n");
        } else if (strcmp(buf, "SUBSCRIBE") == 0) {
            // client wants notifications; add and keep connection open
            add_subscriber(client_fd);
            // Do not close; hand over control to subscription loop: block until socket closed
            // We'll just wait for EOF on the stream
            while (fgets(buf, sizeof(buf), f)) { /* ignore further input */ }
            // connection closed
            break;
        } else {
            dprintf(client_fd, "UNKNOWN\n");
        }
    }
    fclose(f);
}

void *client_thread(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);
    handle_client(client_fd);
    return NULL;
}

void sigint_handler(int sig) {
    (void)sig;
    running = 0;
    if (listen_fd != -1) close(listen_fd);
    unlink(SOCKET_PATH);
}

int main(int argc, char **argv) {
    const char *dbpath = "todo.db";
    if (argc > 1) dbpath = argv[1];
    if (db_init(dbpath) != 0) {
        fprintf(stderr, "Failed to init DB\n");
        return 1;
    }
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    listen_fd = create_unix_server_socket(SOCKET_PATH, BACKLOG);
    if (listen_fd == -1) {
        fprintf(stderr, "Failed to create socket\n");
        db_close();
        return 1;
    }
    printf("Daemon listening on %s\n", SOCKET_PATH);
    while (running) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) break;
        *client_fd = accept(listen_fd, NULL, NULL);
        if (*client_fd == -1) {
            free(client_fd);
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, (void*(*)(void*))client_thread, client_fd);
        pthread_detach(tid);
    }

    // cleanup
    if (listen_fd != -1) close(listen_fd);
    unlink(SOCKET_PATH);
    db_close();
    return 0;
}
