#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <unistd.h>
#include <syslog.h>

#include "common.h"

int main() {
    openlog("worker", LOG_PID, LOG_USER);

    mqd_t mq = mq_open(MQ_NAME, O_RDONLY);
    if (mq == -1) {
        syslog(LOG_ERR, "Impossible d'ouvrir la file de messages");
        exit(EXIT_FAILURE);
    }

    while (1) {
        file_event_t msg;
        mq_receive(mq, (char *)&msg, sizeof(msg), NULL);

        // Simulation de traitement
        syslog(LOG_INFO, "Traitement du fichier %s (type %d)",
               msg.filepath, msg.event_type);

        sleep(1); // traitement simulé
    }

    mq_close(mq);
    closelog();
    return 0;
}
