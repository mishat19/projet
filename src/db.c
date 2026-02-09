#define _GNU_SOURCE
#include "../include/db.h"
#include <sqlite3.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static sqlite3 *g_db = NULL;
static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

int db_init(const char *path) {
    int rc;
    rc = sqlite3_open(path, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    /* schema: id, title, description, status, priority, created_at, updated_at */
    const char *sql =
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "title TEXT NOT NULL,"
        "description TEXT DEFAULT '',"
        "status TEXT DEFAULT 'pending',"
        "priority INTEGER DEFAULT 0,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";
    char *errmsg = NULL;
    rc = sqlite3_exec(g_db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    /* set busy timeout to help with concurrent access */
    sqlite3_busy_timeout(g_db, 5000);
    return 0;
}

void db_close(void) {
    if (g_db) sqlite3_close(g_db);
    g_db = NULL;
}

int db_add_task(const char *title, const char *description, const char *status, int priority, int *out_id) {
    if (!g_db || !title) return -1;
    int rc;
    pthread_mutex_lock(&g_db_mutex);
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(g_db, "INSERT INTO tasks(title, description, status, priority) VALUES(?,?,?,?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status ? status : "pending", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, priority);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    if (out_id) *out_id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return 0;
}

char *db_list_tasks(void) {
    if (!g_db) return NULL;
    pthread_mutex_lock(&g_db_mutex);
    const char *sql = "SELECT id, title, description, status, priority, created_at, updated_at FROM tasks ORDER BY id";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return NULL;
    }
    size_t cap = 2048;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&g_db_mutex);
        return NULL;
    }
    out[0] = '\0';
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *title = sqlite3_column_text(stmt, 1);
        const unsigned char *desc = sqlite3_column_text(stmt, 2);
        const unsigned char *status = sqlite3_column_text(stmt, 3);
        int pr = sqlite3_column_int(stmt, 4);
        const unsigned char *created = sqlite3_column_text(stmt, 5);
        const unsigned char *updated = sqlite3_column_text(stmt, 6);
        char line[1024];
        int n = snprintf(line, sizeof(line), "%d|%s|%s|%s|%d|%s|%s\n", id,
                         title ? (const char*)title : "",
                         desc ? (const char*)desc : "",
                         status ? (const char*)status : "",
                         pr,
                         created ? (const char*)created : "",
                         updated ? (const char*)updated : "");
        if (len + n + 1 > cap) {
            cap = (len + n + 1) * 2;
            char *tmp = realloc(out, cap);
            if (!tmp) {
                free(out);
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&g_db_mutex);
                return NULL;
            }
            out = tmp;
        }
        memcpy(out + len, line, n);
        len += n;
        out[len] = '\0';
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return out;
}

int db_update_task(int id, const char *title, const char *description, const char *status, int priority) {
    if (!g_db) return -1;
    pthread_mutex_lock(&g_db_mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, "UPDATE tasks SET title = ?, description = ?, status = ?, priority = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status ? status : "pending", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, priority);
    sqlite3_bind_int(stmt, 5, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_delete_task(int id) {
    if (!g_db) return -1;
    pthread_mutex_lock(&g_db_mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, "DELETE FROM tasks WHERE id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&g_db_mutex);
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_mutex);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_export_csv(const char *path) {
    char *list = db_list_tasks();
    if (!list) return -1;
    FILE *f = fopen(path, "w");
    if (!f) {
        free(list);
        return -1;
    }
    fprintf(f, "id,title,description,status,priority,created_at,updated_at\n");
    char *saveptr = NULL;
    char *line = strtok_r(list, "\n", &saveptr);
    while (line) {
        /* line format: id|title|description|status|priority|created_at|updated_at */
        char *p = line;
        for (int i = 0; i < 7; ++i) {
            char *sep = strchr(p, '|');
            if (sep) {
                *sep = '\0';
            }
            /* escape double quotes */
            char *escaped = NULL;
            size_t needed = 0;
            const char *s = p;
            while (*s) {
                if (*s == '"') {
                    needed += 2;
                } else {
                    needed += 1;
                }
                s++;
            }
            escaped = malloc(needed + 3);
            char *dst = escaped;
            *dst++ = '"';
            s = p;
            while (*s) {
                if (*s == '"') {
                    *dst++ = '"';
                    *dst++ = '"';
                } else {
                    *dst++ = *s;
                }
                s++;
            }
            *dst++ = '"'; *dst = '\0';
            fprintf(f, "%s", escaped);
            free(escaped);
            if (i < 6) fprintf(f, ",");
            if (!sep) break;
            p = sep + 1;
        }
        fprintf(f, "\n");
        line = strtok_r(NULL, "\n", &saveptr);
    }
    fclose(f);
    free(list);
    return 0;
}

int db_import_csv(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[4096];
    /* skip header */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    while (fgets(line, sizeof(line), f)) {
        /* naive CSV parse: id,title,description,status,priority,created_at,updated_at */
        char *p = line;
        char *cols[7] = {0};
        for (int i = 0; i < 7; ++i) {
            if (*p == '"') {
                p++;
                cols[i] = p;
                while (*p && !(*p == '"' && (*(p+1)==',' || *(p+1)=='\n' || *(p+1)=='\0'))) p++;
                if (*p == '"') *p++ = '\0';
                if (*p == ',') p++;
            } else {
                cols[i] = p;
                while (*p && *p != ',') p++;
                if (*p == ',') *p++ = '\0';
            }
        }
        if (cols[1]) {
            const char *title = cols[1];
            const char *desc = cols[2] ? cols[2] : "";
            const char *status = cols[3] ? cols[3] : "pending";
            int pr = cols[4] ? atoi(cols[4]) : 0;
            db_add_task(title, desc, status, pr, NULL);
        }
    }
    fclose(f);
    return 0;
}
