#ifndef COMMON_H
#define COMMON_H

#define MAX_PATH 256
#define MQ_NAME "/file_events_mq"

// Types d'événements
#define EVENT_CREATE 1
#define EVENT_MODIFY 2

// Structure envoyée dans la file de messages
typedef struct {
    char filepath[MAX_PATH];
    int event_type;
} file_event_t;

#endif
