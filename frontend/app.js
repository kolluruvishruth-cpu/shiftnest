"use strict";
const $ = (s) => document.querySelector(s),
  esc = (s) =>
    String(s ?? "").replace(
      /[&<>"']/g,
      (c) =>
        ({
          "&": "&amp;",
          "<": "&lt;",
          ">": "&gt;",
          '"': "&quot;",
          "'": "&#39;",
        })[c],
    );
let data = null,
  week = "",
  page = "overview",
  roleFilter = "",
  employeeFilter = "",
  search = "",
  employeeStatusFilter = "active";
const money = (n) =>
    new Intl.NumberFormat("en-IN", {
      style: "currency",
      currency: "INR",
      maximumFractionDigits: 2,
    }).format((n || 0) / 100),
  hours = (n) => `${(n / 60).toFixed(1)} h`,
  initials = (n) =>
    n
      .split(" ")
      .slice(0, 2)
      .map((x) => x[0])
      .join("");
const date = (s) => new Date(s + "T12:00:00"),
  iso = (d) =>
    `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`,
  display = (s) =>
    date(s.slice(0, 10)).toLocaleDateString("en-IN", {
      day: "numeric",
      month: "short",
    }),
  time = (s) => s.slice(11, 16),
  skill = (id) => data.skills.find((s) => s.id === +id)?.name || "Unassigned",
  employee = (id) => data.employees.find((e) => e.id === +id),
  manager = () => data.user.role === "manager";
const activeVersion = () => data.versions[0] || null,
  assignments = () =>
    data.assignments.filter((a) => a.version_id === activeVersion()?.id),
  filled = () => assignments().length,
  required = () => data.shifts.reduce((a, s) => a + s.count, 0),
  totalWages = () => data.wages.reduce((a, w) => a + w.total, 0);
function toast(message) {
  $("#toast").textContent = message;
  $("#toast").classList.add("show");
  setTimeout(() => $("#toast").classList.remove("show"), 5500);
}
async function api(path, body) {
  const r = await fetch("/api/" + path, {
    method: body ? "POST" : "GET",
    headers: body
      ? {
          "Content-Type": "application/json",
          "X-CSRF-Token": data?.user.csrf || "",
        }
      : {},
    body: body ? JSON.stringify(body) : undefined,
  });
  let j;
  try {
    j = await r.json();
  } catch {
    throw Error("The server returned an unreadable response.");
  }
  if (!r.ok) {
    if (r.status === 401 && path !== "login") showLogin();
    throw Error(j.error || "Request failed.");
  }
  return j;
}
function showLogin() {
  $("#login").hidden = false;
  $("#app").hidden = true;
  data = null;
}
async function refresh(renderPage = true) {
  data = await api("state" + (week ? "?week=" + week : ""));
  week = week || data.settings[0].current_week;
  $("#login").hidden = true;
  $("#app").hidden = false;
  $("#timezone").textContent = data.settings[0].timezone.replace("_", " ");
  $("#profile-name").textContent = manager()
    ? "Nest manager"
    : employee(data.user.employee_id)?.name || "Team member";
  $("#profile-role").textContent = manager()
    ? "Manager workspace"
    : "Employee workspace";
  $("#avatar").textContent = manager()
    ? "NM"
    : initials(employee(data.user.employee_id)?.name || "Team member");
  $("#unread").textContent = data.notifications.filter(
    (n) => !n.read_at && n.user_id === data.user.id,
  ).length;
  if (renderPage) render();
}
function nav() {
  const items = manager()
    ? [
        ["overview", "◫", "Overview"],
        ["planner", "▦", "Schedule planner"],
        ["employees", "♧", "Employees"],
        ["requirements", "≡", "Shift requirements"],
        ["wages", "₹", "Wage estimates"],
        ["notifications", "♧", "Notifications"],
      ]
    : [
        ["overview", "◫", "My week"],
        ["availability", "◷", "Availability"],
        ["notifications", "♧", "Notifications"],
        ["history", "≡", "Past schedules"],
      ];
  $("#nav").innerHTML = items
    .map(
      ([id, icon, label]) =>
        `<button data-page="${id}" class="${page === id ? "active" : ""}"><span class="nav-icon">${icon}</span>${label}</button>`,
    )
    .join("");
  $("#breadcrumb").innerHTML =
    `Workspace / <b>${items.find((i) => i[0] === page)?.[2] || "Overview"}</b>`;
}
function heading(eyebrow, title, subtitle, actions = "") {
  return `<div class="page-heading"><div><span class="eyebrow">${eyebrow}</span><h1>${title}</h1><p>${subtitle}</p></div><div class="actions">${actions}</div></div>`;
}
function period() {
  const end = date(week);
  end.setDate(end.getDate() + 6);
  const v = activeVersion();
  return `<div class="period-bar"><div class="week-control"><button data-action="previous" aria-label="Previous week">←</button><strong>${display(week)} — ${display(iso(end))}, ${end.getFullYear()}</strong><button data-action="next" aria-label="Next week">→</button><button data-action="this-week">Today</button></div><div class="actions"><span class="status ${v?.status === "published" ? "" : "neutral"}">${v ? esc(v.status) + " · v" + v.id : "No schedule yet"}</span>${manager() && v && v.revision !== data.settings[0].revision ? '<span class="status warn">Inputs changed · regenerate</span>' : ""}</div></div>`;
}
function metrics() {
  return `<div class="metrics">${manager() ? metric("ACTIVE TEAM", data.employees.filter((e) => e.active).length, "People with room to contribute") + metric("STAFFING COVERAGE", `${filled()} <span>/ ${required()}</span>`, "Assigned of required positions") : metric("YOUR SCHEDULE", assignments().length, "Published shifts this week") + metric("SCHEDULED HOURS", hours(data.wages[0]?.regular_minutes + (data.wages[0]?.overtime_minutes || 0) || 0), "Your week at a glance")}${metric(manager() ? "ESTIMATED WAGES" : "YOUR ESTIMATED EARNINGS", money(totalWages()), "Based on scheduled hours")}${metric(manager() ? "COVERAGE" : "OVERTIME", manager() ? `${required() ? Math.round((filled() / required()) * 100) : 0}%` : hours(data.wages[0]?.overtime_minutes || 0), manager() ? `${Math.max(0, required() - filled())} positions to fill` : "At the configured overtime rate")}</div>`;
}
function metric(label, value, note) {
  return `<div class="metric"><div class="metric-label">${label}</div><div class="metric-value">${value}</div><small>${note}</small></div>`;
}
function calendar() {
  const ass = assignments();
  let html = "";
  for (let i = 0; i < 7; i++) {
    const d = date(week);
    d.setDate(d.getDate() + i);
    const key = iso(d),
      shifts = data.shifts.filter(
        (s) =>
          s.starts.startsWith(key) &&
          (!roleFilter || s.skill_id === +roleFilter) &&
          (!employeeFilter ||
            ass.some(
              (a) => a.shift_id === s.id && a.employee_id === +employeeFilter,
            )),
      );
    html += `<section class="day ${key === iso(new Date()) ? "today" : ""}"><div class="day-heading"><span>${d.toLocaleDateString("en-IN", { weekday: "short" }).toUpperCase()}</span><strong>${d.getDate()}</strong></div>${shifts
      .map((s) => {
        const people = ass.filter((a) => a.shift_id === s.id),
          missing = manager() ? s.count - people.length : 0;
        return `<article class="shift-card ${missing ? "unfilled" : ""}"><div class="shift-time">${time(s.starts)} – ${time(s.ends)}${s.starts.slice(0, 10) !== s.ends.slice(0, 10) ? " +1d" : ""}</div><div class="shift-role">${esc(s.title || skill(s.skill_id))}${s.title ? `<small class="requirement-skill">${esc(skill(s.skill_id))}</small>` : ""}</div>${people.map((a) => `<span class="person">${esc(employee(a.employee_id)?.name || "Team member")}</span>`).join("")}${missing ? `<div class="vacancy">○ ${missing} unfilled · review eligible team</div>` : ""}${manager() && activeVersion() ? `<button data-action="assign" data-id="${s.id}">Edit assignment ↗</button>` : ""}</article>`;
      })
      .join(
        "",
      )}${!shifts.length ? '<div class="muted small">—</div>' : ""}</section>`;
  }
  return `<div class="calendar">${html}</div><div class="calendar-legend"><span><i class="dot"></i>Assigned</span>${manager() ? '<span><i class="dot terra"></i>Needs coverage</span>' : ""}<span>Times in ${esc(data.settings[0].timezone)}</span></div>`;
}
function upcoming() {
  return `<div class="panel"><div class="panel-heading"><h3>${manager() ? "The week ahead" : "Your upcoming shifts"}</h3><button class="text-link" data-page="${manager() ? "planner" : "history"}">${manager() ? "Open planner" : "Past schedules"} ↗</button></div>${shiftTable(data.shifts.slice(0, 5), false)}</div>`;
}
function shiftTable(shifts, edit) {
  return shifts.length
    ? `<div class="table-wrap"><table><thead><tr><th>Date & time</th><th>Role</th><th>${manager() ? "Coverage" : "Schedule"}</th>${edit ? "<th>Actions</th>" : ""}</tr></thead><tbody>${shifts
        .map((s) => {
          let n = assignments().filter((a) => a.shift_id === s.id).length;
          return `<tr><td><strong>${display(s.starts)}</strong><small>${time(s.starts)} — ${time(s.ends)}</small></td><td>${esc(s.title || skill(s.skill_id))}${s.title ? `<small class="requirement-skill">${esc(skill(s.skill_id))}</small>` : ""}</td><td><span class="status ${n < s.count && manager() ? "warn" : ""}">${manager() ? n + " / " + s.count + " assigned" : "Published"}</span></td>${edit ? `<td><button data-action="edit-shift" data-id="${s.id}">Edit</button> <button class="danger" data-action="delete-shift" data-id="${s.id}">Remove</button></td>` : ""}</tr>`;
        })
        .join("")}</tbody></table></div>`
    : empty(
        "An open week.",
        "Add your first shift requirement to get started.",
      );
}
function empty(title, copy) {
  return `<div class="empty"><strong>${title}</strong>${copy}</div>`;
}
function insights() {
  const v = activeVersion(),
    r = v?.details;
  if (!r)
    return `<div class="panel"><div class="panel-body">${empty("A thoughtful plan starts here.", "Generate a schedule to see measured algorithm results.")}</div></div>`;
  return `<div class="panel"><div class="panel-heading"><h3>Algorithm Insights</h3><span class="status ${r.complete ? "" : "warn"}">${esc(r.outcome)}</span></div><div class="panel-body"><div class="insights"><div class="insights-intro"><span class="eyebrow">THE REASONING BEHIND THE ROSTER</span><h2>${esc(r.method || "Manual adjustment")}</h2><p>Wages and a fair share of work, considered together. Hard constraints always come first.</p><p>Objective = wages ÷ ₹1,000 + ${(v.run_settings.fair_weight / 100).toFixed(2)} × capacity-ratio variance. Lower is better. Fairness compares hours with each person’s preferred hours.</p></div><div><div class="insight-grid">${[
    [
      "Execution",
      r.method === "Manual adjustment" ? "—" : r.elapsed_ms.toFixed(2) + " ms",
    ],
    [
      "Search nodes",
      r.method === "Manual adjustment" ? "—" : r.nodes.toLocaleString(),
    ],
    [
      "Branches pruned",
      r.method === "Manual adjustment" ? "—" : r.pruned.toLocaleString(),
    ],
    ["Staffing coverage", r.filled + " / " + r.required],
    ["Wage estimate", money(r.cost)],
    ["Fairness variance", r.fairness.toFixed(4)],
    ["Objective score", r.objective.toFixed(4)],
  ]
    .map(
      ([label, val]) =>
        `<div><span>${label}</span><strong>${val}</strong></div>`,
    )
    .join(
      "",
    )}</div><div class="comparison">${r.greedy ? `Greedy baseline: ${r.greedy.filled}/${r.greedy.required} filled · ${money(r.greedy.cost)} · objective ${r.greedy.objective.toFixed(4)}.` : "This schedule was manually adjusted. Search metrics and baseline comparisons do not apply to the adjusted roster."}<br>A lower objective can trade a higher wage bill for a more balanced week.${r.complete ? "" : "<br>Incomplete draft: maximum possible partial coverage is not proven."}</div></div></div></div></div>`;
}
function overview() {
  const managerView = manager();
  return (
    heading(
      "MAKE ROOM FOR A GOOD WEEK",
      managerView
        ? "A thoughtful week starts here."
        : "Your week, in a good place.",
      managerView
        ? "A clear view of your people, your shifts, and the week ahead."
        : "Your published shifts, estimated earnings, and updates in one place.",
      `<button class="primary" data-${managerView ? 'page="planner"' : 'action="add-availability"'}>${managerView ? "Open schedule planner ↗" : "Update availability ↗"}</button>`,
    ) +
    period() +
    metrics() +
    (!managerView
      ? `<div class="notice"><strong>${data.next_shift?.[0] ? `Next shift · ${display(data.next_shift[0].starts)} at ${time(data.next_shift[0].starts)}` : "No upcoming published shifts."}</strong>${data.next_shift?.[0] ? ` · ${esc(skill(data.next_shift[0].skill_id))}` : " Your manager will notify you when your next schedule is ready."}</div>`
      : "") +
    (managerView && filled() < required()
      ? `<div class="notice warn">○ <strong>${required() - filled()} positions need a little attention.</strong> Generate a schedule or review availability in the planner.</div>`
      : "") +
    `<div class="two-col"><div>${upcoming()}</div><div class="panel"><div class="panel-heading"><h3>${managerView ? "Recent schedule activity" : "From your workplace"}</h3></div><div class="panel-body">${
      managerView
        ? data.versions
            .slice(0, 5)
            .map(
              (v) =>
                `<div class="availability-row"><span><strong>Schedule v${v.id}</strong><br>${esc(v.details?.outcome || "Draft created")}</span><span class="status ${v.status === "published" ? "" : "neutral"}">${esc(v.status)}</span></div>`,
            )
            .join("") ||
          empty(
            "The week is yours to shape.",
            "No schedule activity for this week yet.",
          )
        : data.notifications
            .slice(0, 3)
            .map(
              (n) =>
                `<div class="availability-row"><span>${esc(n.message)}</span><span class="status ${n.acknowledged_at ? "" : "warn"}">${n.acknowledged_at ? "Acknowledged" : "New"}</span></div>`,
            )
            .join("") ||
          empty("All caught up.", "Your schedule updates will appear here.")
    }</div></div></div>` +
    (managerView
      ? ""
      : `<div class="panel"><div class="panel-heading"><h3>Your weekly schedule</h3><span class="status">${activeVersion() ? "Published" : "Awaiting publication"}</span></div>${calendar()}</div>`)
  );
}
function planner() {
  const v = activeVersion();
  return (
    heading(
      "THE RIGHT PEOPLE, AT THE RIGHT TIME",
      "The weekly planner.",
      "Bring coverage, cost, and a fair share of hours into balance.",
      `<button data-action="generate">✧ Generate schedule</button><button class="primary" data-action="publish" ${!v || v.status === "published" ? "disabled" : ""}>${v?.status === "published" ? "Published ✓" : "Publish schedule ↗"}</button>`,
    ) +
    period() +
    `<div class="planner-toolbar"><div class="filters"><select id="role-filter" aria-label="Filter by role"><option value="">All roles</option>${data.skills.map((s) => `<option value="${s.id}" ${+roleFilter === s.id ? "selected" : ""}>${esc(s.name)}</option>`).join("")}</select><select id="employee-filter" aria-label="Filter by employee"><option value="">All employees</option>${data.employees
      .filter((e) => e.active)
      .map(
        (e) =>
          `<option value="${e.id}" ${+employeeFilter === e.id ? "selected" : ""}>${esc(e.name)}</option>`,
      )
      .join(
        "",
      )}</select></div><span class="small muted">${filled()} / ${required()} positions · ${money(totalWages())} estimated</span></div>${v && !v.details?.complete ? '<div class="notice warn">Incomplete draft. Unfilled positions remain; publication is blocked until every required position has a valid assignment.</div>' : ""}<div class="panel"><div class="panel-heading"><div><h3>A place for everyone</h3><p>Weekly requirements and assigned team members</p></div><button class="text-link" data-action="add-shift">+ Add requirement</button></div>${data.shifts.length ? calendar() : empty("Your week is a blank page.", "Add a shift requirement, then generate your first schedule.")}</div>${insights()}`
  );
}
function employeePage() {
  const visibleEmployees = data.employees.filter(
    (e) =>
      employeeStatusFilter === "all" ||
      (employeeStatusFilter === "active" ? e.active : !e.active),
  );
  return (
    heading(
      "PEOPLE MAKE THE PLAN",
      "Your team.",
      "Skills, availability, and working preferences — all in one place.",
      '<button class="primary" data-action="add-employee">+ Add employee</button>',
    ) +
    `<div class="panel"><div class="panel-heading"><h3>${visibleEmployees.length} ${employeeStatusFilter === "inactive" ? "inactive accounts" : "team members"}</h3><div class="filters"><select id="employee-status-filter" aria-label="Employee account status">${[
      ["active", "Active employees"],
      ["inactive", "Inactive employees"],
      ["all", "All employees"],
    ]
      .map(
        ([value, label]) =>
          `<option value="${value}" ${employeeStatusFilter === value ? "selected" : ""}>${label}</option>`,
      )
      .join(
        "",
      )}</select><input id="employee-search" placeholder="Search your team…" aria-label="Search employees" value="${esc(search)}"></div></div><div class="table-wrap"><table><thead><tr><th>Team member</th><th>Skills</th><th>Hourly rate</th><th>Weekly capacity</th><th>Status</th><th></th></tr></thead><tbody>${visibleEmployees
      .filter((e) =>
        (e.name + " " + e.email).toLowerCase().includes(search.toLowerCase()),
      )
      .map(
        (e) =>
          `<tr><td><div class="name-cell"><span class="initials">${esc(initials(e.name))}</span><div><strong>${esc(e.name)}</strong><small>${esc(e.email)}</small></div></div></td><td>${data.skills
            .filter((s) => e.skills & s.id)
            .map((s) => esc(s.name))
            .join(
              ", ",
            )}</td><td>${money(e.rate)}</td><td>${hours(e.max_minutes)}<small>Preferred ${hours(e.preferred)}</small></td><td><span class="status ${e.active ? "" : "neutral"}">${e.active ? "Active" : "Inactive"}</span></td><td><button data-action="edit-employee" data-id="${e.id}">Edit</button> <button data-action="employee-availability" data-id="${e.id}">Availability</button>${e.active ? ` <button class="danger" data-action="fire-employee" data-id="${e.id}" aria-label="Fire ${esc(e.name)}">Fire employee</button>` : ""}</td></tr>`,
      )
      .join("")}</tbody></table></div></div>`
  );
}
function requirements() {
  return (
    heading(
      "BUILD THE SHAPE OF THE WEEK",
      "Shift requirements.",
      "Set the roles and staffing levels your workplace needs.",
      '<button class="primary" data-action="add-shift">+ Add requirement</button>',
    ) +
    period() +
    `<div class="panel"><div class="panel-heading"><h3>${data.shifts.length} shifts · ${required()} positions</h3><span class="small muted">Overnight shifts supported within one planning week</span></div>${shiftTable(data.shifts, true)}</div>`
  );
}
function wagesPage() {
  return (
    heading(
      "A CLEARER VIEW OF THE NUMBERS",
      "Wage estimates.",
      "Estimates based on scheduled hours. Actual payroll and statutory deductions are outside this project.",
      '<button data-action="export">↓ Export CSV</button><button data-action="settings">Calculation rules</button>',
    ) +
    period() +
    metrics() +
    `<div class="notice">Weekly overtime starts after ${hours(data.wage_rules.ot_threshold)}, at ${(data.wage_rules.ot_multiplier / 100).toFixed(2)}× the hourly rate. Components are rounded once per employee, half up to the nearest paise.</div><div class="panel"><div class="panel-heading"><h3>The weekly wage book</h3><span class="status">INR · estimated</span></div><div class="table-wrap"><table><thead><tr><th>Employee</th><th>Regular hours</th><th>Overtime</th><th>Regular wages</th><th>Overtime wages</th><th>Total estimate</th></tr></thead><tbody>${data.wages.map((w) => `<tr><td><strong>${esc(employee(w.employee_id)?.name)}</strong></td><td>${hours(w.regular_minutes)}</td><td>${hours(w.overtime_minutes)}</td><td>${money(w.regular_pay)}</td><td>${money(w.overtime_pay)}</td><td><strong>${money(w.total)}</strong></td></tr>`).join("")}<tr><td colspan="5"><strong>Company estimate</strong></td><td><strong>${money(totalWages())}</strong></td></tr></tbody></table></div></div>`
  );
}
function notificationsPage() {
  return (
    heading(
      "KEEP EVERYONE IN THE LOOP",
      manager() ? "Team acknowledgements." : "Your notifications.",
      manager()
        ? "See who has reviewed their schedule and acknowledged changes."
        : "A quiet place for the updates that matter.",
    ) +
    `<div class="panel">${data.notifications.map((n) => `<article class="notification ${n.read_at ? "" : "unread"}"><div>${manager() ? `<span class="eyebrow">${esc(n.name || "Manager")}</span>` : ""}<p>${esc(n.message)}</p><small>${new Date(n.created_at).toLocaleString("en-IN")} · ${n.acknowledged_at ? "Acknowledged" : n.read_at ? "Read" : "Unread"}</small></div>${n.user_id === data.user.id && !n.acknowledged_at ? `<div class="actions">${!n.read_at ? `<button data-action="read" data-id="${n.id}">Mark read</button>` : ""}<button data-action="acknowledge" data-id="${n.id}">Acknowledge ✓</button></div>` : `<span class="status ${n.acknowledged_at ? "" : "neutral"}">${n.acknowledged_at ? "Acknowledged" : "Awaiting review"}</span>`}</article>`).join("") || empty("All caught up.", "Schedule updates will appear here after publication.")}</div>`
  );
}
function availabilityPage() {
  return (
    heading(
      "MAKE SPACE FOR YOUR WEEK",
      "Your availability.",
      "Add the full windows when you can work. Time outside these windows is unavailable.",
      '<button class="primary" data-action="add-availability">+ Add available window</button>',
    ) +
    period() +
    `<div class="notice">Only shifts fully inside an availability window can be assigned. To mark time unavailable, remove or split the surrounding available window.</div><div class="panel"><div class="panel-heading"><h3>Available working windows</h3></div><div class="panel-body">${availabilityList(data.user.employee_id)}</div></div>`
  );
}
function availabilityList(eid) {
  return (
    data.availability
      .filter((a) => a.employee_id === +eid)
      .map(
        (a) =>
          `<div class="availability-row"><span>${display(a.starts)} ${time(a.starts)} → ${display(a.ends)} ${time(a.ends)}</span><button class="danger" data-action="delete-availability" data-id="${a.id}" data-employee="${eid}">Remove</button></div>`,
      )
      .join("") ||
    empty(
      "No availability submitted.",
      "Add an available window so your manager can include you.",
    )
  );
}
function historyPage() {
  return (
    heading(
      "A RECORD OF YOUR WORK",
      "Past schedules.",
      "Your published and superseded assignments, retained for reference.",
    ) +
    `<div class="panel"><div class="table-wrap"><table><thead><tr><th>Date</th><th>Time</th><th>Role</th><th>Version</th><th>Status</th></tr></thead><tbody>${data.history.map((s) => `<tr><td>${display(s.starts)}</td><td>${time(s.starts)} — ${time(s.ends)}</td><td>${esc(s.title || skill(s.skill_id))}${s.title ? `<small class="requirement-skill">${esc(skill(s.skill_id))}</small>` : ""}</td><td>v${s.version_id}</td><td><span class="status ${s.cancelled_at ? "warn" : ""}">${s.cancelled_at ? "Cancelled" : "Scheduled"}</span></td></tr>`).join("")}</tbody></table></div>${data.history.length ? "" : empty("Your story starts here.", "Your first published assignment will appear here.")}</div>`
  );
}
function render() {
  nav();
  const views = {
    overview,
    planner,
    employees: employeePage,
    requirements,
    wages: wagesPage,
    notifications: notificationsPage,
    availability: availabilityPage,
    history: historyPage,
  };
  $("#main").innerHTML = (views[page] || overview)();
  document.title = `ShiftNest · ${page === "overview" ? "Your week" : page.charAt(0).toUpperCase() + page.slice(1)}`;
}
function field(label, name, type = "text", value = "", extra = "") {
  return `<label>${label}<input name="${name}" type="${type}" value="${esc(value)}" ${extra}></label>`;
}
function select(label, name, options, value) {
  return `<label>${label}<select name="${name}">${options.map(([id, n]) => `<option value="${id}" ${String(value) === String(id) ? "selected" : ""}>${esc(n)}</option>`).join("")}</select></label>`;
}
function dialog(title, html, submit, label = "Save changes") {
  $("#dialog-title").textContent = title;
  $("#dialog-form").innerHTML =
    html +
    `<p class="error" id="form-error" role="alert"></p><div class="dialog-footer"><button type="button" data-action="cancel-dialog">Cancel</button><button class="primary" type="submit">${label}</button></div>`;
  $("#dialog-form").onsubmit = async (e) => {
    e.preventDefault();
    const button = $("#dialog-form button[type=submit]");
    button.disabled = true;
    $("#form-error").textContent = "";
    try {
      await submit(new FormData(e.target));
      $("#dialog").close();
      await refresh();
      toast("Saved. Your week is up to date.");
    } catch (err) {
      $("#form-error").textContent = err.message;
    } finally {
      button.disabled = false;
    }
  };
  $("#dialog").showModal();
}
function shiftDialog(id = 0) {
  const s = data.shifts.find((s) => s.id === +id) || {
    starts: week + "T09:00",
    ends: week + "T15:00",
    skill_id: 1,
    count: 1,
    priority: 1,
  };
  dialog(
    id ? "Edit requirement" : "A new shift requirement",
    `${field("Requirement name (optional)", "title", "text", s.title || "", 'maxlength="100" placeholder="e.g. Evening inventory check"')}<div class="form-grid">${field("Starts", "starts", "datetime-local", s.starts, "required")}${field("Ends", "ends", "datetime-local", s.ends, "required")}${select(
      "Required skill",
      "skill_id",
      data.skills.map((s) => [s.id, s.name]),
      s.skill_id,
    )}${field("People required", "count", "number", s.count, 'min="1" max="16" required')}${field("Priority (0–10)", "priority", "number", s.priority, 'min="0" max="10" required')}</div>${field("Or create a new skill (optional)", "new_skill", "text", "", 'maxlength="100" placeholder="e.g. Graphic design"')}<p class="form-note">A new skill overrides the selected skill above. Add this skill to eligible employees using Employees → Edit before generating. Times use ${esc(data.settings[0].timezone)}. Overnight shifts up to 24 hours are supported, within one planning week.</p>`,
    (f) =>
      api("shift", {
        id: +id,
        ...Object.fromEntries(
          [...f].map(([k, v]) => [
            k,
            ["count", "skill_id", "priority"].includes(k) ? +v : v,
          ]),
        ),
      }),
  );
}
function employeeDialog(id = 0) {
  const e = employee(id) || {
    rate: 20000,
    max_minutes: 2400,
    preferred: 2400,
    skills: 1,
    active: true,
  };
  dialog(
    id ? "A little about " + e.name : "Welcome someone to the team",
    `${field("Full name", "name", "text", e.name || "", 'required maxlength="100"')}${field("Work email", "email", "email", e.email || "", "required")}<div class="form-grid">${field("Hourly wage (₹)", "rate", "number", e.rate / 100, 'min="0" max="100000" step="0.01" required')}${field("Maximum weekly hours", "max_hours", "number", e.max_minutes / 60, 'min="0.5" max="168" step="0.5" required')}${field("Preferred weekly hours", "preferred_hours", "number", e.preferred / 60, 'min="0.5" max="168" step="0.5" required')}${select(
      "Account status",
      "active",
      [
        [1, "Active"],
        [0, "Inactive"],
      ],
      e.active ? 1 : 0,
    )}</div><label>Eligible skills</label><div class="check-row">${data.skills.map((s) => `<label><input type="checkbox" name="skills" value="${s.id}" ${e.skills & s.id ? "checked" : ""}>${esc(s.name)}</label>`).join("")}</div>${field(id ? "New password (leave blank to keep)" : "Temporary password (12+ characters)", "password", "password", "", 'minlength="12" autocomplete="new-password" ' + (id ? "" : "required"))}`,
    (f) =>
      api("employee", {
        id: +id,
        name: f.get("name"),
        email: f.get("email"),
        rate: Math.round(+f.get("rate") * 100),
        max_minutes: Math.round(+f.get("max_hours") * 60),
        preferred: Math.round(+f.get("preferred_hours") * 60),
        skills: f.getAll("skills").reduce((a, s) => a | +s, 0),
        active: +f.get("active"),
        password: f.get("password"),
      }),
  );
}
function availabilityDialog(eid = data.user.employee_id) {
  dialog(
    "Available working windows",
    `${manager() ? `<p class="form-note">${esc(employee(eid)?.name)}</p>` : ""}${availabilityList(eid)}<div class="form-grid">${field("Available from", "starts", "datetime-local", week + "T08:00", "required")}${field("Available until", "ends", "datetime-local", week + "T18:00", "required")}</div><p class="form-note">Add one continuous available window. Split windows around unavailable periods. All times use ${esc(data.settings[0].timezone)}.</p>`,
    (f) => api("availability", { employee_id: +eid, ...Object.fromEntries(f) }),
    "Add window",
  );
}
function assignDialog(id) {
  const s = data.shifts.find((s) => s.id === +id),
    current = assignments().filter((a) => a.shift_id === +id);
  dialog(
    "Make room in the roster",
    `<p class="form-note">${esc(s.title || skill(s.skill_id))}${s.title ? `<small class="requirement-skill">${esc(skill(s.skill_id))}</small>` : ""} · ${display(s.starts)} · ${time(s.starts)}–${time(s.ends)}. The server validates availability, skills, overlap, weekly hours and rest.</p>${select("Replace or remove an assignment", "old_employee_id", [[0, "Add to an unfilled position"], ...current.map((a) => [a.employee_id, employee(a.employee_id)?.name || "Team member"])], 0)}${select("Assign employee", "employee_id", [[0, "Remove selected assignment"], ...data.employees.filter((e) => e.active).map((e) => [e.id, e.name])], 0)}${activeVersion().status === "published" ? '<div class="notice">Published changes notify the affected employees immediately. Removing an assignment marks the schedule incomplete.</div>' : ""}`,
    (f) =>
      api("assignment", {
        id: activeVersion().id,
        week,
        shift_id: +id,
        old_employee_id: +f.get("old_employee_id"),
        employee_id: +f.get("employee_id"),
      }),
  );
}
function generateDialog() {
  dialog(
    "Find a balanced week.",
    `${select(
      "Scheduling method",
      "method",
      [
        ["exact", "Branch and Bound · exact search"],
        ["greedy", "Greedy baseline + local improvement"],
      ],
      "exact",
    )}<div class="form-grid">${field("Search time limit (ms)", "time_limit_ms", "number", 2000, 'min="1" max="10000" required')}${field("Search node limit", "node_limit", "number", 200000, 'min="1" max="2000000" required')}</div><p class="form-note">A new draft is created. The best valid schedule is retained if the search reaches its limit. A timeout does not prove infeasibility.</p>`,
    async (f) => {
      const button = $("#dialog-form button[type=submit]");
      button.innerHTML = '<span class="loading"></span>Finding a good fit…';
      try {
        await api("generate", {
          week,
          method: f.get("method"),
          time_limit_ms: +f.get("time_limit_ms"),
          node_limit: +f.get("node_limit"),
        });
      } finally {
        button.textContent = "Generate draft";
      }
    },
    "Generate draft",
  );
}
function settingsDialog() {
  const s = data.settings[0];
  dialog(
    "The rules behind the week.",
    `<div class="form-grid">${field("Minimum rest (minutes)", "rest_minutes", "number", s.rest_minutes, 'min="0" max="1440" required')}${field("Overtime threshold (minutes)", "ot_threshold", "number", s.ot_threshold, 'min="1" max="10080" required')}${field("Overtime multiplier (%)", "ot_multiplier", "number", s.ot_multiplier, 'min="100" max="500" required')}${field("Fairness weight (%)", "fair_weight", "number", s.fair_weight, 'min="0" max="10000" required')}</div><p class="form-note">Company project rules; no claim of labour-law compliance. Changing these rules makes existing proposals stale.</p>`,
    (f) => api("settings", Object.fromEntries([...f].map(([k, v]) => [k, +v]))),
  );
}
function exportCSV() {
  const q = (s) => {
    let value = String(s);
    if (/^[=+@-]/.test(value)) value = "'" + value;
    return '"' + value.replace(/"/g, '""') + '"';
  };
  const lines = [
    [
      "Employee",
      "Regular hours",
      "Overtime hours",
      "Regular wages INR",
      "Overtime wages INR",
      "Total estimate INR",
    ],
    ...data.wages.map((w) => [
      employee(w.employee_id)?.name,
      w.regular_minutes / 60,
      w.overtime_minutes / 60,
      w.regular_pay / 100,
      w.overtime_pay / 100,
      w.total / 100,
    ]),
  ];
  const url = URL.createObjectURL(
    new Blob(["\uFEFF" + lines.map((l) => l.map(q).join(",")).join("\r\n")], {
      type: "text/csv;charset=utf-8",
    }),
  );
  const a = document.createElement("a");
  a.href = url;
  a.download = `ShiftNest-wages-${week}.csv`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
document.addEventListener("click", async (event) => {
  const el = event.target.closest("[data-action],[data-page]");
  if (!el) return;
  if (el.dataset.page) {
    page = el.dataset.page;
    render();
    return;
  }
  const action = el.dataset.action,
    id = +el.dataset.id;
  try {
    if (action === "cancel-dialog") $("#dialog").close();
    else if (action === "previous" || action === "next") {
      const d = date(week);
      d.setDate(d.getDate() + (action === "next" ? 7 : -7));
      week = iso(d);
      await refresh();
    } else if (action === "this-week") {
      week = data.settings[0].current_week;
      await refresh();
    } else if (action === "generate") generateDialog();
    else if (action === "add-shift" || action === "edit-shift") shiftDialog(id);
    else if (action === "add-employee" || action === "edit-employee")
      employeeDialog(id);
    else if (action === "fire-employee") {
      const person = employee(id);
      dialog(
        "Fire employee?",
        `<p>Deactivate <strong>${esc(person.name)}</strong>? They will lose access to ShiftNest and will no longer be eligible for new schedules.</p><p>Past records and existing assignments are retained. Regenerate affected schedules to replace their assignments. You can reactivate this account using Edit.</p>`,
        () => api("employee-status", { id, active: 0 }),
        "Fire employee",
      );
      $("#dialog-form button[type=submit]").classList.add("danger");
    } else if (
      action === "add-availability" ||
      action === "employee-availability"
    )
      availabilityDialog(id || data.user.employee_id);
    else if (action === "assign") assignDialog(id);
    else if (action === "settings") settingsDialog();
    else if (action === "export") exportCSV();
    else if (action === "publish") {
      dialog(
        "Ready for your team?",
        `<p>Publish schedule v${activeVersion().id} for ${display(week)}? Your team will receive an in-app notification. This replaces the previously published version for this week.</p>`,
        () => api("publish", { id: activeVersion().id, week }),
        "Publish schedule",
      );
    } else if (action === "delete-shift") {
      dialog(
        "Remove this requirement?",
        "<p>This removes the requirement from planning and cancels its active assignments. Employees with a published assignment will receive a cancellation notification. Past records are kept. Generate a new schedule afterward.</p>",
        () => api("shift", { id, action: "delete" }),
        "Remove requirement",
      );
    } else if (action === "delete-availability") {
      await api("availability", {
        id,
        employee_id: +el.dataset.employee,
        action: "delete",
      });
      await refresh();
      if ($("#dialog").open) availabilityDialogRefresh(+el.dataset.employee);
      toast("Availability removed.");
    } else if (action === "read" || action === "acknowledge") {
      await api("notification", {
        id,
        action: action === "read" ? "read" : "acknowledge",
      });
      await refresh();
      toast(
        action === "read"
          ? "Marked as read."
          : "Schedule acknowledged. Thank you.",
      );
    }
  } catch (e) {
    toast(e.message);
  }
});
function availabilityDialogRefresh(eid) {
  $("#dialog").close();
  availabilityDialog(eid);
}
document.addEventListener("change", (e) => {
  if (e.target.id === "employee-status-filter") {
    employeeStatusFilter = e.target.value;
    render();
  }
  if (e.target.id === "role-filter") {
    roleFilter = e.target.value;
    render();
  }
  if (e.target.id === "employee-filter") {
    employeeFilter = e.target.value;
    render();
  }
});
document.addEventListener("input", (e) => {
  if (e.target.id === "employee-search") {
    search = e.target.value;
    const pos = e.target.selectionStart;
    render();
    $("#employee-search").focus();
    $("#employee-search").setSelectionRange(pos, pos);
  }
});
$("#close-dialog").onclick = () => $("#dialog").close();
$("#notification-shortcut").onclick = () => {
  page = "notifications";
  render();
};
$("#logout").onclick = async () => {
  try {
    await api("logout", {});
    week = "";
    page = "overview";
    roleFilter = employeeFilter = search = "";
    employeeStatusFilter = "active";
    showLogin();
  } catch (e) {
    toast(e.message);
  }
};
$("#login-form").onsubmit = async (e) => {
  e.preventDefault();
  const button = e.target.querySelector("button");
  button.disabled = true;
  button.innerHTML = '<span class="loading"></span>Signing in…';
  $("#login-error").textContent = "";
  try {
    await api("login", Object.fromEntries(new FormData(e.target)));
    e.target.reset();
    await refresh();
  } catch (err) {
    $("#login-error").textContent = err.message;
  } finally {
    button.disabled = false;
    button.innerHTML = "Sign in <span>↗</span>";
  }
};
refresh().catch((e) => {
  showLogin();
  if (!e.message.includes("sign in")) $("#login-error").textContent = e.message;
});
setInterval(() => {
  if (data && !document.hidden && !$("#dialog").open)
    refresh(page === "notifications").catch(() => {});
}, 30000);
