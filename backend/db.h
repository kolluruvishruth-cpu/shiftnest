#ifndef SHIFTNEST_DB_H
#define SHIFTNEST_DB_H
#include "optimizer.h"
#include <cjson/cJSON.h>
#include <libpq-fe.h>
extern PGconn *db;
extern char db_error[200];
PGresult *query(const char *sql, int n, const char *const *args);
int command(const char *sql, int n, const char *const *args);
cJSON *rows(const char *sql, int n, const char *const *args);
int load_problem(const char *week, Problem *p, long long *revision);
cJSON *result_json(const Result *r);
int migrate(void);
int seed(void);
char *read_file(const char *path, size_t *size);
#endif
