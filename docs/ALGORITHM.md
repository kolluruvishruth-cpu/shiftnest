# ShiftNest scheduling engine

The optimizer is an independent C17 module (`backend/optimizer.c`). It has no HTTP/database dependency; this lets us test the algorithm directly.

## Formulation

Let E be the employee count, S the shift count, and P = sum of required staffing counts (the number of assignment slots). Decisions are binary x[e,s]. A complete schedule satisfies sum_e x[e,s] = required[s]. Every assignment must match an active employee’s skills, lie inside one full availability window, respect maximum weekly minutes, avoid duplicate/overlapping shifts and leave the configured minimum rest. Neighbouring published weeks contribute fixed busy intervals to enforce rest across Monday boundaries.

Duration is measured in integer elapsed minutes from UTC timestamps; money is integer paise. Capacity-based fairness uses r[e] = assigned_minutes[e] / preferred_minutes[e], with maximum_minutes as fallback. The fairness measure is the population variance of r over all active employees, including employees with zero assignments. Preferred hours must be positive. This models relative workload, not a guarantee that every active employee can work every role.

For complete schedules the objective is:

```text
objective = total_weekly_wages_paise / 100000
          + (fairness_weight_percent / 100) × variance(r)
```

Thus the wage term is measured relative to a fixed ₹1,000 reference. At the default fairness weight of 100%, one full unit of capacity-ratio variance is worth ₹1,000 in the objective. These transparent project weights are configurable; a lower weighted objective can have a higher wage bill. Wages are always shown separately. No per-shift preference penalty is used.

## Weekly wages

For m assigned minutes, threshold T, hourly rate R paise and overtime multiplier M percent:

```text
regular_minutes = min(m,T)
overtime_minutes = max(0,m-T)
regular_paise = floor((regular_minutes × R + 30) / 60)
overtime_paise = floor((overtime_minutes × R × M + 3000) / 6000)
total = regular_paise + overtime_paise
```

Each weekly component is rounded half up once; overtime is calculated across all shifts in the planning week. All products use 64-bit integers. Maximum validated rates, minutes and multipliers fit within these bounds. Wage reports and candidate evaluation call the same function. Rates/settings are snapshotted per schedule version. Attendance, actual payroll, taxes, transfers and statutory deductions are excluded.

## Ordering and greedy baseline

Sort shifts by initial eligible-employee count minus staffing demand, most constrained first; break ties by higher priority, then stable shift order. Expand each requirement into consecutive identical slots. Stable employee order uses database employee IDs.

```text
schedule = empty
for each ordered staffing slot:
    candidates = employees whose addition satisfies every hard constraint
    choose candidate giving the lowest resulting weighted objective
    break ties deterministically by employee order
    add candidate, or leave position unfilled when no candidate exists
measure complete/partial schedule
up to three passes:
    try replacing each assigned employee with every eligible employee
    accept only strict objective improvements with the same coverage
    stop early if no improvement or time budget is reached
```

The local step revalidates all hard constraints. It never drops a filled position to improve cost. The greedy result is a baseline and an initial incumbent; it is **not claimed optimal**. For incomplete coverage this valid partial result is the fallback; ShiftNest does not claim it maximizes partial coverage and blocks its publication.

## Branch and Bound

The search state contains the next slot index k, current assignments, a best incumbent, ordered slot array, explored/pruned counters and start time. Candidate generation recomputes minutes and checks constraints from the assignment list. Candidates are explored in increasing rate order with stable ties. Consecutive identical slots choose increasing employee indexes to avoid equivalent permutations; this does not exclude any distinct employee subset.

```text
best = greedy(problem)
DFS(k, current):
    if runtime or node limit reached:
        mark search limited; return
    increment explored node count
    if k == P:
        measure current
        replace best if complete and its objective is lower
        return
    if best is complete and LOWER_BOUND(current,k) >= best.objective:
        count pruned branch; return
    candidates = feasible employees for slot[k], sorted deterministically
    if none: count infeasible pruning
    for candidate in candidates, eliminating identical-slot permutations:
        add candidate
        DFS(k+1,current)
        remove candidate
        stop if limited
```

The DFS searches complete schedules; it does not branch on deliberately leaving slots vacant. If no full schedule is found, the partial greedy fallback remains. Only complete solutions can be upper bounds for pruning complete solutions. A complete greedy result is the initial upper bound when available.

## Why the lower bound is valid

The implementation adds current weekly wage cost and, independently for each remaining slot, the lowest base-rate duration cost of an employee who can currently take it. It ignores competition among future slots, additional overtime, and future fairness. It subtracts one paise from each per-slot base estimate (and clamps at zero) to allow conservatively for changes in weekly component rounding. The bound is divided by the same 100000 normalization as the objective.

1. Adding assignments cannot make a currently invalid employee valid: availability, skills, activity, capacities, overlap/rest and staffing constraints only restrict further additions.
2. Independently reusing the cheapest employee for multiple future slots relaxes constraints; it cannot overestimate the minimum completion cost.
3. Overtime multipliers are at least 100%, so base-rate pay is a lower bound on extra unrounded wages. For any nonnegative additional minutes, the difference between the two rounded weekly components and the current rounded components can undercut unrounded base increment by less than one paise (only the threshold-crossing case changes both components). The one-paise-per-slot allowance is conservative.
4. Fairness is a nonnegative variance with a nonnegative weight. Using zero as its lower bound is valid. **Current fairness is not used as a future lower bound**, since it can fall when more work is allocated.

If a remaining slot has no currently feasible candidate, that branch cannot lead to a complete schedule. Infeasibility pruning and objective pruning use only these valid relaxations. The test suite compares the result with independent unpruned enumeration on 200 deterministic small problems, in addition to the known-optimum cases.

## Outcomes

| Displayed outcome | Evidence |
|---|---|
| Optimal | Complete incumbent and completed exact search |
| Feasible, optimality not proven | Complete incumbent, but exact search was limited or only greedy was selected |
| Infeasible, proven by completed search | Exact search finished without any complete assignment |
| No feasible solution found within the limit | Search stopped, or greedy alone failed; no complete incumbent |

Manual adjustments explicitly report that optimality is not proven. The UI labels partial drafts incomplete. A timeout by itself never proves infeasibility. Exact proof is deterministic for fixed inputs and an unlimited-enough budget; time limits can vary the work completed across machines. Counters/timings come from the run itself; no synthetic search tree is displayed.

## Complexity

Let W bound availability/busy windows per employee. `can_assign` costs O(P+W) because it scans current assignments and windows. Full objective evaluation costs O(EP+S) because employee workloads are recomputed from the assignment list. The recursive DFS has at most O(E^P) nodes in the naive worst case. Its lower bound examines at most P remaining slots × E employees, each costing O(P+W), so a conservative overall upper bound is O(E^P × [PE(P+W)+EP+S]). Ordering, symmetry reduction and pruning can greatly reduce observed work but do not eliminate exponential worst-case growth.

Greedy plus at most three replacement passes takes O(PE × [EP+S+P+W]) after ordering, with a fixed pass multiplier. Search memory is O(EW+S+P+PE) using the problem snapshot and O(P)-deep recursion with candidate arrays of up to E employees per level. Assignments themselves are shared and pushed/popped, rather than copying the entire search tree. Real time/node limits make larger demonstrations practical; they do not change the theoretical complexity or prove optimality.

## Deterministic fixtures

`tests/optimizer_test.c` provides: (1) independently known ₹200 optimum; (2) unavailable employees, proving infeasibility; (3) overlap/duplicate/rest/max-hour/skill/inactive rejection; (4) higher wages accepted for lower capacity imbalance at higher fairness weight; (5) overtime causing a different preferred assignment; (6) limit outcomes with and without a full incumbent; (7) 200 exhaustive-enumeration comparisons; and (8) a separate `--large` fixture with 12 employees, 21 shifts and 42 positions.
