#ifndef TODO_DB_H
#define TODO_DB_H

#include <stdlib.h>

int db_init(const char *path);
void db_close(void);
int db_add_task(const char *title, const char *description, const char *status, int priority, int *out_id);
char *db_list_tasks(void); /* caller frees; format: id|title|description|status|priority|created_at|updated_at\n */
int db_update_task(int id, const char *title, const char *description, const char *status, int priority);
int db_delete_task(int id);
int db_export_csv(const char *path);
int db_import_csv(const char *path);

#endif /* TODO_DB_H */
