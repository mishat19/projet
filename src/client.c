#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

#define SOCKET_PATH "/tmp/todo.sock"

static int connect_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return -1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) { perror("connect"); close(fd); return -1; }
    return fd;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args]\nCommands:\n  add title [description] [status] [priority]\n  list\n  watch\n  export <file> <csv|json>\n  import <file>\n  delete <id>\n  update id title description status priority\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "watch") == 0) {
        /* create named semaphore to synchronize printing */
        const char *semname = "/todo_watch_sem";
        sem_unlink(semname);
        sem_t *sem = sem_open(semname, O_CREAT | O_EXCL, 0600, 1);
        if (sem == SEM_FAILED) { perror("sem_open"); return 1; }

        pid_t pid = fork();
        if (pid == -1) { perror("fork"); sem_close(sem); sem_unlink(semname); return 1; }
        if (pid == 0) {
            /* child: subscribe and print notifications */
            sem_t *csem = sem_open(semname, 0);
            if (csem == SEM_FAILED) { perror("sem_open child"); _exit(1); }
            int sfd = connect_socket();
            if (sfd == -1) _exit(1);
            send(sfd, "SUBSCRIBE\n", 10, 0);
            char buf[1024];
            ssize_t n;
            while ((n = read(sfd, buf, sizeof(buf)-1)) > 0) {
                buf[n] = '\0';
                sem_wait(csem);
                printf("%s", buf);
                fflush(stdout);
                sem_post(csem);
            }
            close(sfd);
            sem_close(csem);
            _exit(0);
        } else {
            /* parent: interactive input while child prints notifications */
            sem_t *psem = sem_open(semname, 0);
            if (psem == SEM_FAILED) { perror("sem_open parent"); return 1; }
            char line[1024];
            while (1) {
                sem_wait(psem);
                printf("todo> "); fflush(stdout);
                if (!fgets(line, sizeof(line), stdin)) { sem_post(psem); break; }
                sem_post(psem);
                /* trim newline */
                char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
                if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;
                /* send as raw command to daemon */
                int cfd = connect_socket();
                if (cfd == -1) break;
                send(cfd, line, strlen(line), 0);
                send(cfd, "\n", 1, 0);
                char resp[1024];
                ssize_t r;
                r = read(cfd, resp, sizeof(resp)-1);
                if (r > 0) {
                    resp[r] = '\0';
                    sem_wait(psem);
                    printf("%s", resp);
                    sem_post(psem);
                }
                close(cfd);
            }
            /* cleanup */
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            sem_close(psem);
            sem_unlink(semname);
            return 0;
        }
    }

    /* other commands: connect per-request */
    int fd = connect_socket();
    if (fd == -1) return 1;
    if (strcmp(argv[1], "add") == 0 && argc >= 3) {
        const char *title = argv[2];
        const char *desc = (argc >= 4) ? argv[3] : "";
        const char *status = (argc >= 5) ? argv[4] : "pending";
        int pr = (argc >= 6) ? atoi(argv[5]) : 0;
        char buf[2048]; snprintf(buf, sizeof(buf), "ADD %s|%s|%s|%d\n", title, desc, status, pr);
        send(fd, buf, strlen(buf), 0);
        char resp[256];
        ssize_t n;
        n = read(fd, resp, sizeof(resp)-1);
        if (n > 0) {
            resp[n] = '\0';
            printf("%s", resp);
        }
    } else if (strcmp(argv[1], "list") == 0) {
        send(fd, "LIST\n", 5, 0);
        char resp[16384];
        ssize_t n;
        n = read(fd, resp, sizeof(resp)-1);
        if (n > 0) {
            resp[n] = '\0';
            printf("%s", resp);
        }
    } else if (strcmp(argv[1], "delete") == 0 && argc >= 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "DELETE %s\n", argv[2]);
        send(fd, buf, strlen(buf), 0);
        char r[64];
        ssize_t n;
        n = read(fd, r, sizeof(r)-1);
        if (n > 0) {
            r[n] = '\0';
            printf("%s", r);
        }
    } else if (strcmp(argv[1], "update") == 0 && argc >= 6) {
        /* update id title description status priority */
        char buf[2048];
        snprintf(buf, sizeof(buf), "UPDATE %s|%s|%s|%s|%s\n", argv[2], argv[3], argv[4], argv[5], argv[6]);
        send(fd, buf, strlen(buf), 0);
        char r[128];
        ssize_t n;
        n = read(fd, r, sizeof(r)-1);
        if (n > 0) {
            r[n] = '\0';
            printf("%s", r);
        }
    } else if (strcmp(argv[1], "export") == 0 && argc >= 4) {
        /* ask LIST then write file */
        send(fd, "LIST\n", 5, 0);
        char *buf = malloc(16384);
        ssize_t n = read(fd, buf, 16384-1);
        if (n>0) { buf[n]='\0';
            /* parse lines and write CSV or JSON */
            if (strcmp(argv[3], "csv") == 0) {
                FILE *f = fopen(argv[2], "w"); if (!f) { perror("fopen"); free(buf); close(fd); return 1; }
                fprintf(f, "id,title,description,status,priority,created_at,updated_at\n");
                char *saveptr = NULL; char *line = strtok_r(buf, "\n", &saveptr);
                while (line) {
                    /* line: id|title|description|status|priority|created|updated */
                    char *p = line;
                    for (int i=0;i<7;i++){
                        char *sep = strchr(p, '|');
                        if (sep) { *sep='\0'; }
                        /* quote */
                        fprintf(f, "\"%s\"", p);
                        if (i<6) fprintf(f, ",");
                        if (!sep) break;
                        p = sep+1;
                    }
                    fprintf(f, "\n");
                    line = strtok_r(NULL, "\n", &saveptr);
                }
                fclose(f);
            } else {
                /* simple JSON array */
                FILE *f = fopen(argv[2], "w"); if (!f) { perror("fopen"); free(buf); close(fd); return 1; }
                fprintf(f, "[");
                char *saveptr = NULL; char *line = strtok_r(buf, "\n", &saveptr);
                int first = 1;
                while (line) {
                    if (!first) fprintf(f, ",\n"); first=0;
                    char *p = line;
                    char *cols[7] = {0};
                    for (int i=0;i<7;i++) { cols[i]=p; char *s = strchr(p, '|'); if (s) { *s='\0'; p = s+1; } else break; }
                    fprintf(f, "  {\"id\":%s,\"title\":\"%s\",\"description\":\"%s\",\"status\":\"%s\",\"priority\":%s,\"created_at\":\"%s\",\"updated_at\":\"%s\"}", cols[0]?cols[0] : "0", cols[1]?cols[1] : "", cols[2]?cols[2] : "", cols[3]?cols[3] : "", cols[4]?cols[4] : "0", cols[5]?cols[5] : "", cols[6]?cols[6] : "");
                    line = strtok_r(NULL, "\n", &saveptr);
                }
                fprintf(f, "\n]\n"); fclose(f);
            }
        }
        free(buf);
    } else if (strcmp(argv[1], "import") == 0 && argc >= 3) {
        /* read file and send ADD for each line */
        FILE *f = fopen(argv[2], "r"); if (!f) { perror("fopen"); close(fd); return 1; }
        char line[4096]; /* skip header if csv */
        if (fgets(line, sizeof(line), f)) {
            if (strchr(line, ',') && strstr(line, "title")) {
                /* CSV */
                while (fgets(line, sizeof(line), f)) {
                    /* naive parse: quoted CSV */
                                char *p = line;
                                char *cols[7] = {0};
                                for (int i = 0; i < 7; i++) {
                                    if (*p == '"') {
                                        p++;
                                        cols[i] = p;
                                        char *q = strchr(p, '"');
                                        if (!q) break;
                                        *q = '\0';
                                        p = q + 1;
                                        if (*p == ',') p++;
                                    } else {
                                        cols[i] = p;
                                        char *q = strchr(p, ',');
                                        if (!q) break;
                                        *q = '\0';
                                        p = q + 1;
                                    }
                                }
                    if (cols[1]) {
                        int cfd = connect_socket(); if (cfd==-1) break;
                        char buf[2048]; snprintf(buf, sizeof(buf), "ADD %s|%s|%s|%s\n", cols[1], cols[2]?cols[2]:"", cols[3]?cols[3]:"pending", cols[4]?cols[4]:"0"); send(cfd, buf, strlen(buf), 0); close(cfd);
                    }
                }
            } else {
                /* assume simple JSON not implemented: skip */
            }
        }
        fclose(f);
    } else {
        fprintf(stderr, "Unknown or malformed command\n");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
