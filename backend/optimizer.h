#ifndef SHIFTNEST_OPTIMIZER_H
#define SHIFTNEST_OPTIMIZER_H
#include <stdint.h>
#define MAX_E 64
#define MAX_S 128
#define MAX_A 512
#define MAX_W 64
typedef struct {
  int64_t start, end;
} Window;
typedef struct {
  int id, active, rate, max_minutes, preferred, skills, nw, nb;
  Window windows[MAX_W], busy[MAX_W];
} Employee;
typedef struct {
  int id, skill, count, priority;
  int64_t start, end;
} Shift;
typedef struct {
  int rest, ot_threshold, ot_multiplier, fair_weight, node_limit, time_limit_ms;
} Rules;
typedef struct {
  int ne, ns;
  Employee e[MAX_E];
  Shift s[MAX_S];
  Rules rules;
} Problem;
typedef struct {
  int e, s;
} Assignment;
typedef struct {
  int regular, overtime;
  int64_t regular_pay, overtime_pay, total;
} Wage;
typedef struct {
  Assignment a[MAX_A];
  int n, filled, required, complete;
  int64_t cost;
  double fairness, objective;
} Solution;
typedef struct {
  Solution best, greedy;
  uint64_t nodes, pruned;
  double elapsed_ms;
  int limited;
  char outcome[64];
} Result;
Wage wage(int minutes, int rate, const Rules *r);
int can_assign(const Problem *p, const Solution *s, int e, int shift);
void measure(const Problem *p, Solution *s);
void optimize(const Problem *p, Result *r, int exact);
#endif
