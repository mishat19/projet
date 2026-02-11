#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>   // Files de messages POSIX
#include <unistd.h>
#include <syslog.h>

#include "common.h"

int main() {
    // Ouverture du log système pour ce processus (worker)
    // LOG_PID : inclut le PID dans les logs
    // LOG_USER : facilité de log pour les processus utilisateur
    openlog("worker", LOG_PID, LOG_USER);

    // Ouverture de la file de messages POSIX en mode lecture seule
    mqd_t mq = mq_open(MQ_NAME, O_RDONLY);
    if (mq == -1) {
        syslog(LOG_ERR, "Impossible d'ouvrir la file de messages");
        exit(EXIT_FAILURE);
    }

    while (1) {
        file_event_t msg;
        // Réception d'un message depuis la file de messages
        mq_receive(mq, (char *)&msg, sizeof(msg), NULL);

        // Log de l'événement reçu
        syslog(LOG_INFO, "Traitement du fichier %s (type %d)", msg.filepath, msg.event_type);

        // Simulation d'un traitement
        sleep(1);
    }

    // Fermeture de la file de messages
    mq_close(mq);
    closelog();
    return 0;
}
