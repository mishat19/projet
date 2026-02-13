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
// Taille du buffer pour les événements inotify
#define EVENT_BUF_LEN 1024

// Variable dont la valeur peut changer de manière imprévue (contrôler la boucle principale du démon)
static volatile int running = 1;
// Descripteur de la file de messages POSIX
mqd_t mq;

/*
 * Gestionnaire de signal : permet d'arrêter proprement le démon
 * quand un signal SIGTERM ou SIGINT est reçu.
 */
void handle_signal(int sig) {
    syslog(LOG_INFO, "Daemon arrêté (signal %d)", sig);
    running = 0; // Met fin à la boucle principale
}

/*
 * Fonction pour transformer le processus en démon :
 * - Crée un processus fils et termine le père
 * - Crée une nouvelle session
 * - Change le répertoire courant pour /
 * - Ferme les descripteurs standards (stdin, stdout, stderr)
 */
void daemonize() {
    pid_t pid = fork(); // Création d'un processus
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // Termine le processus père

    setsid();
    chdir("/"); // Change le répertoire courant pour la racine
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main() {
    // Transformation en démon
    daemonize();

    // Ouverture du log système pour le démon
    openlog("file_daemon", LOG_PID, LOG_DAEMON);

    // Configuration des gestionnaires de signaux pour SIGTERM et SIGINT
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    // Configuration des attributs de la file de messages POSIX
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,
        .mq_msgsize = sizeof(file_event_t),
        .mq_curmsgs = 0 // Nombre initial de messages
    };

    // Ouverture ou création de la file de messages POSIX
    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == -1) {
        syslog(LOG_ERR, "mq_open erreur");
        exit(EXIT_FAILURE);
    }

    // Initialisation de inotify pour surveiller les événements de fichiers
    int fd = inotify_init();
    if (fd < 0) {
        syslog(LOG_ERR, "Erreur inotify_init: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Ajout du répertoire à surveiller pour les événements IN_CREATE et IN_MODIFY
    int wd = inotify_add_watch(fd, WATCH_DIR, IN_CREATE | IN_MODIFY);
    if (wd < 0) {
        syslog(LOG_ERR, "Erreur inotify_add_watch sur %s: %s", WATCH_DIR, strerror(errno));
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Daemon lancé, surveillance de %s", WATCH_DIR);

    // Buffer pour stocker les événements inotify
    char buffer[EVENT_BUF_LEN];

    while (running) {
        // Lecture des événements inotify
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) continue;

        // Cast du buffer en structure inotify_event
        struct inotify_event *event = (struct inotify_event *)buffer;

        // Si un événement concerne un fichier (event->len > 0)
        if (event->len) {
            file_event_t msg; // Stockage event à envoyer dans la file de messages
            // Construction du chemin complet du fichier concerné
            snprintf(msg.filepath, MAX_PATH, "%s/%s", WATCH_DIR, event->name);

            // Détermination du type d'événement sur le fichier
            if (event->mask & IN_CREATE)
                msg.event_type = EVENT_CREATE;
            else if (event->mask & IN_MODIFY)
                msg.event_type = EVENT_MODIFY;
            else
                continue;

            // Envoi du message dans la file de messages
            mq_send(mq, (char *)&msg, sizeof(msg), 0);
            syslog(LOG_INFO, "Événement détecté : %s", msg.filepath);
        }
    }

    // Nettoyage avant la fin du programme
    mq_close(mq);
    mq_unlink(MQ_NAME);
    closelog();
    return 0;
}
