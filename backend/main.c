#include "db.h"
#include <ctype.h>
#include <microhttpd.h>
#include <pthread.h>
#include <signal.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER,
                       solver_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t running = 1;
typedef struct {
  char body[32769];
  size_t length;
  int overflow;
} Request;
typedef struct {
  int id, employee, manager;
  char csrf[65];
} User;
static void stop(int x) {
  (void)x;
  running = 0;
}
static const char *str(cJSON *o, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static int num(cJSON *o, const char *key, int def) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(v) && v->valuedouble >= -2147483647 &&
                 v->valuedouble <= 2147483647 && v->valuedouble == v->valueint
             ? v->valueint
             : def;
}
static void integer(char *out, int n) { snprintf(out, 32, "%d", n); }
static cJSON *error(const char *s) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", s);
  return o;
}
static int date_valid(const char *s) {
  if (strlen(s) != 10 || s[4] != '-' || s[7] != '-')
    return 0;
  for (int i = 0; i < 10; i++)
    if (i != 4 && i != 7 && !isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}
static int time_valid(const char *s) {
  if (strlen(s) != 16 || s[10] != 'T' || s[13] != ':')
    return 0;
  char d[11];
  memcpy(d, s, 10);
  d[10] = 0;
  return date_valid(d) && isdigit((unsigned char)s[11]) &&
         isdigit((unsigned char)s[12]) && isdigit((unsigned char)s[14]) &&
         isdigit((unsigned char)s[15]);
}
static void random_hex(char out[65]) {
  unsigned char bytes[32];
  randombytes_buf(bytes, sizeof(bytes));
  sodium_bin2hex(out, 65, bytes, sizeof(bytes));
}
static enum MHD_Result send_response(struct MHD_Connection *c, int status,
                                     const char *body, const char *type,
                                     const char *cookie) {
  struct MHD_Response *r = MHD_create_response_from_buffer(
      strlen(body), (void *)body, MHD_RESPMEM_MUST_COPY);
  if (!r)
    return MHD_NO;
  MHD_add_response_header(r, "Content-Type", type);
  MHD_add_response_header(r, "Cache-Control", "no-store");
  MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
  MHD_add_response_header(r, "Referrer-Policy", "same-origin");
  MHD_add_response_header(
      r, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' "
      "data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  if (cookie)
    MHD_add_response_header(r, "Set-Cookie", cookie);
  enum MHD_Result ok = MHD_queue_response(c, (unsigned)status, r);
  MHD_destroy_response(r);
  return ok;
}
static int authenticate(struct MHD_Connection *c, User *u) {
  const char *token =
      MHD_lookup_connection_value(c, MHD_COOKIE_KIND, "shiftnest");
  if (!token || strlen(token) != 64)
    return 0;
  const char *a[] = {token};
  PGresult *r = query(
      "SELECT u.id,u.role,coalesce(e.id,0),s.csrf FROM sessions s JOIN users u "
      "ON u.id=s.user_id LEFT JOIN employees e ON e.user_id=u.id WHERE "
      "s.token=$1 AND s.expires>now() AND (u.role='manager' OR e.active)",
      1, a);
  if (!r)
    return 0;
  int ok = PQntuples(r) == 1;
  if (ok) {
    u->id = atoi(PQgetvalue(r, 0, 0));
    u->manager = !strcmp(PQgetvalue(r, 0, 1), "manager");
    u->employee = atoi(PQgetvalue(r, 0, 2));
    snprintf(u->csrf, sizeof(u->csrf), "%s", PQgetvalue(r, 0, 3));
  }
  PQclear(r);
  return ok;
}
static cJSON *login(cJSON *body, int *status, char *cookie) {
  const char *email = str(body, "email"), *password = str(body, "password");
  if (strlen(email) > 150 || strlen(password) > 200 || !strchr(email, '@')) {
    *status = 400;
    return error("Enter a valid email and password.");
  }
  const char *a[] = {email};
  PGresult *r = query("SELECT failures FROM login_attempts WHERE key=$1 AND "
                      "window_start>now()-interval '15 minutes'",
                      1, a);
  if (!r)
    return NULL;
  int blocked = PQntuples(r) && atoi(PQgetvalue(r, 0, 0)) >= 8;
  PQclear(r);
  if (blocked) {
    *status = 429;
    return error("Too many attempts. Try again in 15 minutes.");
  }
  r = query(
      "SELECT u.id,u.password_hash FROM users u LEFT JOIN employees e ON "
      "e.user_id=u.id WHERE u.email=$1 AND (u.role='manager' OR e.active)",
      1, a);
  if (!r)
    return NULL;
  int id = 0;
  if (PQntuples(r) == 1 &&
      crypto_pwhash_str_verify(PQgetvalue(r, 0, 1), password,
                               strlen(password)) == 0)
    id = atoi(PQgetvalue(r, 0, 0));
  PQclear(r);
  if (!id) {
    command("INSERT INTO login_attempts(key,failures) VALUES($1,1) ON "
            "CONFLICT(key) DO UPDATE SET failures=CASE WHEN "
            "login_attempts.window_start<now()-interval '15 minutes' THEN 1 "
            "ELSE login_attempts.failures+1 END,window_start=CASE WHEN "
            "login_attempts.window_start<now()-interval '15 minutes' THEN "
            "now() ELSE login_attempts.window_start END",
            1, a);
    *status = 401;
    return error("Email or password is incorrect.");
  }
  command("DELETE FROM login_attempts WHERE key=$1", 1, a);
  command("DELETE FROM sessions WHERE expires<now()", 0, NULL);
  char token[65], csrf[65], uid[32];
  random_hex(token);
  random_hex(csrf);
  integer(uid, id);
  const char *v[] = {token, uid, csrf};
  if (!command("INSERT INTO sessions(token,user_id,csrf,expires) "
               "VALUES($1,$2::int,$3,now()+interval '12 hours')",
               3, v))
    return NULL;
  snprintf(cookie, 256,
           "shiftnest=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200%s",
           token,
           getenv("COOKIE_SECURE") && !strcmp(getenv("COOKIE_SECURE"), "1")
               ? "; Secure"
               : "");
  return cJSON_CreateObject();
}
static int read_assignments(int version, const Problem *p, Solution *s,
                            int validate) {
  memset(s, 0, sizeof(*s));
  char id[32];
  integer(id, version);
  const char *a[] = {id};
  PGresult *r =
      query("SELECT a.employee_id,a.shift_id FROM assignments a JOIN shifts sh "
            "ON sh.id=a.shift_id WHERE sh.archived_at IS NULL AND "
            "a.version_id=$1::int ORDER BY a.shift_id,a.employee_id",
            1, a);
  if (!r)
    return 0;
  int ok = 1;
  for (int i = 0; i < PQntuples(r); i++) {
    int e = -1, sh = -1;
    for (int j = 0; j < p->ne; j++)
      if (p->e[j].id == atoi(PQgetvalue(r, i, 0)))
        e = j;
    for (int j = 0; j < p->ns; j++)
      if (p->s[j].id == atoi(PQgetvalue(r, i, 1)))
        sh = j;
    if (s->n >= MAX_A || e < 0 || sh < 0 ||
        (validate && !can_assign(p, s, e, sh))) {
      ok = 0;
      break;
    }
    s->a[s->n++] = (Assignment){e, sh};
  }
  PQclear(r);
  measure(p, s);
  return ok;
}
static int load_assignments(int version, const Problem *p, Solution *s) {
  return read_assignments(version, p, s, 1);
}

/* Cancellation may proceed even if unrelated inputs already made a draft stale.
   Recalculate the remaining roster, but do not grant it a new feasibility
   proof. */
static int refresh_cancelled_version(int version, const char *week) {
  Problem p;
  long long revision;
  Solution sol;
  char id[32];
  integer(id, version);
  const char *a[] = {id};
  if (!load_problem(week, &p, &revision))
    return 0;
  PGresult *r = query(
      "SELECT settings FROM optimization_runs WHERE version_id=$1::int", 1, a);
  if (!r)
    return 0;
  if (!PQntuples(r)) {
    PQclear(r);
    return 1;
  }
  cJSON *saved = cJSON_Parse(PQgetvalue(r, 0, 0));
  PQclear(r);
  p.rules.ot_threshold = num(saved, "ot_threshold", p.rules.ot_threshold);
  p.rules.ot_multiplier = num(saved, "ot_multiplier", p.rules.ot_multiplier);
  p.rules.fair_weight = num(saved, "fair_weight", p.rules.fair_weight);
  cJSON *rates = cJSON_GetObjectItem(saved, "employee_rates");
  for (int e = 0; e < p.ne; e++) {
    char key[32];
    integer(key, p.e[e].id);
    p.e[e].rate = num(rates, key, p.e[e].rate);
  }
  cJSON_Delete(saved);
  if (!read_assignments(version, &p, &sol, 0))
    return 0;
  Result result = {0};
  result.best = sol;
  strcpy(result.outcome, "Requirement cancelled; validation required");
  cJSON *details = result_json(&result);
  cJSON_DeleteItemFromObject(details, "greedy");
  cJSON_AddStringToObject(details, "method", "Manual adjustment");
  cJSON_AddBoolToObject(details, "needs_regeneration", 1);
  char *json = cJSON_PrintUnformatted(details);
  const char *v[] = {id, json};
  int ok = command("UPDATE optimization_runs SET "
                   "original_details=coalesce(original_details,details),"
                   "details=$2::jsonb WHERE version_id=$1::int",
                   2, v);
  free(json);
  cJSON_Delete(details);
  return ok;
}
static cJSON *state(User u, const char *week) {
  char uid[32], eid[32];
  integer(uid, u.id);
  integer(eid, u.employee);
  const char *a[] = {week, uid, eid, u.manager ? "true" : "false"};
  cJSON *o = cJSON_CreateObject();
  cJSON *me = cJSON_CreateObject();
  cJSON_AddNumberToObject(me, "id", u.id);
  cJSON_AddNumberToObject(me, "employee_id", u.employee);
  cJSON_AddStringToObject(me, "role", u.manager ? "manager" : "employee");
  cJSON_AddStringToObject(me, "csrf", u.csrf);
  cJSON_AddItemToObject(o, "user", me);
#define ADD_ROWS(key, sql)                                                     \
  do {                                                                         \
    cJSON *v = rows(sql, 4, a);                                                \
    if (!v) {                                                                  \
      cJSON_Delete(o);                                                         \
      return NULL;                                                             \
    }                                                                          \
    cJSON_AddItemToObject(o, key, v);                                          \
  } while (0)
  ADD_ROWS(
      "settings",
      "SELECT "
      "timezone,revision,rest_minutes,ot_threshold,ot_multiplier,fair_weight,"
      "to_char(date_trunc('week',now() AT TIME ZONE timezone),'YYYY-MM-DD') AS "
      "current_week FROM settings WHERE $1::date IS NOT NULL AND $2::int>0 AND "
      "$3::int>=0 AND $4::boolean IS NOT NULL");
  ADD_ROWS("skills",
           "SELECT * FROM skills WHERE $1::date IS NOT NULL AND $2::int>0 AND "
           "$3::int>=0 AND $4::boolean IS NOT NULL ORDER BY id");
  ADD_ROWS(
      "employees",
      "SELECT e.*,u.email,coalesce((SELECT sum(skill_id) FROM employee_skills "
      "WHERE employee_id=e.id),0) AS skills FROM employees e JOIN users u ON "
      "u.id=e.user_id WHERE ($4::boolean OR e.id=$3::int) AND $1::date IS NOT "
      "NULL AND $2::int>0 ORDER BY e.id");
  ADD_ROWS(
      "availability",
      "SELECT a.id,a.employee_id,to_char(a.starts AT TIME ZONE "
      "st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS starts,to_char(a.ends AT TIME "
      "ZONE st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS ends FROM availability a "
      "CROSS JOIN settings st WHERE ($4::boolean OR a.employee_id=$3::int) AND "
      "a.ends>=($1::date AT TIME ZONE st.timezone) AND a.starts<(($1::date+7) "
      "AT TIME ZONE st.timezone) AND $2::int>0 ORDER BY a.starts");
  ADD_ROWS(
      "versions",
      "SELECT v.*,r.details,r.settings AS run_settings FROM schedule_versions "
      "v LEFT JOIN optimization_runs r ON r.version_id=v.id WHERE "
      "v.week=$1::date AND ($4::boolean OR v.status='published') AND $2::int>0 "
      "AND $3::int>=0 ORDER BY v.id DESC");
  ADD_ROWS(
      "shifts",
      "SELECT s.id,s.title,s.skill_id,s.count,s.priority,to_char(s.starts AT "
      "TIME ZONE "
      "st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS starts,to_char(s.ends AT TIME "
      "ZONE st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS ends FROM shifts s CROSS "
      "JOIN settings st WHERE s.archived_at IS NULL AND s.starts>=($1::date AT "
      "TIME ZONE st.timezone) "
      "AND s.starts<(($1::date+7) AT TIME ZONE st.timezone) AND ($4::boolean "
      "OR EXISTS(SELECT 1 FROM assignments a JOIN schedule_versions v ON "
      "v.id=a.version_id WHERE a.shift_id=s.id AND a.employee_id=$3::int AND "
      "v.status='published')) AND $2::int>0 ORDER BY s.starts,s.id");
  ADD_ROWS("assignments",
           "SELECT a.* FROM assignments a JOIN schedule_versions v ON "
           "v.id=a.version_id JOIN shifts sh ON sh.id=a.shift_id WHERE "
           "sh.archived_at IS NULL AND v.week=$1::date AND ($4::boolean OR "
           "(v.status='published' AND a.employee_id=$3::int)) AND $2::int>0 "
           "ORDER BY a.shift_id,a.employee_id");
  ADD_ROWS("notifications",
           "SELECT n.*,e.name FROM notifications n LEFT JOIN employees e ON "
           "e.user_id=n.user_id WHERE ($4::boolean OR n.user_id=$2::int) AND "
           "$1::date IS NOT NULL AND $3::int>=0 ORDER BY n.created_at DESC "
           "LIMIT 100");
  ADD_ROWS(
      "next_shift",
      "SELECT s.title,s.skill_id,to_char(s.starts AT TIME ZONE "
      "st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS starts,to_char(s.ends AT TIME "
      "ZONE st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS ends FROM assignments a "
      "JOIN schedule_versions v ON v.id=a.version_id JOIN shifts s ON "
      "s.id=a.shift_id CROSS JOIN settings st WHERE a.employee_id=$3::int AND "
      "v.status='published' AND s.archived_at IS NULL AND s.starts>=now() AND "
      "$1::date IS NOT NULL AND "
      "$2::int>0 AND $4::boolean IS NOT NULL ORDER BY s.starts LIMIT 1");
  ADD_ROWS("history",
           "SELECT a.version_id,s.id AS shift_id,to_char(s.starts AT TIME ZONE "
           "st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS starts,to_char(s.ends AT "
           "TIME ZONE st.timezone,'YYYY-MM-DD\"T\"HH24:MI') AS "
           "ends,s.title,s.skill_id,s.archived_at AS cancelled_at "
           "FROM assignments a JOIN schedule_versions v ON v.id=a.version_id "
           "JOIN shifts s ON s.id=a.shift_id CROSS JOIN settings st WHERE "
           "v.status IN('published','superseded') AND a.employee_id=$3::int "
           "AND $2::int>0 AND $1::date IS NOT NULL AND $4::boolean IS NOT NULL "
           "ORDER BY s.starts DESC LIMIT 100");
#undef ADD_ROWS
  Problem p;
  long long rev;
  if (!load_problem(week, &p, &rev)) {
    cJSON_Delete(o);
    return NULL;
  }
  cJSON *versions = cJSON_GetObjectItem(o, "versions"),
        *v = cJSON_GetArrayItem(versions, 0);
  int vid = v ? num(v, "id", 0) : 0;
  if (!u.manager) {
    cJSON *it;
    cJSON_ArrayForEach(it, versions) {
      if (!strcmp(str(it, "status"), "published")) {
        vid = num(it, "id", 0);
        break;
      }
    }
  }
  /* Immutable wage rates and calculation rules belong to the schedule run. */
  cJSON *snapshot = cJSON_GetObjectItem(v, "run_settings");
  if (snapshot) {
    p.rules.ot_threshold = num(snapshot, "ot_threshold", p.rules.ot_threshold);
    p.rules.ot_multiplier =
        num(snapshot, "ot_multiplier", p.rules.ot_multiplier);
    cJSON *rates = cJSON_GetObjectItem(snapshot, "employee_rates");
    for (int e = 0; e < p.ne; e++) {
      char key[32];
      integer(key, p.e[e].id);
      p.e[e].rate = num(rates, key, p.e[e].rate);
    }
  }
  Solution sol = {0};
  if (vid) { /* Reports remain visible if availability subsequently changes. */
    char id[32];
    integer(id, vid);
    const char *pa[] = {id};
    PGresult *r = query("SELECT a.employee_id,a.shift_id FROM assignments a "
                        "JOIN shifts sh ON sh.id=a.shift_id WHERE "
                        "sh.archived_at IS NULL AND a.version_id=$1::int",
                        1, pa);
    if (r) {
      for (int i = 0; i < PQntuples(r); i++)
        for (int e = 0; e < p.ne; e++)
          for (int s = 0; s < p.ns; s++)
            if (p.e[e].id == atoi(PQgetvalue(r, i, 0)) &&
                p.s[s].id == atoi(PQgetvalue(r, i, 1)) && sol.n < MAX_A)
              sol.a[sol.n++] = (Assignment){e, s};
      PQclear(r);
    }
  }
  cJSON *wages = cJSON_CreateArray();
  for (int e = 0; e < p.ne; e++)
    if (u.manager || p.e[e].id == u.employee) {
      int minutes = 0;
      for (int i = 0; i < sol.n; i++)
        if (sol.a[i].e == e)
          minutes += (int)((p.s[sol.a[i].s].end - p.s[sol.a[i].s].start) / 60);
      Wage w = wage(minutes, p.e[e].rate, &p.rules);
      cJSON *x = cJSON_CreateObject();
      cJSON_AddNumberToObject(x, "employee_id", p.e[e].id);
      cJSON_AddNumberToObject(x, "regular_minutes", w.regular);
      cJSON_AddNumberToObject(x, "overtime_minutes", w.overtime);
      cJSON_AddNumberToObject(x, "regular_pay", (double)w.regular_pay);
      cJSON_AddNumberToObject(x, "overtime_pay", (double)w.overtime_pay);
      cJSON_AddNumberToObject(x, "total", (double)w.total);
      cJSON_AddItemToArray(wages, x);
    }
  cJSON_AddItemToObject(o, "wages", wages);
  cJSON *wage_rules = cJSON_CreateObject();
  cJSON_AddNumberToObject(wage_rules, "ot_threshold", p.rules.ot_threshold);
  cJSON_AddNumberToObject(wage_rules, "ot_multiplier", p.rules.ot_multiplier);
  cJSON_AddItemToObject(o, "wage_rules", wage_rules);
  /* Employee responses contain no company run costs or other private data. */
  if (!u.manager) {
    cJSON *it;
    cJSON_ArrayForEach(it, versions) {
      cJSON_DeleteItemFromObject(it, "details");
      cJSON_DeleteItemFromObject(it, "run_settings");
    }
  }
  return o;
}
static cJSON *mutate(const char *url, cJSON *b, User u, int *status) {
  char id[32], eid[32], uid[32], rate[32], max[32], pref[32], skill[32],
      count[32], priority[32];
  integer(id, num(b, "id", 0));
  integer(eid, u.manager ? num(b, "employee_id", 0) : u.employee);
  integer(uid, u.id);
  const char *week = str(b, "week");
  if (!strcmp(url, "/api/notification")) {
    const char *a[] = {id, uid, str(b, "action")};
    if (!command("UPDATE notifications SET "
                 "read_at=coalesce(read_at,now()),acknowledged_at=CASE WHEN "
                 "$3='acknowledge' THEN coalesce(acknowledged_at,now()) ELSE "
                 "acknowledged_at END WHERE id=$1::int AND user_id=$2::int",
                 3, a))
      return NULL;
    return cJSON_CreateObject();
  }
  if (!strcmp(url, "/api/availability")) {
    const char *a[] = {id, eid, str(b, "starts"), str(b, "ends")};
    if (!strcmp(str(b, "action"), "delete")) {
      if (!command("DELETE FROM availability WHERE id=$1::int AND "
                   "employee_id=$2::int",
                   2, a))
        return NULL;
    } else {
      if (!time_valid(a[2]) || !time_valid(a[3])) {
        *status = 400;
        return error("Choose valid availability start and end times.");
      }
      if (!command("INSERT INTO availability(employee_id,starts,ends) "
                   "VALUES($2::int,$3::timestamp AT TIME ZONE (SELECT timezone "
                   "FROM settings),$4::timestamp AT TIME ZONE (SELECT timezone "
                   "FROM settings)) ON CONFLICT DO NOTHING",
                   4, a))
        return NULL;
    }
    return cJSON_CreateObject();
  }
  if (!u.manager) {
    *status = 403;
    return error("Manager access is required.");
  }
  if (!strcmp(url, "/api/employee-status")) {
    const char *a[] = {id};
    if (atoi(id) < 1 || num(b, "active", -1) != 0) {
      *status = 400;
      return error("Choose an employee to deactivate.");
    }
    if (!command("BEGIN", 0, NULL))
      return NULL;
    PGresult *r = query(
        "SELECT user_id,active FROM employees WHERE id=$1::int FOR UPDATE", 1,
        a);
    if (!r) {
      command("ROLLBACK", 0, NULL);
      return NULL;
    }
    if (!PQntuples(r)) {
      PQclear(r);
      command("ROLLBACK", 0, NULL);
      *status = 404;
      return error("Employee not found.");
    }
    int active = !strcmp(PQgetvalue(r, 0, 1), "t");
    PQclear(r);
    int ok =
        !active ||
        command("UPDATE employees SET active=false WHERE id=$1::int", 1, a);
    if (ok)
      ok = command("DELETE FROM sessions WHERE user_id=(SELECT user_id FROM "
                   "employees WHERE id=$1::int)",
                   1, a);
    if (!command(ok ? "COMMIT" : "ROLLBACK", 0, NULL))
      ok = 0;
    return ok ? cJSON_CreateObject() : NULL;
  }
  if (!strcmp(url, "/api/employee")) {
    integer(rate, num(b, "rate", -1));
    integer(max, num(b, "max_minutes", 0));
    integer(pref, num(b, "preferred", 0));
    integer(skill, num(b, "skills", 0));
    const char *name = str(b, "name"), *email = str(b, "email"),
               *password = str(b, "password");
    int edit = atoi(id) > 0;
    if (!*name || strlen(name) > 100 || strlen(email) > 150 ||
        !strchr(email, '@') || atoi(rate) < 0 || atoi(rate) > 10000000 ||
        atoi(max) < 1 || atoi(max) > 10080 || atoi(pref) < 1 ||
        atoi(pref) > 10080 || atoi(skill) < 1 ||
        (!edit && strlen(password) < 12) || strlen(password) > 200) {
      *status = 400;
      return error("Check name, email, skills, rate, hours and password (12+ "
                   "characters for new accounts).");
    }
    char hash[crypto_pwhash_STRBYTES] = "";
    if (*password && crypto_pwhash_str(hash, password, strlen(password),
                                       crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                       crypto_pwhash_MEMLIMIT_INTERACTIVE))
      return NULL;
    const char *a[] = {id,  name, email, rate,
                       max, pref, skill, num(b, "active", 1) ? "true" : "false",
                       hash};
    if (!command("BEGIN", 0, NULL))
      return NULL;
    PGresult *known =
        query("SELECT coalesce(bit_or(id),0) FROM skills", 0, NULL);
    int valid_skills = known && !(atoi(skill) & ~atoi(PQgetvalue(known, 0, 0)));
    PQclear(known);
    if (!valid_skills) {
      command("ROLLBACK", 0, NULL);
      *status = 400;
      return error("Choose at least one existing skill.");
    }
    int ok;
    if (edit)
      ok = command("UPDATE employees SET "
                   "name=$2,rate=$4::int,max_minutes=$5::int,preferred=$6::int,"
                   "active=$8::boolean WHERE id=$1::int AND $3 IS NOT NULL AND "
                   "$7::int>0 AND $9 IS NOT NULL",
                   9, a) &&
           command("UPDATE users SET email=$3,password_hash=CASE WHEN $9='' "
                   "THEN password_hash ELSE $9 END WHERE id=(SELECT user_id "
                   "FROM employees WHERE id=$1::int) AND $2 IS NOT NULL AND "
                   "$4::int>=0 AND $5::int>0 AND $6::int>0 AND $7::int>0 AND "
                   "$8::boolean IS NOT NULL",
                   9, a);
    else {
      PGresult *r =
          query("WITH u AS(INSERT INTO users(email,password_hash,role) "
                "VALUES($3,$9,'employee') RETURNING id) INSERT INTO "
                "employees(user_id,name,rate,max_minutes,preferred,active) "
                "SELECT u.id,$2,$4::int,$5::int,$6::int,$8::boolean FROM u "
                "WHERE $1::int=0 AND $7::int>0 RETURNING id",
                9, a);
      ok = r != NULL;
      if (r) {
        integer(id, atoi(PQgetvalue(r, 0, 0)));
        PQclear(r);
      }
    }
    if (ok)
      ok = command("DELETE FROM employee_skills WHERE employee_id=$1::int", 1,
                   a) &&
           command("INSERT INTO employee_skills SELECT $1::int,id FROM skills "
                   "WHERE (id & $7::int)>0 AND $2 IS NOT NULL AND $3 IS NOT "
                   "NULL AND $4::int>=0 AND $5::int>0 AND $6::int>0",
                   7, a);
    command(ok ? "COMMIT" : "ROLLBACK", 0, NULL);
    return ok ? cJSON_CreateObject() : NULL;
  }
  if (!strcmp(url, "/api/shift")) {
    integer(skill, num(b, "skill_id", 0));
    integer(count, num(b, "count", 0));
    integer(priority, num(b, "priority", 0));
    const char *a[] = {id,    str(b, "starts"), str(b, "ends"), skill,
                       count, priority,         str(b, "title")};
    if (!strcmp(str(b, "action"), "delete")) {
      if (!command("BEGIN", 0, NULL))
        return NULL;
      PGresult *r =
          query("SELECT s.archived_at,to_char(date_trunc('week',s.starts AT "
                "TIME ZONE st.timezone),'YYYY-MM-DD') FROM shifts s CROSS JOIN "
                "settings st WHERE s.id=$1::int FOR UPDATE OF st,s",
                1, a);
      if (!r) {
        command("ROLLBACK", 0, NULL);
        return NULL;
      }
      if (!PQntuples(r)) {
        PQclear(r);
        command("ROLLBACK", 0, NULL);
        *status = 404;
        return error("Requirement not found.");
      }
      if (!PQgetisnull(r, 0, 0)) {
        PQclear(r);
        command("COMMIT", 0, NULL);
        return cJSON_CreateObject();
      }
      char planning_week[11];
      snprintf(planning_week, sizeof(planning_week), "%s", PQgetvalue(r, 0, 1));
      PQclear(r);
      const char *v[] = {id, planning_week};
      /* Notify only employees whose currently published assignment is
         cancelled. Unique event keys and the archive check make retries
         idempotent. */
      int ok = command(
          "INSERT INTO notifications(user_id,version_id,event_key,message) "
          "SELECT DISTINCT "
          "e.user_id,sv.id,'cancel-requirement:'||s.id||':'||sv.id,'Your "
          "assignment on '||to_char(s.starts AT TIME ZONE st.timezone,'DD Mon "
          "YYYY HH24:MI')||' was cancelled because the shift requirement was "
          "removed. Please review your updated schedule.' FROM assignments ass "
          "JOIN schedule_versions sv ON sv.id=ass.version_id JOIN employees e "
          "ON e.id=ass.employee_id JOIN shifts s ON s.id=ass.shift_id CROSS "
          "JOIN settings st WHERE s.id=$1::int AND sv.status='published' ON "
          "CONFLICT DO NOTHING",
          1, a);
      if (ok)
        ok = command("UPDATE shifts SET archived_at=now() WHERE id=$1::int", 1,
                     a);
      if (ok)
        ok = command(
            "UPDATE schedule_versions SET change_no=change_no+1 WHERE "
            "week=$2::date AND status IN('draft','published') AND $1::int>0",
            2, v);
      r = ok ? query("SELECT id FROM schedule_versions WHERE week=$2::date AND "
                     "status IN('draft','published') AND $1::int>0 ORDER BY id "
                     "FOR UPDATE",
                     2, v)
             : NULL;
      if (!r)
        ok = 0;
      if (r) {
        for (int i = 0; i < PQntuples(r) && ok; i++)
          ok = refresh_cancelled_version(atoi(PQgetvalue(r, i, 0)),
                                         planning_week);
        PQclear(r);
      }
      if (!command(ok ? "COMMIT" : "ROLLBACK", 0, NULL))
        ok = 0;
      return ok ? cJSON_CreateObject() : NULL;
    } else {
      if (!time_valid(a[1]) || !time_valid(a[2]) || atoi(count) < 1 ||
          atoi(count) > 16 || strlen(a[6]) > 100 ||
          strlen(str(b, "new_skill")) > 100) {
        *status = 400;
        return error("Choose valid times and between 1 and 16 employees.");
      }
      if (atoi(id)) {
        PGresult *r = query(
            "SELECT 1 FROM assignments WHERE shift_id=$1::int LIMIT 1", 1, a);
        int used = r && PQntuples(r);
        PQclear(r);
        if (used) {
          *status = 409;
          return error("A scheduled requirement is immutable. Cancel "
                       "assignments and create a replacement shift.");
        }
      }
      if (!command("BEGIN", 0, NULL))
        return NULL;
      const char *new_skill = str(b, "new_skill");
      if (*new_skill) {
        const char *name[] = {new_skill};
        if (!command("LOCK TABLE skills IN SHARE ROW EXCLUSIVE MODE", 0,
                     NULL)) {
          command("ROLLBACK", 0, NULL);
          return NULL;
        }
        PGresult *r = query(
            "SELECT id FROM skills WHERE lower(btrim(name))=lower(btrim($1))",
            1, name);
        if (!r) {
          command("ROLLBACK", 0, NULL);
          return NULL;
        }
        if (PQntuples(r)) {
          integer(skill, atoi(PQgetvalue(r, 0, 0)));
          PQclear(r);
        } else {
          PQclear(r);
          r = query("SELECT coalesce(max(id)*2,1) FROM skills", 0, NULL);
          if (!r) {
            command("ROLLBACK", 0, NULL);
            return NULL;
          }
          int next = atoi(PQgetvalue(r, 0, 0));
          PQclear(r);
          if (next > 536870912) {
            command("ROLLBACK", 0, NULL);
            *status = 400;
            return error(
                "The current optimizer supports up to 30 distinct skills.");
          }
          integer(skill, next);
          const char *values[] = {skill, new_skill};
          if (!command("INSERT INTO skills(id,name) VALUES($1::int,btrim($2))",
                       2, values)) {
            command("ROLLBACK", 0, NULL);
            return NULL;
          }
        }
      }
      int saved = command(
          atoi(id)
              ? "UPDATE shifts SET starts=$2::timestamp AT TIME ZONE "
                "(SELECT timezone FROM settings),ends=$3::timestamp AT "
                "TIME ZONE (SELECT timezone FROM "
                "settings),skill_id=$4::int,count=$5::int,priority=$6::int,"
                "title=$7 "
                "WHERE id=$1::int"
              : "INSERT INTO shifts(starts,ends,skill_id,count,priority,title) "
                "SELECT $2::timestamp AT TIME ZONE (SELECT timezone FROM "
                "settings),$3::timestamp AT TIME ZONE (SELECT timezone "
                "FROM settings),$4::int,$5::int,$6::int,$7 WHERE $1::int=0",
          7, a);
      if (!command(saved ? "COMMIT" : "ROLLBACK", 0, NULL))
        saved = 0;
      if (!saved)
        return NULL;
      /* Shifts may cross midnight but cannot cross the company planning-week
       * boundary. */
    }
    return cJSON_CreateObject();
  }
  if (!strcmp(url, "/api/settings")) {
    char rest[32], ot[32], mult[32], fair[32];
    integer(rest, num(b, "rest_minutes", 660));
    integer(ot, num(b, "ot_threshold", 2400));
    integer(mult, num(b, "ot_multiplier", 150));
    integer(fair, num(b, "fair_weight", 100));
    const char *a[] = {rest, ot, mult, fair};
    if (!command("UPDATE settings SET "
                 "rest_minutes=$1::int,ot_threshold=$2::int,ot_multiplier=$3::"
                 "int,fair_weight=$4::int,revision=revision+1 WHERE id=1",
                 4, a))
      return NULL;
    return cJSON_CreateObject();
  }
  if (!date_valid(week)) {
    *status = 400;
    return error("A valid planning week is required.");
  }
  if (!strcmp(url, "/api/publish") || !strcmp(url, "/api/assignment")) {
    const char *a[] = {id, week};
    if (!command("BEGIN", 0, NULL))
      return NULL;
    PGresult *r = query("SELECT v.status,v.revision,st.revision FROM "
                        "schedule_versions v CROSS JOIN settings st WHERE "
                        "v.id=$1::int AND v.week=$2::date FOR UPDATE OF v,st",
                        2, a);
    if (!r || PQntuples(r) != 1) {
      PQclear(r);
      command("ROLLBACK", 0, NULL);
      *status = 404;
      return error("Schedule not found.");
    }
    int published = !strcmp(PQgetvalue(r, 0, 0), "published"),
        superseded = !strcmp(PQgetvalue(r, 0, 0), "superseded");
    int stale = atoll(PQgetvalue(r, 0, 1)) != atoll(PQgetvalue(r, 0, 2));
    PQclear(r);
    if (published && !strcmp(url, "/api/publish")) {
      command("COMMIT", 0, NULL);
      return cJSON_CreateObject();
    }
    if (superseded || stale) {
      command("ROLLBACK", 0, NULL);
      *status = 409;
      return error("Inputs changed after generation. Generate a new schedule "
                   "before making changes or publishing.");
    }
    Problem p;
    long long revision;
    Solution sol;
    if (!load_problem(week, &p, &revision) ||
        !load_assignments(atoi(id), &p, &sol)) {
      command("ROLLBACK", 0, NULL);
      *status = 409;
      return error("Schedule conflicts with current availability, skills, rest "
                   "or hour limits.");
    }
    int ok = 1;
    if (!strcmp(url, "/api/assignment")) {
      char sid[32], old[32];
      integer(sid, num(b, "shift_id", 0));
      integer(old, num(b, "old_employee_id", 0));
      const char *v[] = {id, sid, old, eid};
      int shift = -1, e = -1;
      for (int i = 0; i < p.ns; i++)
        if (p.s[i].id == atoi(sid))
          shift = i;
      for (int i = 0; i < p.ne; i++)
        if (p.e[i].id == atoi(eid))
          e = i;
      if (atoi(old)) {
        int found = 0;
        for (int i = 0; i < sol.n; i++)
          if (p.s[sol.a[i].s].id == atoi(sid) &&
              p.e[sol.a[i].e].id == atoi(old)) {
            sol.a[i] = sol.a[--sol.n];
            found = 1;
            break;
          }
        if (!found)
          ok = 0;
      }
      if (atoi(eid) && !can_assign(&p, &sol, e, shift))
        ok = 0;
      if (!ok) {
        command("ROLLBACK", 0, NULL);
        *status = 409;
        return error("Assignment rejected: check skill, availability, overlap, "
                     "rest, weekly hours, or staffing capacity.");
      }
      ok = command("DELETE FROM assignments WHERE version_id=$1::int AND "
                   "shift_id=$2::int AND employee_id=$3::int",
                   3, v);
      if (atoi(eid) && ok) {
        sol.a[sol.n++] = (Assignment){e, shift};
        ok = command("INSERT INTO assignments VALUES($1::int,$2::int,$4::int) "
                     "ON CONFLICT DO NOTHING",
                     4, v);
      }
      if (ok)
        ok = command("UPDATE schedule_versions SET change_no=change_no+1 WHERE "
                     "id=$1::int",
                     1, a);
      if (ok && published) {
        const char *n[] = {id, old, eid,
                           atoi(eid) ? "Your published schedule changed. "
                                       "Please review and acknowledge it."
                                     : "An assignment was cancelled. Please "
                                       "review your updated schedule."};
        ok = command(
            "INSERT INTO notifications(user_id,version_id,event_key,message) "
            "SELECT e.user_id,v.id,'change:'||v.id||':'||v.change_no,$4 FROM "
            "employees e CROSS JOIN schedule_versions v WHERE v.id=$1::int AND "
            "e.id IN($2::int,$3::int) ON CONFLICT DO NOTHING",
            4, n);
      }
      if (ok) {
        measure(&p, &sol);
        Result rr = {0};
        rr.best = sol;
        strcpy(rr.outcome, "Manually adjusted; optimality not proven");
        cJSON *j = result_json(&rr);
        cJSON_AddStringToObject(j, "method", "Manual adjustment");
        cJSON_DeleteItemFromObject(j, "greedy");
        char *json = cJSON_PrintUnformatted(j);
        const char *z[] = {id, json};
        ok = command("UPDATE optimization_runs SET details=$2::jsonb WHERE "
                     "version_id=$1::int",
                     2, z);
        free(json);
        cJSON_Delete(j);
      }
    } else {
      if (!sol.complete) {
        command("ROLLBACK", 0, NULL);
        *status = 409;
        return error("Incomplete schedules cannot be published. Resolve "
                     "unfilled positions first.");
      }
      if (ok)
        ok = command(
            "INSERT INTO notifications(user_id,version_id,event_key,message) "
            "SELECT DISTINCT e.user_id,$1::int,'publish:'||$1,'A new ShiftNest "
            "schedule is ready. Please review and acknowledge it.' FROM "
            "employees e JOIN assignments ass ON ass.employee_id=e.id JOIN "
            "schedule_versions v ON v.id=ass.version_id WHERE (v.week=$2::date "
            "AND v.status='published') OR v.id=$1::int ON CONFLICT DO NOTHING",
            2, a);
      if (ok)
        ok = command("UPDATE schedule_versions SET status='superseded' WHERE "
                     "week=$2::date AND status='published' AND id<>$1::int",
                     2, a) &&
             command("UPDATE schedule_versions SET "
                     "status='published',published_at=now() WHERE id=$1::int",
                     1, a);
    }
    /* Publishing or editing published work changes neighbouring-week rest
     * inputs. */
    if (ok && (published || !strcmp(url, "/api/publish")))
      ok = command("UPDATE settings SET revision=revision+1 WHERE id=1", 0,
                   NULL) &&
           command("UPDATE schedule_versions SET revision=(SELECT revision "
                   "FROM settings) WHERE id=$1::int",
                   1, a);
    if (!command(ok ? "COMMIT" : "ROLLBACK", 0, NULL))
      ok = 0;
    return ok ? cJSON_CreateObject() : NULL;
  }
  *status = 404;
  return error("Unknown operation.");
}
static cJSON *generate(cJSON *b, int *status) {
  const char *week = str(b, "week");
  if (!date_valid(week)) {
    *status = 400;
    return error("Choose a valid planning week.");
  }
  if (pthread_mutex_trylock(&solver_mutex)) {
    *status = 409;
    return error("Another schedule is being generated. Try again shortly.");
  }
  Problem *p = calloc(1, sizeof(*p));
  Result *r = calloc(1, sizeof(*r));
  if (!p || !r) {
    free(p);
    free(r);
    pthread_mutex_unlock(&solver_mutex);
    return NULL;
  }
  long long revision;
  int ok = command("BEGIN ISOLATION LEVEL REPEATABLE READ", 0, NULL) &&
           load_problem(week, p, &revision);
  command(ok ? "COMMIT" : "ROLLBACK", 0, NULL);
  if (!ok) {
    free(p);
    free(r);
    pthread_mutex_unlock(&solver_mutex);
    return NULL;
  }
  p->rules.node_limit = num(b, "node_limit", 200000);
  p->rules.time_limit_ms = num(b, "time_limit_ms", 2000);
  if (p->rules.node_limit < 1)
    p->rules.node_limit = 1;
  if (p->rules.node_limit > 2000000)
    p->rules.node_limit = 2000000;
  if (p->rules.time_limit_ms < 1)
    p->rules.time_limit_ms = 1;
  if (p->rules.time_limit_ms > 10000)
    p->rules.time_limit_ms = 10000;
  pthread_mutex_unlock(&db_mutex);
  optimize(p, r, strcmp(str(b, "method"), "greedy") != 0);
  pthread_mutex_lock(&db_mutex);
  cJSON *j = result_json(r);
  cJSON_AddStringToObject(j, "method",
                          !strcmp(str(b, "method"), "greedy")
                              ? "Greedy + replacement descent"
                              : "Branch and Bound");
  char rev[32];
  snprintf(rev, sizeof(rev), "%lld", revision);
  const char *a[] = {week, rev};
  ok = command("BEGIN", 0, NULL);
  PGresult *q = ok ? query("INSERT INTO schedule_versions(week,revision) "
                           "VALUES($1::date,$2::bigint) RETURNING id",
                           2, a)
                   : NULL;
  char id[32] = "0";
  if (q) {
    integer(id, atoi(PQgetvalue(q, 0, 0)));
    PQclear(q);
  } else
    ok = 0;
  for (int i = 0; i < r->best.n && ok; i++) {
    char eid[32], sid[32];
    integer(eid, p->e[r->best.a[i].e].id);
    integer(sid, p->s[r->best.a[i].s].id);
    const char *v[] = {id, sid, eid};
    ok = command("INSERT INTO assignments VALUES($1::int,$2::int,$3::int)", 3,
                 v);
  }
  char *json = cJSON_PrintUnformatted(j);
  char settings[300];
  snprintf(settings, sizeof(settings),
           "{\"rest_minutes\":%d,\"ot_threshold\":%d,\"ot_multiplier\":%d,"
           "\"fair_weight\":%d,\"node_limit\":%d,\"time_limit_ms\":%d}",
           p->rules.rest, p->rules.ot_threshold, p->rules.ot_multiplier,
           p->rules.fair_weight, p->rules.node_limit, p->rules.time_limit_ms);
  cJSON *saved = cJSON_Parse(settings), *rates = cJSON_CreateObject();
  for (int e = 0; e < p->ne; e++) {
    char key[32];
    integer(key, p->e[e].id);
    cJSON_AddNumberToObject(rates, key, p->e[e].rate);
  }
  cJSON_AddItemToObject(saved, "employee_rates", rates);
  char *saved_text = cJSON_PrintUnformatted(saved);
  const char *v[] = {id, json, saved_text};
  if (ok)
    ok = command("INSERT INTO optimization_runs(version_id,details,settings) "
                 "VALUES($1::int,$2::jsonb,$3::jsonb)",
                 3, v);
  command(ok ? "COMMIT" : "ROLLBACK", 0, NULL);
  free(json);
  free(saved_text);
  cJSON_Delete(saved);
  free(p);
  free(r);
  pthread_mutex_unlock(&solver_mutex);
  if (!ok) {
    cJSON_Delete(j);
    return NULL;
  }
  cJSON_AddNumberToObject(j, "version_id", atoi(id));
  return j;
}
static enum MHD_Result handle(void *cls, struct MHD_Connection *c,
                              const char *url, const char *method,
                              const char *version, const char *upload,
                              size_t *size, void **context) {
  (void)cls;
  (void)version;
  if (!*context) {
    *context = calloc(1, sizeof(Request));
    return *context ? MHD_YES : MHD_NO;
  }
  Request *req = *context;
  if (*size) {
    if (req->length + *size > 32768)
      req->overflow = 1;
    else {
      memcpy(req->body + req->length, upload, *size);
      req->length += *size;
    }
    *size = 0;
    return MHD_YES;
  }
  if (req->overflow)
    return send_response(c, 413, "{\"error\":\"Request too large.\"}",
                         "application/json", NULL);
  if (strncmp(url, "/api/", 5)) {
    if (strcmp(method, "GET"))
      return send_response(c, 405, "Method not allowed", "text/plain", NULL);
    const char *path = !strcmp(url, "/app.js")       ? "frontend/app.js"
                       : !strcmp(url, "/styles.css") ? "frontend/styles.css"
                       : !strcmp(url, "/")           ? "frontend/index.html"
                                                     : NULL;
    if (!path)
      return send_response(c, 404, "Not found", "text/plain", NULL);
    char *file = read_file(path, NULL);
    if (!file)
      return send_response(c, 500,
                           "Frontend files unavailable. Run from project root.",
                           "text/plain", NULL);
    enum MHD_Result result =
        send_response(c, 200, file,
                      strstr(path, ".js")    ? "text/javascript"
                      : strstr(path, ".css") ? "text/css"
                                             : "text/html",
                      NULL);
    free(file);
    return result;
  }
  if (strcmp(method, "GET") && strcmp(method, "POST"))
    return send_response(c, 405, "{}", "application/json", NULL);
  int status = 200;
  char cookie[256] = "";
  cJSON *body = cJSON_Parse(req->body), *out = NULL;
  User u = {0};
  pthread_mutex_lock(&db_mutex);
  if (!strcmp(url, "/api/health")) {
    out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "database", PQstatus(db) == CONNECTION_OK);
  } else if (!strcmp(url, "/api/login") && !strcmp(method, "POST"))
    out = login(body, &status, cookie);
  else if (!authenticate(c, &u)) {
    status = 401;
    out = error("Please sign in.");
  } else if (!strcmp(method, "POST")) {
    const char *csrf =
        MHD_lookup_connection_value(c, MHD_HEADER_KIND, "X-CSRF-Token");
    if (!csrf || strlen(csrf) != 64 || sodium_memcmp(csrf, u.csrf, 64)) {
      status = 403;
      out = error("Security token expired. Reload and try again.");
    } else if (!strcmp(url, "/api/logout")) {
      const char *a[] = {
          MHD_lookup_connection_value(c, MHD_COOKIE_KIND, "shiftnest")};
      command("DELETE FROM sessions WHERE token=$1", 1, a);
      strcpy(cookie,
             "shiftnest=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
      out = cJSON_CreateObject();
    } else if (!strcmp(url, "/api/generate")) {
      if (!u.manager) {
        status = 403;
        out = error("Manager access required.");
      } else
        out = generate(body, &status);
    } else
      out = mutate(url, body, u, &status);
  } else if (!strcmp(url, "/api/state")) {
    const char *week =
        MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, "week");
    char current[11];
    if (!week) {
      PGresult *r = query("SELECT to_char(date_trunc('week',now() AT TIME ZONE "
                          "timezone),'YYYY-MM-DD') FROM settings",
                          0, NULL);
      if (r) {
        snprintf(current, sizeof(current), "%s", PQgetvalue(r, 0, 0));
        PQclear(r);
        week = current;
      }
    }
    if (week && date_valid(week))
      out = state(u, week);
    else {
      status = 400;
      out = error("Invalid week.");
    }
  } else {
    status = 404;
    out = error("Not found.");
  }
  if (!out) {
    status = 400;
    out = error(*db_error ? db_error : "The operation could not be completed.");
  }
  pthread_mutex_unlock(&db_mutex);
  char *json = cJSON_PrintUnformatted(out);
  enum MHD_Result result =
      send_response(c, status, json ? json : "{}", "application/json",
                    *cookie ? cookie : NULL);
  free(json);
  cJSON_Delete(out);
  cJSON_Delete(body);
  return result;
}
static void completed(void *cls, struct MHD_Connection *c, void **context,
                      enum MHD_RequestTerminationCode why) {
  (void)cls;
  (void)c;
  (void)why;
  free(*context);
  *context = NULL;
}
static void load_env(void) {
  FILE *f = fopen(".env", "r");
  if (!f)
    return;
  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#')
      continue;
    *eq = 0;
    char *val = eq + 1;
    if (*val == '\"') {
      val++;
      char *end = strrchr(val, '\"');
      if (end)
        *end = 0;
    }
    if (!getenv(line)) {
#ifdef _WIN32
      _putenv_s(line, val);
#else
      setenv(line, val, 0);
#endif
    }
  }
  fclose(f);
  sodium_memzero(line, sizeof(line));
}
int main(int argc, char **argv) {
  load_env();
  if (sodium_init() < 0)
    return 1;
  const char *url = getenv("DATABASE_URL");
  if (!url || !*url) {
    fprintf(stderr, "Set DATABASE_URL in .env. See .env.example. No database "
                    "has been connected.\n");
    return 1;
  }
  db = PQconnectdb(url);
  if (PQstatus(db) != CONNECTION_OK) {
    fprintf(stderr, "PostgreSQL connection failed. Check DATABASE_URL and "
                    "network access.\n");
    PQfinish(db);
    return 1;
  }
  command("SET statement_timeout='15s'", 0, NULL);
  if (argc > 1) {
    int ok = !strcmp(argv[1], "--migrate") ? migrate()
             : !strcmp(argv[1], "--seed")  ? seed()
                                           : 0;
    if (ok && getenv("COMPANY_TIMEZONE")) {
      const char *a[] = {getenv("COMPANY_TIMEZONE")};
      ok = command(
          "UPDATE settings SET timezone=$1 WHERE id=1 AND timezone<>$1", 1, a);
    }
    puts(ok ? "ShiftNest command completed."
            : "ShiftNest command failed. Check configuration and database "
              "state.");
    PQfinish(db);
    return ok ? 0 : 1;
  }
  unsigned port = getenv("PORT") ? (unsigned)atoi(getenv("PORT")) : 8090;
  if (port < 1 || port > 65535)
    port = 8090;
  struct MHD_Daemon *daemon = MHD_start_daemon(
      MHD_USE_INTERNAL_POLLING_THREAD, port, NULL, NULL, handle, NULL,
      MHD_OPTION_THREAD_POOL_SIZE, (unsigned)4, MHD_OPTION_CONNECTION_LIMIT,
      (unsigned)64, MHD_OPTION_CONNECTION_TIMEOUT, (unsigned)30,
      MHD_OPTION_CONNECTION_MEMORY_LIMIT, (size_t)65536,
      MHD_OPTION_NOTIFY_COMPLETED, completed, NULL, MHD_OPTION_END);
  if (!daemon) {
    fprintf(stderr, "Cannot start HTTP server.\n");
    PQfinish(db);
    return 1;
  }
  printf("ShiftNest running at http://localhost:%u\n", port);
  fflush(stdout);
  signal(SIGINT, stop);
  signal(SIGTERM, stop);
  while (running) {
    struct timespec t = {0, 200000000};
    nanosleep(&t, NULL);
  }
  MHD_stop_daemon(daemon);
  PQfinish(db);
  return 0;
}
