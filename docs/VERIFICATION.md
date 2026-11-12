# ShiftNest verification

Verified on Windows with GCC 16.2.0, CMake 4.4.5, libmicrohttpd 1.0.10, libpq/PostgreSQL 18.6, cJSON 1.7.19 and libsodium 1.0.22. The application is a compiled C executable. Browser tests use Playwright driving the installed Microsoft Edge; JavaScript is development tooling and frontend code only.

## Passed

- C17 Debug build with `-Wall -Wextra -Wpedantic`, with no compiler warnings after formatting/fixes.
- Known-optimum C fixture, infeasible availability, skill eligibility, inactive accounts, overlapping/duplicate assignments, minimum rest and weekly maximums.
- Integer wage/overtime calculations and a fixture where overtime changes the preferred assignment.
- Explicit cost/fairness tradeoff and limit outcomes with/without a full incumbent.
- 200 deterministic small problems checked against independent exhaustive enumeration, including multi-person requirements.
- Larger fixture: 12 employees, 21 shifts, 42 staffing slots. One observed one-second run explored 92,394 nodes, pruned 80,864 branches, filled 42/42 positions, and estimated ₹34,320 versus greedy ₹34,380. Outcome: **Feasible, optimality not proven**. These are observed measurements, not theoretical guarantees; reruns can explore a different number of nodes in a fixed time.
- Real PostgreSQL migration and repeatable seeding.
- Authentication, invalid login, manager-only operations, employee data isolation and CSRF rejection.
- Availability submission/removal and persisted state.
- Exact schedule generation, complete coverage, overnight shift support and cross-week shift rejection.
- Shared optimizer/report wage calculations, employee editing and immutable historical rate estimates.
- Publication and notifications in one transaction, idempotent publication retries, schedule changes and cancellation notifications.
- Requirement removal with both draft and published assignment history: active planner/assignments exclude the cancelled shift, wage estimates and coverage are recalculated, history shows the cancellation, original optimization results remain stored, publication of stale drafts is rejected, and retries do not duplicate notifications. `node tests/cancellation.mjs` exercises this in a separate test week; the browser Remove/confirmation flow was also verified on port 8092.
- Employee acknowledgement persisted in PostgreSQL.
- Custom requirement names and custom skills: persistence, case-insensitive skill reuse, employee skill assignment, C optimizer eligibility, and rollback of a new skill when its requirement is invalid. Active-list filtering hides fired employees, while the inactive archive retains them. Verified through `tests/custom-requirements.mjs` and `tests/custom-requirements-browser.mjs`, including planner labels, employee skill checkboxes and mobile overflow checks. Test employees were restored and test requirements removed.
- Stale publication rejection after availability/rate changes.
- Login throttling and oversized request rejection.
- Application stop/rebuild/restart followed by reading the original published assignment and acknowledged notification from PostgreSQL.
- Browser workflow: create staffing requirement → generate → inspect current results → publish → employee login → view and acknowledge notification → submit availability.
- Browser checks at 1440×1040 desktop and 390×844 mobile; no JavaScript page errors or body overflow. Calendar becomes a day-by-day list on narrow screens. Tables scroll inside their containers.
- Visual inspection of generated desktop planner, mobile planner, mobile wage report and employee dashboard screenshots. Focus styling and reduced-motion rules are included; native labels/dialogs and ordinary form buttons support keyboard use.

## Environment limitations

**Neon:** The supplied hostname resolved, but both pooled and direct libpq attempts were reset before authentication/query execution. PostgreSQL TLS/SNI connection options were checked and the private connection string was retained. Neon end-to-end persistence could not be verified from this machine in this session. Tests above ran against a real, explicitly selected loopback PostgreSQL database using the same schema, libpq code and transactions. No browser-storage substitute is used.

**Sanitizers:** An actual `-fsanitize=address,undefined` link attempt failed because this Windows UCRT64 distribution has no `libasan` / `libubsan` runtimes. No sanitizer success is claimed. CMake supports a separate Linux sanitizer build; it was not executed here.

**Production/deployment:** No external deployment was requested or performed. HTTPS must be provided by a reverse proxy and secure cookies enabled for production. External email/SMS/push, attendance-based payroll and legal compliance are outside scope. Browser tests verify ordinary interactions and responsive screenshots; they are not a full assistive-technology audit.

## Reproduce

See the README. Use a dedicated seeded development database: integration/browser tests intentionally create requirements, availability, draft/published versions and notifications. `tests/persistence.mjs` expects the artifact written by `tests/integration.mjs` and should be run after restarting the C application. `tests/rules.mjs` verifies historical wage snapshots and restores the employee rate in a `finally` block.

Screenshots are saved in the ignored `artifacts/` directory. The application is left accessible locally at http://localhost:8090 using explicit local PostgreSQL mode. Demo passwords remain only in the private `.env` file.
