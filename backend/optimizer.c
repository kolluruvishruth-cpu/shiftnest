#include "optimizer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now_ms(void) {
  struct timespec t;
  timespec_get(&t, TIME_UTC);
  return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
Wage wage(int minutes, int rate, const Rules *r) {
  Wage w = {0};
  w.regular = minutes < r->ot_threshold ? minutes : r->ot_threshold;
  w.overtime = minutes - w.regular;
  /* Round each weekly regular/overtime component half up, once, to paise. */
  w.regular_pay = ((int64_t)w.regular * rate + 30) / 60;
  w.overtime_pay =
      ((int64_t)w.overtime * rate * r->ot_multiplier + 3000) / 6000;
  w.total = w.regular_pay + w.overtime_pay;
  return w;
}
static int duration(const Shift *s) { return (int)((s->end - s->start) / 60); }
static int minutes_for(const Problem *p, const Solution *s, int e) {
  int m = 0;
  for (int i = 0; i < s->n; i++)
    if (s->a[i].e == e)
      m += duration(&p->s[s->a[i].s]);
  return m;
}
int can_assign(const Problem *p, const Solution *s, int e, int shift) {
  if (e < 0 || e >= p->ne || shift < 0 || shift >= p->ns)
    return 0;
  const Employee *u = &p->e[e];
  const Shift *v = &p->s[shift];
  if (!u->active || !(u->skills & v->skill) ||
      minutes_for(p, s, e) + duration(v) > u->max_minutes)
    return 0;
  int available = 0, count = 0;
  for (int i = 0; i < u->nw; i++)
    if (u->windows[i].start <= v->start && u->windows[i].end >= v->end)
      available = 1;
  if (!available)
    return 0;
  for (int i = 0; i < u->nb; i++)
    if (!(v->end + (int64_t)p->rules.rest * 60 <= u->busy[i].start ||
          u->busy[i].end + (int64_t)p->rules.rest * 60 <= v->start))
      return 0;
  for (int i = 0; i < s->n; i++) {
    if (s->a[i].s == shift)
      count++;
    if (s->a[i].e != e)
      continue;
    const Shift *other = &p->s[s->a[i].s];
    if (s->a[i].s == shift ||
        !(v->end + (int64_t)p->rules.rest * 60 <= other->start ||
          other->end + (int64_t)p->rules.rest * 60 <= v->start))
      return 0;
  }
  return count < v->count;
}
void measure(const Problem *p, Solution *s) {
  s->cost = 0;
  s->required = 0;
  s->filled = s->n;
  s->fairness = 0;
  double total = 0;
  int active = 0;
  double ratios[MAX_E] = {0};
  for (int i = 0; i < p->ns; i++)
    s->required += p->s[i].count;
  for (int i = 0; i < p->ne; i++) {
    int m = minutes_for(p, s, i);
    s->cost += wage(m, p->e[i].rate, &p->rules).total;
    if (p->e[i].active) {
      int cap = p->e[i].preferred > 0 ? p->e[i].preferred : p->e[i].max_minutes;
      ratios[i] = cap ? (double)m / cap : 0;
      total += ratios[i];
      active++;
    }
  }
  double mean = active ? total / active : 0;
  for (int i = 0; i < p->ne; i++)
    if (p->e[i].active)
      s->fairness += (ratios[i] - mean) * (ratios[i] - mean);
  if (active)
    s->fairness /= active;
  /* Cost in rupees / fixed 1000 INR reference; capacity ratio variance is
   * dimensionless. */
  s->objective =
      (double)s->cost / 100000.0 + p->rules.fair_weight / 100.0 * s->fairness;
  s->complete = s->n == s->required;
}
typedef struct {
  const Problem *p;
  Result *r;
  int slots[MAX_A], total;
  double start;
  Solution cur;
  int exhaustive;
} Search;
static int better(const Solution *a, const Solution *b) {
  return a->n > b->n || (a->n == b->n && a->objective < b->objective - 1e-10);
}
static void greedy(const Problem *p, const int *slots, int total, Solution *s,
                   double deadline) {
  memset(s, 0, sizeof(*s));
  for (int k = 0; k < total; k++) {
    if (now_ms() >= deadline)
      break;
    int best = -1;
    double score = 1e100;
    for (int e = 0; e < p->ne; e++)
      if (can_assign(p, s, e, slots[k])) {
        Solution t = *s;
        t.a[t.n++] = (Assignment){e, slots[k]};
        measure(p, &t);
        if (t.objective < score - 1e-10) {
          best = e;
          score = t.objective;
        }
      }
    if (best >= 0)
      s->a[s->n++] = (Assignment){best, slots[k]};
  }
  measure(p, s);
  /* A strict, valid one-assignment replacement descent. */
  for (int pass = 0; pass < 3; pass++) {
    int improved = 0;
    for (int a = 0; a < s->n; a++)
      for (int e = 0; e < p->ne; e++) {
        if (now_ms() >= deadline)
          return;
        Solution t = *s;
        int shift = t.a[a].s;
        t.a[a] = t.a[--t.n];
        if (can_assign(p, &t, e, shift)) {
          t.a[t.n++] = (Assignment){e, shift};
          measure(p, &t);
          if (t.objective < s->objective - 1e-10) {
            *s = t;
            improved = 1;
          }
        }
      }
    if (!improved)
      break;
  }
}
/* Every completion pays at least current wages plus minimum base wage per
   remaining slot. Ignore competition, future overtime, rest and fairness.
   Subtract one paise per slot to account conservatively for weekly rounding. */
static double bound(Search *q, int k) {
  measure(q->p, &q->cur);
  int64_t cost = q->cur.cost;
  for (int j = k; j < q->total; j++) {
    int64_t best = INT64_MAX;
    int s = q->slots[j];
    for (int e = 0; e < q->p->ne; e++)
      if (can_assign(q->p, &q->cur, e, s)) {
        int64_t c = (int64_t)duration(&q->p->s[s]) * q->p->e[e].rate / 60 - 1;
        if (c < best)
          best = c;
      }
    if (best == INT64_MAX)
      return 1e100;
    if (best > 0)
      cost += best;
  }
  return cost / 100000.0;
}
static void dfs(Search *q, int k) {
  if (q->r->nodes >= (uint64_t)q->p->rules.node_limit ||
      now_ms() - q->start >= q->p->rules.time_limit_ms) {
    q->r->limited = 1;
    return;
  }
  q->r->nodes++;
  if (k == q->total) {
    measure(q->p, &q->cur);
    if (better(&q->cur, &q->r->best))
      q->r->best = q->cur;
    return;
  }
  if (q->r->best.complete && bound(q, k) >= q->r->best.objective - 1e-10) {
    q->r->pruned++;
    return;
  }
  int s = q->slots[k], candidates[MAX_E], n = 0;
  for (int e = 0; e < q->p->ne; e++)
    if (can_assign(q->p, &q->cur, e, s))
      candidates[n++] = e;
  for (int i = 1; i < n; i++) {
    int v = candidates[i], j = i;
    while (j > 0 && q->p->e[candidates[j - 1]].rate > q->p->e[v].rate) {
      candidates[j] = candidates[j - 1];
      j--;
    }
    candidates[j] = v;
  }
  if (!n)
    q->r->pruned++;
  for (int i = 0; i < n && !q->r->limited; i++) {
    /* Consecutive identical slots choose increasing employee IDs: symmetry
     * reduction. */
    if (k > 0 && q->slots[k - 1] == s && q->cur.n &&
        q->cur.a[q->cur.n - 1].s == s &&
        q->cur.a[q->cur.n - 1].e >= candidates[i])
      continue;
    q->cur.a[q->cur.n++] = (Assignment){candidates[i], s};
    dfs(q, k + 1);
    q->cur.n--;
  }
}
void optimize(const Problem *p, Result *r, int exact) {
  memset(r, 0, sizeof(*r));
  Search q = {0};
  q.p = p;
  q.r = r;
  q.start = now_ms();
  int order[MAX_S], elig[MAX_S];
  Solution empty = {0};
  for (int s = 0; s < p->ns; s++) {
    order[s] = s;
    elig[s] = 0;
    for (int e = 0; e < p->ne; e++)
      elig[s] += can_assign(p, &empty, e, s);
  }
  for (int i = 1; i < p->ns; i++) {
    int v = order[i], j = i;
    while (j > 0 && (elig[order[j - 1]] - p->s[order[j - 1]].count >
                         elig[v] - p->s[v].count ||
                     (elig[order[j - 1]] - p->s[order[j - 1]].count ==
                          elig[v] - p->s[v].count &&
                      p->s[order[j - 1]].priority < p->s[v].priority))) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = v;
  }
  for (int i = 0; i < p->ns; i++)
    for (int j = 0; j < p->s[order[i]].count && q.total < MAX_A; j++)
      q.slots[q.total++] = order[i];
  greedy(p, q.slots, q.total, &r->greedy, q.start + p->rules.time_limit_ms);
  r->best = r->greedy;
  if (exact)
    dfs(&q, 0);
  else
    r->limited = 1;
  strcpy(r->outcome,
         r->best.complete
             ? (r->limited ? "Feasible, optimality not proven" : "Optimal")
             : (r->limited ? "No feasible solution found within the limit"
                           : "Infeasible, proven by completed search"));
  r->elapsed_ms = now_ms() - q.start;
}
