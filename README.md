# ShiftNest

Employee scheduling and wage optimization for a Design and Analysis of Algorithms course. ShiftNest uses a real C17 Branch and Bound solver, a greedy comparison, PostgreSQL persistence, and a responsive, same-origin web interface.

## Quick start on this computer

If you cloned this repository, install the dependencies under **Dependencies and a clean-machine build** first. Local toolchains, database files, credentials and compiled binaries are intentionally excluded from Git. Configure your own `.env` using `.env.example` before migration and seeding.

The project includes an installed development toolchain under the ignored `.tools/` directory. Node is used only for formatting and browser tests; **the application server, business logic, database access, optimizer and wage calculations are C**.

From PowerShell in this folder:

```powershell
.\scripts\build.ps1
# Local development database (already initialized and seeded on this machine):
.\scripts\start.ps1 -Local
```

Open **http://localhost:8090**. If a server is already running there, use it, or stop that instance with Ctrl+C before starting another.

Demo email: `manager@shiftnest.demo`. Employee emails: `employee1@shiftnest.demo` through `employee12@shiftnest.demo`. The generated demo password is the `SEED_PASSWORD` value in your private `.env`. This password is deliberately excluded from this README and source control. It is shared by demo accounts only. New employees receive a separate manager-supplied password.

To use **Neon** instead:

```powershell
# Configure DATABASE_URL in .env first, as below.
.\scripts\start.ps1 -Migrate -Seed
```

This command performs real migrations and seeds the database named by your URL. Use a dedicated empty ShiftNest database. Subsequent starts need only `start.ps1`.

## Neon configuration

Copy `.env.example` to `.env`. In Neon’s Connect panel copy the PostgreSQL connection URL. Store it only in `.env`, without the `psql` command or surrounding shell quotes:

```dotenv
DATABASE_URL=postgresql://USER:PASSWORD@HOST/DATABASE?sslmode=require&channel_binding=require
PORT=8090
COOKIE_SECURE=0
COMPANY_TIMEZONE=Asia/Kolkata
SEED_PASSWORD=choose-a-strong-demo-password-at-least-12-characters
```

The Neon connection is TLS-compatible through libpq; `sslmode=require` encrypts the transport, and `verify-full` additionally verifies the hostname and certificate chain when the libpq CA store is configured. Use the Neon-provided SSL and channel-binding options. Do not put a real connection string in a source file, command history, frontend, screenshot or commit. Existing process environment variables take precedence over `.env`. The Windows start script clears only `DATABASE_URL` in Neon mode so the private file is used.

`COOKIE_SECURE=1` is required behind HTTPS in production. The C server serves HTTP; terminate HTTPS at a trusted reverse proxy. The default `0` supports local HTTP development. The timezone is set on migration/seed; keep it fixed after creating data. All UI datetime fields are interpreted in that timezone. Database comparisons and week boundaries use PostgreSQL timezone conversion; the solver uses actual UTC timestamps. Weeks begin Monday. Overnight shifts up to 24 hours are supported; a shift crossing the Monday boundary is explicitly rejected. Ambiguous DST wall times use PostgreSQL’s timezone interpretation; the default Asia/Kolkata has no DST.

**Verification status:** Full tests were performed against real local PostgreSQL. Neon’s supplied host resolved but reset its connection before authentication during this session. The Neon URL remains intact in `.env`; Neon connectivity must be rechecked once its endpoint/network allows connections. The app does not silently fall back to local storage or local PostgreSQL: local mode is explicit.

## Dependencies and a clean-machine build

Selected maintained libraries:

| Dependency | Responsibility | Verified version here |
|---|---|---|
| GNU libmicrohttpd | HTTP parsing, limits and worker pool | 1.0.10 |
| PostgreSQL libpq | Parameterized SQL and TLS connections | 18.6 |
| cJSON | JSON parsing and serialization | 1.7.19 |
| libsodium | Argon2id passwords, secure random session/CSRF tokens | 1.0.22 |
| CMake / Ninja / GCC | Build tools, C17 compilation | 4.4.5 / 1.13.2 / 16.2.0 |

Their official references: [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/), [libpq](https://www.postgresql.org/docs/current/libpq.html), [cJSON](https://github.com/DaveGamble/cJSON), [libsodium password hashing](https://doc.libsodium.org/password_hashing/default_phf).

On Windows, install MSYS2 and open its **UCRT64** shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,libmicrohttpd,postgresql,libsodium,cjson}
cd '/c/path/to/ShiftNest'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/shiftnest --migrate
./build/shiftnest --seed
./build/shiftnest
```

Use one toolchain consistently. The old `C:\MinGW` GCC 6 is not the toolchain used here. The PowerShell scripts put this project’s UCRT64 binaries first on PATH.

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config libmicrohttpd-dev libpq-dev libsodium-dev libcjson-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/shiftnest --migrate
./build/shiftnest --seed
./build/shiftnest
```

Run from the project root so the frontend and migration paths resolve. The SQL migration is repeatable and preserves existing records. Seed is separate from production setup and uses a supplied password; it adds one manager, 12 employees with different rates, skills and capacities, seven requirements, daily availability with an exception, and welcome notifications. It preserves existing account passwords on repeat runs.

Optional local PostgreSQL initialization, using PostgreSQL tools on PATH:

```powershell
initdb -D .tools/pgdata -U shiftnest -A trust --encoding=UTF8 --locale=C
pg_ctl -D .tools/pgdata -l .tools/postgres.log -o '-h 127.0.0.1 -p 55432' start
createdb -h 127.0.0.1 -p 55432 -U shiftnest shiftnest
.\scripts\start.ps1 -Local -Migrate -Seed
```

Trust authentication is used only on this loopback-only development cluster. For a shared/server deployment use proper PostgreSQL authentication. Stop this local cluster with `pg_ctl -D .tools/pgdata stop`.

## Architecture and folders

```text
Browser: HTML / CSS / vanilla JavaScript
               │ same-origin JSON + session cookie
               ▼
C17 server: libmicrohttpd → authorization → business rules
               │                         │
               │                   C optimizer + wages
               ▼
libpq → Neon PostgreSQL (or explicitly selected local PostgreSQL)

backend/main.c       HTTP, authentication, authorization, API and transactions
backend/db.c         Database helpers, snapshot loading, migration/seed commands
backend/optimizer.*  Pure C scheduling and wage module
database/            SQL schema, constraints, indexes, revision triggers
frontend/            Responsive HTML, CSS and JavaScript
tests/               C fixtures, API and browser integration checks
scripts/             Windows build and launch helpers
docs/                Professor-facing algorithm and verification notes
```

The diagram’s PostgreSQL connection is one persistent libpq connection guarded by a mutex. Four HTTP workers share that bounded connection. One solver request is allowed at a time; it copies a repeatable-read database snapshot, releases the database mutex during CPU work, and stores the run afterward. Other workers can serve requests while solving. Runtime and node limits are enforced; limits are capped at 10 seconds and 2,000,000 nodes. Search state and best incumbent stay in memory only during solving; results/settings/rates are persisted with the draft.

## Working features

- Managers create/edit/deactivate/search employees, manage skills/rates/capacities and review availability.
- The manager's Employees table includes **Fire employee** with confirmation. This deactivates the account, revokes existing sessions and excludes the employee from new generated schedules. Existing assignments and history remain; regenerate affected planning weeks to replace their assignments. Edit → Active reactivates an account.
- The Employees table defaults to **Active employees**, so fired employees disappear immediately. Select **Inactive employees** to review/reactivate retained accounts; historical schedule names remain available.
- Add requirement accepts an optional custom **Requirement name** and **Or create a new skill**. A newly entered skill overrides the selected existing skill and is created with the requirement in one transaction. Skill names are trimmed and reused case-insensitively. Assign new skills to eligible employees through Employees → Edit before generating a schedule. The C optimizer's bitmask supports 30 distinct skill categories; requirement/skill names may contain up to 100 characters (subject to backend UTF-8 byte limits). Custom names appear in requirement tables, planner cards and history, with the required skill underneath.
- Employees see only their own private account, assignments, wage estimates, notification records and history. Availability outside submitted windows is unavailable; split windows to exclude periods.
- Weekly requirements support editable unscheduled shifts, priority and counts. **Remove** cancels a requirement even when it has draft/published assignments: it disappears from planning, remaining coverage and estimates are recalculated, and employees with a published assignment receive a cancellation notification. Removal preserves shift/assignment history and the original solver result. Repeated requests do not create duplicate notifications. Generate a fresh proposal before publishing again. Times on requirements with history remain immutable; create a replacement to change those times.
- A schedule is saved as a proposed version. The planner shows coverage, employee names, filters, wages, real run metrics and greedy comparison. Validated manual assignment changes are available through forms.
- Only fully staffed, valid drafts may be published. Published versions can later lose an assignment through an explicit cancellation; these are marked incomplete and affected employees are notified.
- Publication, superseding the former publication, and notification insertion are one transaction. Event keys and status checks make publication retries idempotent. People whose prior assignments were removed also receive the new-publication notification.
- In-app notifications support unread counts, read state and acknowledgement. The client polls every 30 seconds; no external delivery is simulated.
- Integer weekly wage estimates show regular/overtime amounts and export to CSV. Rates and overtime settings are snapshotted with each version so later edits do not rewrite historical estimates.

## Security and boundaries

Password hashes use libsodium’s Argon2id API with interactive parameters. Sessions use 256-bit secure random tokens, are stored server-side, expire in 12 hours, and use HttpOnly / SameSite=Strict cookies. Mutating authenticated APIs require an independent CSRF header. Roles and employee ownership are checked in C, not merely hidden in the interface. SQL values are parameterized. Database errors returned to users contain a category/code rather than SQL, credentials or internal messages. Request bodies are capped at 32 KiB; HTTP connections at 64. Eight failed attempts per email per 15 minutes are rate limited; production should add proxy-level per-IP throttling.

Global input revisions cover employee records, skills, availability, shifts and rule edits. Draft publication locks the settings/version rows and revalidates the full schedule. Publication and published edits also invalidate other planning snapshots because adjacent-week rest constraints may change. Full input validation is rerun for manual changes. PostgreSQL foreign keys/checks/unique indexes complement application checks. This is an educational, single-organization application; it has no invitation/reset-email/multi-tenant system.

Supported limits: 64 employees, 128 shifts and 512 staffing positions per week, 64 availability windows and 64 neighbouring published assignments per employee. Oversized snapshots are rejected explicitly. There is no claim of statutory payroll or labour-law compliance. Preferences are preferred weekly hours; optional per-shift preferences are not implemented. A partial greedy fallback respects all hard constraints but does not claim maximum possible partial coverage; incomplete drafts cannot be published. No exhaustive search tree is fabricated or shown.

## Tests

```powershell
.\scripts\build.ps1
# With the seeded local C server running at localhost:8090:
node tests/integration.mjs
node tests/rules.mjs
npm install
npx playwright install chromium
npm run test:browser
# Larger timing fixture (ensure UCRT64 bin is on PATH):
.\build\optimizer_tests.exe --large
```

Browser test tooling is JavaScript only; the backend remains the compiled C executable. API/browser tests modify demo data, so use a dedicated seeded development database. `TEST_URL` changes the target URL. The `.env` seed password is read without printing it. Screenshots and temporary persistence references are stored in ignored `artifacts/`.

On this machine the browser checks used installed Edge with `PLAYWRIGHT_CHANNEL=msedge` and `PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1` (the optional Playwright dependency-probe download was slow). This skips only the tooling’s dependency probe; the browser itself was launched and the full test was executed. The default test channel is downloaded Chromium.

For supported Linux toolchains, enable AddressSanitizer + UBSan with `-DSANITIZE=ON` in a separate build directory. This Windows UCRT64 compiler does not provide the sanitizer runtimes; see the verification report for the checks actually run. See [the algorithm explanation](docs/ALGORITHM.md) and [verification report](docs/VERIFICATION.md).

## Five-minute classroom demonstration

1. **0:00–0:45:** Explain the problem and the browser → C → PostgreSQL architecture. Show the employee rates, skills and availability.
2. **0:45–1:30:** Create a requirement and open the weekly planner. Explain the binary assignment decision and hard constraints.
3. **1:30–2:45:** Generate a Branch and Bound proposal. Read its actual outcome and metrics; compare its objective and wage bill with greedy. Explain the relaxed lower bound and upper-bound incumbent.
4. **2:45–3:30:** Demonstrate a valid manual change or a rejected conflict. Publish a fully staffed version.
5. **3:30–4:15:** Log in as an assigned employee. View the schedule, notification and acknowledgement, and submit availability.
6. **4:15–5:00:** Show integer overtime estimates and CSV export. Explain why exponential search is limited and why timeout is not proof of infeasibility.
