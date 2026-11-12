#include "optimizer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Problem fixture(void) {
  Problem p = {0};
  p.ne = 2;
  p.ns = 2;
  p.rules = (Rules){0, 2400, 150, 0, 1000000, 10000};
  for (int i = 0; i < 2; i++) {
    p.e[i] = (Employee){.id = i + 1,
                        .active = 1,
                        .rate = 10000 + i * 10000,
                        .max_minutes = 2400,
                        .preferred = 2400,
                        .skills = 1,
                        .nw = 1};
    p.e[i].windows[0] = (Window){0, 1000000};
    p.s[i] = (Shift){i + 1, 1, 1, 1, 3600 + i * 86400, 7200 + i * 86400};
  }
  return p;
}
static double brute_best;
static void brute(const Problem *p, Solution s, int slot) {
  int total = 0, shift = -1;
  for (int i = 0; i < p->ns; i++) {
    if (slot >= total && slot < total + p->s[i].count)
      shift = i;
    total += p->s[i].count;
  }
  if (slot == total) {
    measure(p, &s);
    if (s.complete && s.objective < brute_best)
      brute_best = s.objective;
    return;
  }
  for (int e = 0; e < p->ne; e++)
    if (can_assign(p, &s, e, shift)) {
      s.a[s.n++] = (Assignment){e, shift};
      brute(p, s, slot + 1);
      s.n--;
    }
}
static void compare(Problem *p) {
  Result r;
  optimize(p, &r, 1);
  Solution s = {0};
  brute_best = 1e100;
  brute(p, s, 0);
  assert(!r.limited);
  if (brute_best < 1e99) {
    assert(r.best.complete);
    assert(fabs(r.best.objective - brute_best) < 1e-8);
  } else
    assert(!r.best.complete);
}
int main(int argc, char **argv) {
  Problem p = fixture();
  Result r;
  optimize(&p, &r, 1);
  assert(r.best.complete && r.best.cost == 20000);
  assert(!strcmp(r.outcome, "Optimal"));
  compare(&p);
  puts("PASS independently known optimum: INR 200");
  p = fixture();
  p.e[0].nw = p.e[1].nw = 0;
  optimize(&p, &r, 1);
  assert(!r.best.complete && !r.limited && strstr(r.outcome, "Infeasible"));
  puts("PASS unavailable employees / proven infeasibility");
  p = fixture();
  Solution s = {0};
  assert(can_assign(&p, &s, 0, 0));
  s.a[s.n++] = (Assignment){0, 0};
  assert(!can_assign(&p, &s, 0, 0));
  p.s[1].start = 7100;
  p.s[1].end = 10700;
  assert(!can_assign(&p, &s, 0, 1));
  p.s[1].start = 8000;
  p.s[1].end = 11600;
  p.rules.rest = 60;
  assert(!can_assign(&p, &s, 0, 1));
  p.rules.rest = 0;
  p.e[0].max_minutes = 60;
  assert(!can_assign(&p, &s, 0, 1));
  p.e[1].skills = 2;
  assert(!can_assign(&p, &s, 1, 0));
  p.e[1].skills = 1;
  p.e[1].active = 0;
  assert(!can_assign(&p, &s, 1, 0));
  puts("PASS overlap, duplicate, rest, maximum hours, skill and inactive "
       "checks");
  p = fixture();
  p.rules.ot_threshold = 60;
  p.rules.ot_multiplier = 200;
  p.e[1].rate = 14000;
  optimize(&p, &r, 1);
  assert(r.best.cost == 24000);
  compare(&p);
  Wage w = wage(90, 10001, &p.rules);
  assert(w.regular_pay == 10001 && w.overtime_pay == 10001);
  puts("PASS overtime changes preferred assignment; integer money rounding");
  p = fixture();
  p.e[0].preferred = p.e[1].preferred = 120;
  p.e[1].rate = 11000;
  p.rules.fair_weight = 0;
  optimize(&p, &r, 1);
  int64_t cheap = r.best.cost;
  p.rules.fair_weight = 1000;
  optimize(&p, &r, 1);
  assert(r.best.cost > cheap && r.best.fairness < .01);
  compare(&p);
  puts("PASS explicit cost / fairness tradeoff");
  p = fixture();
  p.rules.node_limit = 1;
  optimize(&p, &r, 1);
  assert(r.limited && r.best.complete &&
         !strcmp(r.outcome, "Feasible, optimality not proven"));
  p.e[0].max_minutes = p.e[1].max_minutes = 60;
  p.ns = 3;
  p.s[2] = (Shift){3, 1, 1, 1, 200000, 203600};
  optimize(&p, &r, 1);
  assert(r.limited && !r.best.complete &&
         !strcmp(r.outcome, "No feasible solution found within the limit"));
  puts("PASS timeout reporting preserves incumbent and never proves "
       "infeasibility");
  srand(42);
  for (int k = 0; k < 200; k++) {
    p = fixture();
    p.ne = 3;
    p.ns = 4;
    p.e[2] = p.e[1];
    p.e[2].id = 3;
    for (int e = 0; e < 3; e++) {
      p.e[e].rate = 5000 + rand() % 20000;
      p.e[e].preferred = 60 + rand() % 180;
      p.e[e].max_minutes = 60 + (rand() % 4) * 60;
      p.e[e].skills = rand() % 4;
      p.e[e].nw = rand() % 2;
    }
    for (int j = 0; j < 4; j++) {
      p.s[j] = (Shift){j + 1,          1, 1 + rand() % 2, 1, 3600 + j * 7200,
                       7200 + j * 7200};
    }
    p.rules.ot_threshold = 60 + rand() % 120;
    p.rules.fair_weight = rand() % 1000;
    p.rules.rest = rand() % 100;
    compare(&p);
  }
  puts("PASS 200 deterministic small instances versus independent exhaustive "
       "enumeration");
  if (argc > 1 && !strcmp(argv[1], "--large")) {
    p = fixture();
    p.ne = 12;
    p.ns = 21;
    p.rules.time_limit_ms = 1000;
    p.rules.node_limit = 500000;
    for (int e = 0; e < 12; e++) {
      p.e[e] = p.e[e % 2];
      p.e[e].id = e + 1;
      p.e[e].rate = 18000 + e * 1500;
    }
    for (int j = 0; j < 21; j++)
      p.s[j] = (Shift){j + 1,
                       1,
                       2,
                       1,
                       3600 + (j / 3) * 86400 + (j % 3) * 18000,
                       18000 + (j / 3) * 86400 + (j % 3) * 18000};
    optimize(&p, &r, 1);
    printf("Larger fixture: %s; nodes=%llu pruned=%llu time=%.2fms "
           "coverage=%d/%d cost=%lld greedy=%lld\n",
           r.outcome, (unsigned long long)r.nodes, (unsigned long long)r.pruned,
           r.elapsed_ms, r.best.n, r.best.required, (long long)r.best.cost,
           (long long)r.greedy.cost);
  }
  puts("All ShiftNest optimizer tests passed.");
  return 0;
}
