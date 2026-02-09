#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <sys/inotify.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>

#include "common.h"

#define WATCH_DIR "/home/mathis/Documents/Januera/projet/watch"
#define EVENT_BUF_LEN 1024

static volatile int running = 1;
mqd_t mq;

/* Gestion propre de SIGTERM */
void handle_signal(int sig) {
    syslog(LOG_INFO, "Daemon arrêté (signal %d)", sig);
    running = 0;
}

/* Transformation en daemon */
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    setsid();
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main() {
    daemonize();

    openlog("file_daemon", LOG_PID, LOG_DAEMON);

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    /* Initialisation de la file de messages */
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = sizeof(file_event_t),
        .mq_curmsgs = 0
    };

    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == -1) {
        syslog(LOG_ERR, "mq_open erreur");
        exit(EXIT_FAILURE);
    }

    /* Initialisation inotify */
    int fd = inotify_init();
    if (fd < 0) {
        syslog(LOG_ERR, "Erreur inotify_init: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int wd = inotify_add_watch(fd, WATCH_DIR, IN_CREATE | IN_MODIFY);
    if (wd < 0) {
        syslog(LOG_ERR, "Erreur inotify_add_watch sur %s: %s",
            WATCH_DIR, strerror(errno));
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Daemon lancé, surveillance de %s", WATCH_DIR);

    char buffer[EVENT_BUF_LEN];

    while (running) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) continue;

        struct inotify_event *event = (struct inotify_event *)buffer;

        if (event->len) {
            file_event_t msg;
            snprintf(msg.filepath, MAX_PATH, "%s/%s", WATCH_DIR, event->name);

            if (event->mask & IN_CREATE)
                msg.event_type = EVENT_CREATE;
            else if (event->mask & IN_MODIFY)
                msg.event_type = EVENT_MODIFY;
            else
                continue;

            mq_send(mq, (char *)&msg, sizeof(msg), 0);
            syslog(LOG_INFO, "Événement détecté : %s", msg.filepath);
        }
    }

    mq_close(mq);
    mq_unlink(MQ_NAME);
    closelog();
    return 0;
}
