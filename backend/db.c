#include "db.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
PGconn *db;
char db_error[200];
PGresult *query(const char *sql, int n, const char *const *args) {
  Oid types[16];
  if (n > 16)
    return NULL;
  for (int i = 0; i < n; i++)
    types[i] = 25;
  PGresult *r = PQexecParams(db, sql, n, types, args, NULL, NULL, 0);
  if (PQresultStatus(r) != PGRES_TUPLES_OK &&
      PQresultStatus(r) != PGRES_COMMAND_OK) {
    const char *state = PQresultErrorField(r, PG_DIAG_SQLSTATE);
    snprintf(db_error, sizeof(db_error),
             "Database rejected this operation (%s). Check input or related "
             "records.",
             state ? state : "connection");
    PQclear(r);
    return NULL;
  }
  return r;
}
int command(const char *sql, int n, const char *const *args) {
  PGresult *r = query(sql, n, args);
  if (!r)
    return 0;
  PQclear(r);
  return 1;
}
cJSON *rows(const char *sql, int n, const char *const *args) {
  PGresult *r = query(sql, n, args);
  if (!r)
    return NULL;
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < PQntuples(r); i++) {
    cJSON *o = cJSON_CreateObject();
    for (int j = 0; j < PQnfields(r); j++) {
      char *v = PQgetvalue(r, i, j);
      Oid t = PQftype(r, j);
      if (PQgetisnull(r, i, j))
        cJSON_AddNullToObject(o, PQfname(r, j));
      else if (t == 16)
        cJSON_AddBoolToObject(o, PQfname(r, j), !strcmp(v, "t"));
      else if (t == 20 || t == 21 || t == 23 || t == 700 || t == 701 ||
               t == 1700)
        cJSON_AddNumberToObject(o, PQfname(r, j), strtod(v, NULL));
      else if (t == 114 || t == 3802) {
        cJSON *x = cJSON_Parse(v);
        cJSON_AddItemToObject(o, PQfname(r, j), x ? x : cJSON_CreateNull());
      } else
        cJSON_AddStringToObject(o, PQfname(r, j), v);
    }
    cJSON_AddItemToArray(a, o);
  }
  PQclear(r);
  return a;
}
char *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  if (n < 0 || n > 4000000) {
    fclose(f);
    return NULL;
  }
  char *s = malloc((size_t)n + 1);
  if (!s) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(s, 1, (size_t)n, f);
  s[got] = 0;
  fclose(f);
  if (size)
    *size = got;
  return s;
}
int migrate(void) {
  const char *files[] = {"database/001_init.sql",
                         "database/002_requirement_cancellation.sql",
                         "database/003_custom_requirements.sql"};
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    char *s = read_file(files[i], NULL);
    if (!s)
      return 0;
    PGresult *r = PQexec(db, s);
    free(s);
    int ok = PQresultStatus(r) == PGRES_COMMAND_OK;
    PQclear(r);
    if (!ok)
      return 0;
  }
  return 1;
}
int load_problem(const char *week, Problem *p, long long *revision) {
  memset(p, 0, sizeof(*p));
  const char *a[] = {week};
  PGresult *r = query(
      "SELECT revision,rest_minutes,ot_threshold,ot_multiplier,fair_weight "
      "FROM settings WHERE id=1",
      0, NULL);
  if (!r)
    return 0;
  *revision = atoll(PQgetvalue(r, 0, 0));
  p->rules = (Rules){atoi(PQgetvalue(r, 0, 1)),
                     atoi(PQgetvalue(r, 0, 2)),
                     atoi(PQgetvalue(r, 0, 3)),
                     atoi(PQgetvalue(r, 0, 4)),
                     200000,
                     2000};
  PQclear(r);
  r = query(
      "SELECT "
      "e.id,e.active,e.rate,e.max_minutes,e.preferred,coalesce(sum(DISTINCT "
      "es.skill_id),0) FROM employees e LEFT JOIN employee_skills es ON "
      "es.employee_id=e.id GROUP BY e.id ORDER BY e.id",
      0, NULL);
  if (!r)
    return 0;
  if (PQntuples(r) > MAX_E) {
    PQclear(r);
    strcpy(db_error, "Maximum 64 employees supported.");
    return 0;
  }
  p->ne = PQntuples(r);
  for (int i = 0; i < p->ne; i++)
    p->e[i] = (Employee){.id = atoi(PQgetvalue(r, i, 0)),
                         .active = !strcmp(PQgetvalue(r, i, 1), "t"),
                         .rate = atoi(PQgetvalue(r, i, 2)),
                         .max_minutes = atoi(PQgetvalue(r, i, 3)),
                         .preferred = atoi(PQgetvalue(r, i, 4)),
                         .skills = atoi(PQgetvalue(r, i, 5))};
  PQclear(r);
  r = query(
      "SELECT employee_id,extract(epoch FROM starts)::bigint,extract(epoch "
      "FROM ends)::bigint FROM availability WHERE ends >= ($1::date AT TIME "
      "ZONE (SELECT timezone FROM settings)) - interval '1 day' AND starts < "
      "(($1::date+7) AT TIME ZONE (SELECT timezone FROM settings)) + interval "
      "'1 day' ORDER BY starts",
      1, a);
  if (!r)
    return 0;
  for (int i = 0; i < PQntuples(r); i++)
    for (int e = 0; e < p->ne; e++)
      if (p->e[e].id == atoi(PQgetvalue(r, i, 0))) {
        Employee *u = &p->e[e];
        if (u->nw >= MAX_W) {
          PQclear(r);
          strcpy(db_error,
                 "Maximum 64 availability windows per employee per week.");
          return 0;
        }
        u->windows[u->nw++] =
            (Window){atoll(PQgetvalue(r, i, 1)), atoll(PQgetvalue(r, i, 2))};
      }
  PQclear(r);
  r = query("SELECT id,skill_id,count,priority,extract(epoch FROM "
            "starts)::bigint,extract(epoch FROM ends)::bigint FROM shifts "
            "WHERE archived_at IS NULL AND starts >= ($1::date AT TIME ZONE "
            "(SELECT timezone FROM "
            "settings)) AND starts < (($1::date+7) AT TIME ZONE (SELECT "
            "timezone FROM settings)) ORDER BY starts,id",
            1, a);
  if (!r)
    return 0;
  int total = 0;
  p->ns = PQntuples(r);
  if (p->ns > MAX_S) {
    PQclear(r);
    strcpy(db_error, "Maximum 128 shifts per week.");
    return 0;
  }
  for (int i = 0; i < p->ns; i++) {
    p->s[i] = (Shift){atoi(PQgetvalue(r, i, 0)),  atoi(PQgetvalue(r, i, 1)),
                      atoi(PQgetvalue(r, i, 2)),  atoi(PQgetvalue(r, i, 3)),
                      atoll(PQgetvalue(r, i, 4)), atoll(PQgetvalue(r, i, 5))};
    total += p->s[i].count;
  }
  PQclear(r);
  if (total > MAX_A) {
    strcpy(db_error, "Maximum 512 staffing positions per week.");
    return 0;
  }
  /* Published assignments in neighbouring weeks constrain rest at the boundary.
   */
  r = query(
      "SELECT a.employee_id,extract(epoch FROM s.starts)::bigint,extract(epoch "
      "FROM s.ends)::bigint FROM assignments a JOIN schedule_versions v ON "
      "v.id=a.version_id JOIN shifts s ON s.id=a.shift_id CROSS JOIN settings "
      "st WHERE v.status='published' AND s.archived_at IS NULL AND "
      "v.week<>$1::date AND "
      "s.ends>=($1::date AT TIME ZONE st.timezone)-interval '1 day' AND "
      "s.starts<(($1::date+7) AT TIME ZONE st.timezone)+interval '1 day'",
      1, a);
  if (!r)
    return 0;
  for (int i = 0; i < PQntuples(r); i++)
    for (int e = 0; e < p->ne; e++)
      if (p->e[e].id == atoi(PQgetvalue(r, i, 0))) {
        Employee *u = &p->e[e];
        if (u->nb >= MAX_W) {
          PQclear(r);
          strcpy(db_error, "Too many neighbouring assignments.");
          return 0;
        }
        u->busy[u->nb++] =
            (Window){atoll(PQgetvalue(r, i, 1)), atoll(PQgetvalue(r, i, 2))};
      }
  PQclear(r);
  return 1;
}
static cJSON *solution_json(const Solution *s) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "filled", s->n);
  cJSON_AddNumberToObject(o, "required", s->required);
  cJSON_AddBoolToObject(o, "complete", s->complete);
  cJSON_AddNumberToObject(o, "cost", (double)s->cost);
  cJSON_AddNumberToObject(o, "fairness", s->fairness);
  cJSON_AddNumberToObject(o, "objective", s->objective);
  return o;
}
cJSON *result_json(const Result *r) {
  cJSON *o = solution_json(&r->best);
  cJSON_AddStringToObject(o, "outcome", r->outcome);
  cJSON_AddNumberToObject(o, "nodes", (double)r->nodes);
  cJSON_AddNumberToObject(o, "pruned", (double)r->pruned);
  cJSON_AddNumberToObject(o, "elapsed_ms", r->elapsed_ms);
  cJSON_AddItemToObject(o, "greedy", solution_json(&r->greedy));
  return o;
}
int seed(void) {
  const char *pw = getenv("SEED_PASSWORD");
  if (!pw || strlen(pw) < 12) {
    fprintf(stderr,
            "Set SEED_PASSWORD (12+ characters) in your environment.\n");
    return 0;
  }
  char hash[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(hash, pw, strlen(pw),
                        crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE))
    return 0;
  if (!command("BEGIN", 0, NULL))
    return 0;
  const char *a[] = {hash};
  if (!command("INSERT INTO users(email,password_hash,role) "
               "VALUES('manager@shiftnest.demo',$1,'manager') ON CONFLICT DO "
               "NOTHING",
               1, a))
    goto fail;
  const char *names[] = {"Aarav Shah",  "Diya Rao",     "Ishaan Mehta",
                         "Ananya Iyer", "Kabir Patel",  "Meera Nair",
                         "Rohan Das",   "Sara Khan",    "Arjun Reddy",
                         "Tara Sen",    "Vihaan Joshi", "Nila Menon"};
  for (int i = 0; i < 12; i++) {
    char email[80], rate[20], skills[10], max[20];
    snprintf(email, sizeof(email), "employee%d@shiftnest.demo", i + 1);
    snprintf(rate, sizeof(rate), "%d", 18000 + i * 1500);
    snprintf(skills, sizeof(skills), "%d",
             i % 4 == 0   ? 9
             : i % 3 == 0 ? 5
             : i % 2 == 0 ? 3
                          : 6);
    snprintf(max, sizeof(max), "%d", i % 4 == 0 ? 1440 : 2400);
    const char *v[] = {email, hash, names[i], rate, max, skills};
    if (!command("WITH u AS (INSERT INTO users(email,password_hash,role) "
                 "VALUES($1,$2,'employee') ON CONFLICT(email) DO UPDATE SET "
                 "email=excluded.email RETURNING id) INSERT INTO "
                 "employees(user_id,name,rate,max_minutes,preferred) SELECT "
                 "id,$3,$4::int,$5::int,$5::int FROM u ON CONFLICT(user_id) DO "
                 "NOTHING",
                 5, v))
      goto fail;
    if (!command("INSERT INTO employee_skills SELECT e.id,s.id FROM employees "
                 "e JOIN users u ON u.id=e.user_id CROSS JOIN skills s WHERE "
                 "u.email=$1 AND (s.id & $6::int)>0 ON CONFLICT DO NOTHING",
                 6, v))
      goto fail;
  }
  if (!command(
          "INSERT INTO availability(employee_id,starts,ends) SELECT "
          "e.id,((date_trunc('week',now() AT TIME ZONE "
          "st.timezone)::date+d)+time '07:00') AT TIME ZONE "
          "st.timezone,((date_trunc('week',now() AT TIME ZONE "
          "st.timezone)::date+d)+time '22:00') AT TIME ZONE st.timezone FROM "
          "employees e CROSS JOIN settings st CROSS JOIN generate_series(0,6) "
          "d WHERE NOT (e.id%4=0 AND d=2) ON CONFLICT DO NOTHING",
          0, NULL))
    goto fail;
  if (!command(
          "INSERT INTO shifts(starts,ends,skill_id,count,priority) SELECT "
          "((date_trunc('week',now() AT TIME ZONE timezone)::date+d)+time "
          "'09:00') AT TIME ZONE timezone,((date_trunc('week',now() AT TIME "
          "ZONE timezone)::date+d)+time '15:00') AT TIME ZONE timezone,CASE "
          "WHEN d%2=0 THEN 1 ELSE 2 END,2,1 FROM settings CROSS JOIN "
          "generate_series(0,6) d WHERE NOT EXISTS(SELECT 1 FROM shifts)",
          0, NULL))
    goto fail;
  if (!command("INSERT INTO notifications(user_id,event_key,message) SELECT "
               "user_id,'welcome','Welcome to ShiftNest. Please review your "
               "availability before the next schedule is planned.' FROM "
               "employees ON CONFLICT DO NOTHING",
               0, NULL))
    goto fail;
  return command("COMMIT", 0, NULL);
fail:
  command("ROLLBACK", 0, NULL);
  return 0;
}
