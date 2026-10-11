# NetPulse — UI/UX Wireframe Specification

Native Linux desktop network monitoring dashboard.
C17 · GTK 4 · GLib/GIO · Cairo · CMake. Desktop-first, no web technology.

This document is the implementation contract for the interface: component
placement, states, behavior, and the mapping from each region to concrete
GTK 4 widgets. It is ordered in nine sections — design overview, main window,
per-page wireframes, shared components, navigation and interaction,
responsive behavior, UI states and errors, GTK 4 mapping, review checklist.

Illustrative numbers below are **wireframe placeholders only**. The running
application displays genuine measurements or an explicit empty/unknown state;
it never renders invented statistics.

---

## 1. Design overview

### 1.1 Direction
A professional network-administration utility: quiet chrome, dense but
scannable data, and status communicated redundantly (color + icon + words) so
meaning never depends on hue alone. Restraint over decoration — no gradients,
no glassmorphism, no decorative animation, no oversized headings. Motion is
limited to one functional transition (sidebar collapse).

### 1.2 Design principles
1. **Real or nothing.** Every number traces to a syscall, a kernel counter
   delta, or a measured timestamp. Unmeasured values render `n/a`.
2. **Status is a system, not a style.** Seven named states with fixed wording
   (§7.1), reused verbatim on every page.
3. **Density with air.** Compact rows, generous section spacing, one clear
   focus per region.
4. **Chrome recedes, data advances.** Muted borders, low-contrast secondary
   text, high-contrast primary values.
5. **No dead controls.** Every visible control performs a real action.
6. **Never block the user.** Tests run on worker threads; sampling runs on a
   main-loop timer that never stalls on I/O.

### 1.3 Visual system

**Color**

| Token | Dark | Light | Use |
|---|---|---|---|
| `bg` | `#1e222a` | `#f2f3f5` | sidebar, window |
| `base` | `#262b35` | `#ffffff` | cards, inputs, chart |
| `fg` | `#e6e9ee` | `#1c1e21` | primary text |
| `borders` | `#3a4150` | `#d5d8de` | card/divider edges |
| `accent` | `#3584e4` | `#3584e4` | selection, focus ring |
| `ok` | `#2ca02c` / `#1d7a38` | same | connected, success |
| `warn` | `#c77700` / `#9a6a00` | same | degraded, warning |
| `err` | `#b3261e` | same | disconnected, failure |
| chart Dl | `#33b359` | `#33b359` | download series |
| chart Ul | `#408cf2` | `#408cf2` | upload series |

**Typography**
Page title 15 px/600 · section heading 14 px/700 · card title 11 px uppercase,
+0.6 px tracking, 65% opacity · card value 21 px/700 tabular numerals ·
body 12–13 px · monospace 12 px for addresses, logs, and counts.

**Spacing scale**
4 / 6 / 8 / 10 / 12 / 16 / 24 px. Card gap 16 px, section gap 12 px, page
padding 24 px horizontal / 20 px vertical, card padding 10–12 px, card radius
10 px, pill radius 999 px.

**Chrome targets (1440×900; flexible)**
Sidebar 230 px expanded / 68 px collapsed · header bar ~56 px · default window
1440×900 · minimum comfortable 1024×768.

---

## 2. Main window wireframe

Three regions: left sidebar, top toolbar (header bar), main content stack.

### 2.1 Expanded sidebar

```
+---------------------------+----------------------------------------------+
| [logo] NetPulse           | [=] Overview          Connected [refresh] [Pause] [Theme] [gear]
+---------------------------+----------------------------------------------+
|  [#] Overview             |                                                      |
|  [#] Network Interfaces   |                                                      |
|  [o] Connectivity Tests   |                 M A I N   C O N T E N T        |
|  [>] Traffic Monitor      |                  (GtkStack, one page          |
|  [o] Diagnostics & Logs   |                   visible at a time)           |
|                           |                                                      |
|  [g] Settings             |                                                      |
|  [?] About NetPulse       |                                                      |
+---------------------------+----------------------------------------------+
   230 px
```

Regions: **brand** (logo + wordmark; wordmark hides when collapsed) ·
**nav list** (7 rows, single selection, active row = accent background + inset
left bar) · **toggle** (header bar, drives collapse) · **status cluster**
(status pill, refresh, pause) · **utilities** (theme selector, settings jump).

### 2.2 Collapsed sidebar

```
+-------+----------------------------------------------------------+
| [logo]| [>] Overview                Connected [refresh] [Pause] [Theme] [gear] |
+-------+----------------------------------------------------------+
|  [#]  |                                                      |
|  [#]  |                                                      |
|  [o]  |                 M A I N   C O N T E N T             |
|  [>]  |                                                      |
|  [o]  |                                                      |
|  [g]  |                                                      |
|  [?]  |                                                      |
+-------+
  68 px
```

Collapsed invariants: width 68 px · icons only, 16 px, horizontally centered ·
labels hidden (`gtk_widget_set_visible`) · **logo still visible** · row tooltips
present · `GTK_ACCESSIBLE_PROPERTY_LABEL` unchanged · active-page indicator
(accent bar + background) preserved · selected page preserved · content
reflows into the freed space without clipping.

---

## 3. Page wireframes

### 3.1 Overview Dashboard

```
+--------------------------------------------------------------------------+
| [=] Overview                       Connected  [refresh] [Pause] [Theme]  |
+--------------------------------------------------------------------------+
| +----------------+ +---------------+ +----------------+ +---------------+ |
| | CONNECTIVITY   | | ACTIVE IFACE  | | LATENCY        | | PACKET LOSS   | |
| | Connected      | | wlp61s0       | | 24 ms          | | n/a           | |
| | Local link up  | | Default route | | Last check     | | not measured  | |
| +----------------+ +---------------+ +----------------+ +---------------+ |
| +----------------+ +---------------+                                        |
| | DOWNLOAD       | | UPLOAD        |                                        |
| | 12.4 MB/s      | | 2.1 MB/s      |                                        |
| +----------------+ +---------------+                                        |
| +--------------------------------------------------------------------------+ 
| | LIVE NETWORK TRAFFIC                              [Download] [Upload]     |
| |  max |                                            green line             |
| |      |                              /\/\___/\/\      blue line            |
| |      |____    ______/\______/\/                  ...                     |
| |    0  +--------------------------- -120s  -90s  -60s  -30s  -0s        |
| +--------------------------------------------------------------------------+ 
| +-----------------------------+ +----------------------------------------+ |
| | NETWORK INTERFACES          | | RECENT EVENTS                         | |
| | wlp61s0  up   192.168.1.42   | | 23:04:11  [INFO]  check succeeded    | |
| | eth0     down no IP          | | 23:03:02  [WARN]  ping not permitted  | |
| | lo       up   127.0.0.1      | | Last check: 23:04:11                 | |
| +-----------------------------+ +----------------------------------------+ |
+--------------------------------------------------------------------------+
```

Hierarchy: status → headline rates → trend → detail. The four required metrics
are always present even when unavailable, because "unknown" is information.
Packet loss shows `n/a` with the reason "not measured by current tests" — the
test set does not compute loss, and the UI says so rather than showing 0%.

### 3.2 Network Interfaces

```
+--------------------------------------------------------------------------+
| [=] Network Interfaces                            Connected [refresh] ... |
+--------------------------------------------------------------------------+
| INTERFACE            | SELECTED INTERFACE — wlp61s0                        |
| [o] wlp61s0   up  [+] | Type        wifi                                      |
| [o] eth0     down    | State       up (up)                                  |
| [o] docker0  up  [+] | IPv4        192.168.1.42                             |
| [o] lo       up  [o] | IPv6        —                                        |
|                     | MAC         d0:39:57:61:c1:27                        |
|                     | Received    1.2 GB                                    |
|                     | Transmitted 845 MB                                    |
|                     | Inbound     1.4 MB/s                                 |
|                     | Outbound    320 KB/s                                  |
+--------------------------------------------------------------------------+
```

List rows show name + icon + up/down pill. Selecting a row updates the details
panel and re-targets the traffic monitor (persisted as the selected interface).
Empty state: "No interfaces found. Check permissions or virtual devices."

### 3.3 Connectivity Tests

```
+--------------------------------------------------------------------------+
| [=] Connectivity Tests                                                   |
+--------------------------------------------------------------------------+
| Target host / IP   [8.8.8.8____________________________]                 |
| Test type          [ DNS resolution          v ]                          |
| Port (TCP only)    [ 53  ] (sensitive unless TCP)                         |
| Timeout (ms)       [3000 ]                                                 |
| [ Start Test ] (o)  Idle — no test running.                               |
+--------------------------------------------------------------------------+
| TEST HISTORY                                                             |
| 23:04:11  DNS  8.8.8.8       OK      12.4 ms                            |
| 23:04:02  TCP  example.com:443 FAIL   Connection refused                |
| 23:03:58  PING 1.1.1.1       FAIL    'ping' is not permitted             |
+--------------------------------------------------------------------------+
| hint: DNS uses the system resolver, TCP opens a real socket, ping needs   |
|       the 'ping' utility plus ICMP permission.                            |
+--------------------------------------------------------------------------+
```

Behavior: validation rejects empty/invalid hosts, out-of-range ports and
timeouts with a specific message (never a fake failure). While a test runs the
button disables and a spinner shows; results return on the main loop and
prepend to history (capped at 50 rows).

### 3.4 Traffic Monitor

Chart is the primary element; controls are a single compact row above it.

```
+--------------------------------------------------------------------------+
| [=] Traffic Monitor                                                      |
+--------------------------------------------------------------------------+
| Interface [wlp61s0 v]  Sample (ms) [1000]  [Pause] [Clear]               |
+---------------------------------+----------------------------------------+
| DOWNLOAD 12.4 MB/s              | UPLOAD  2.1 MB/s                       |
+---------------------------------+----------------------------------------+
|                                                                         |
|  max |                (download)                                        |
|      |                                        /‾\__/‾\__    (upload)         |
|      |____   ____/‾\____/‾\____/                                          |
|    0 +------------------------- -120s -90s -60s -30s -0s                 |
+--------------------------------------------------------------------------+
| Peak download 18.2 MB/s · Peak upload 3.4 MB/s · Samples 120 · PAUSED    |
+--------------------------------------------------------------------------+
```

Axis labels carry real units (B/s → GB/s); the legend uses colored series
names matching the plotted lines.

### 3.5 Diagnostics and Logs

Compact, table-like rows — deliberately not oversized cards.

```
+--------------------------------------------------------------------------+
| [=] Diagnostics & Logs                                                   |
+--------------------------------------------------------------------------+
| [ Search messages…        ] [All severities v] [Category] [Export CSV] [Clear Logs]  12 event(s) |
+--------------------------------------------------------------------------+
| 2026-10-11 23:04:11  INFO     connectivity  DNS 8.8.8.8 resolved in 12 ms  |
| 2026-10-11 23:04:02  WARNING  connectivity  ping not permitted            |
| 2026-10-11 23:03:20  INFO     traffic      monitor switched to wlp61s0    |
+--------------------------------------------------------------------------+
```

Severity, category, and free-text filters combine; results are newest-first,
capped at 200 visible rows. Clear Logs asks for confirmation. Export writes CSV
with headers and reports success/failure via toast.

### 3.6 Settings

```
+--------------------------------------------------------------------------+
| [=] Settings                                                             |
+--------------------------------------------------------------------------+
| Appearance            [ System v ]                                       |
| Monitoring interval   [ 2000 ] ms      Sampling interval [ 1000 ] ms       |
| Chart history         [ 120  ] s                                          |
| Connectivity target   [ 8.8.8.8 ] host   Port [ 53 ]   Timeout [3000] ms  |
| Latency warning       [ 250 ] ms      [x] Enable warnings                |
+--------------------------------------------------------------------------+
| [Save Settings] [Restore Defaults] [Export Interfaces CSV]                |
| Settings saved.                                                           |
+--------------------------------------------------------------------------+
```

Validation runs before saving; failures render an inline message and leave the
stored preferences untouched. Saved state is confirmed in the status line.

### 3.7 About

Icon, name, version, an honest description, the technology stack, license
pointer, and a limitations statement. No invented repository URLs or
certifications.

```
+--------------------------------------------------------------------------+
| [logo]                                                                   |
| NetPulse                                                                 |
| Version 0.1.0                                                            |
| Native Linux network monitoring... All measurements come from live      |
| system interfaces — no simulated data is ever presented as real.          |
| TECHNOLOGY                                                                |
| C17 · GTK 4 · GLib/GIO · Cairo · CMake/Ninja · GCC · g_test              |
| LICENSE                                                                   |
| MIT License — see the LICENSE file.                                      |
| LIMITATIONS                                                               |
| ICMP needs the ping utility and permission; virtual interfaces may have  |
| no IP; rates derive from kernel counters at the sampling interval.       |
+--------------------------------------------------------------------------+
```

---

## 4. Shared component specifications

| Component | Purpose | Behavior |
|---|---|---|
| **Status pill** | Overall monitoring state | Text + semantic color (ok/warn/err/muted); never color alone |
| **Metric card** | One headline value | Title (uppercase), value (21 px tabular), subtitle (reason/context); optional state border |
| **Section label** | Region heading | 14 px/700, left aligned, muted |
| **Nav row** | Page link | Icon + label; selected = accent fill + inset bar; hover/focus rings |
| **Sidebar** | Navigation shell | Expand/collapse, tooltips, accessible names, persistence |
| **Chart** | Time series | Cairo drawing area; download/upload series, unit-aware Y axis, relative X axis, empty state |
| **Log row** | Diagnostic entry | Monospace, timestamp/severity/category/message, severity coloring |
| **Filter control** | Narrow the list | Search entry, severity drop-down, category entry; live re-query |
| **Spin button** | Numeric input | Clamped range, unit suffix in the label |
| **Drop-down** | Enumerated choice | Interface, test type, theme, severity |
| **Switch** | Boolean | Alert enablement |
| **Buttons** | Actions | Primary (`suggested-action`), secondary, destructive (`destructive-action`) with confirmation |
| **Dialog** | Blocking decision | Alert dialog (destructive), file dialog (export); both async |
| **Toast** | Transient feedback | Bottom-center revealer, auto-hide 3 s |
| **Spinner** | In-flight work | Shown while a test runs; button disabled meanwhile |
| **Empty state** | Nothing to show | Explains cause and the next action; never fake data |

---

## 5. Navigation and interaction flow

- Sidebar click → stack page switch + header title update + row highlight.
- Header title carries a sidebar toggle; its icon and tooltip reflect state
  (Collapse ↔ Expand).
- Toggle collapses/expands with a ~180 ms width transition; the active page is
  preserved because page state lives in the window, not the sidebar.
- Settings gear jumps to Settings from any page.
- Theme selector applies and persists immediately.
- Refresh re-scans now; Pause/Resume freezes sampling and labels state.
- Interface selection updates details panel, traffic monitor target, and the
  persisted selection.
- Start Test runs asynchronously; result prepends to history and updates the
  Overview latency card.
- Destructive actions (Clear Logs) require confirmation; export reports outcome.

State ownership: the sidebar owns presentation only; `NpWindowState` owns the
active page, selection, and monitoring flags. This separation is why collapsing
can never reset the active page.

---

## 6. Responsive behavior

| Width | Behavior |
|---|---|
| ≥ 1400 px | Full layout: 3–4 metric columns, two-column bottom row |
| 1200–1399 px | Cards keep grid; bottom row stays side by side |
| 1024–1199 px | Metric grid compacts; bottom row stacks vertically; sidebar 230 px |
| < 1024 px | Sidebar auto-collapses to 68 px (user toggle still available); content scrolls |

Rules: no absolute positioning; content scrolls rather than clipping; charts
keep a minimum drawable size; long hostnames, IPv6 addresses, and error text
wrap; tables scroll vertically with the page.

---

## 7. UI states and error handling

### 7.1 Status vocabulary (fixed app-wide)

| State | Wording | Treatment |
|---|---|---|
| Connected | `Connected` | green pill, "Local link up" |
| Disconnected | `Disconnected` | red pill, "No active link detected" |
| Checking | `Checking…` | muted + spinner |
| Paused | `Paused` | muted pill, chart labelled PAUSED |
| Degraded / Warning | `Warning` | amber |
| Failed | `Failed` | red |
| Unknown | `Unknown` / `n/a` | muted, with a reason |

### 7.2 Per-page states

| Page | Loading | Empty | Error |
|---|---|---|---|
| Overview | "Scanning…" then live values | "No interfaces found" | scan failure logged as warning; pill → Unknown |
| Interfaces | list rebuilt each tick | "No interfaces found…" | details → "No interface data available." |
| Connectivity | spinner + disabled button | no results yet | exact resolver/socket/permission message inline |
| Traffic | "No data yet" in chart | sample count 0 | unavailable counters shown as `n/a` |
| Logs | "No events yet." | "No log entries match the current filters." | export failure toast + error log |
| Settings | n/a | n/a | inline validation message, nothing saved |
| About | n/a | n/a | n/a |

### 7.3 Failure messages that must stay specific
Missing `ping` binary vs. missing ICMP permission (exit 2) are distinguished ·
DNS failure carries the resolver message · TCP failure distinguishes refusal
from timeout · export distinguishes permission from path errors.

---

## 8. GTK 4 implementation mapping

| Region / component | GTK 4 widget | Notes |
|---|---|---|
| Window chrome | `GtkHeaderBar` | title widget, pack start/end |
| Sidebar shell | `GtkBox` (vertical) + qdata state | `include/netpulse/sidebar.h`, `src/ui/sidebar.c` |
| Brand | `GtkBox` + `GtkImage` + `GtkLabel` | wordmark hidden when collapsed |
| Nav list | `GtkListBox` + `GtkListBoxRow` | `row-selected` → navigation callback |
| Row icons | `GtkImage` from icon name | `np_icon()` resolves theme fallbacks |
| Collapse animation | `g_timeout_add` tick → `gtk_widget_set_size_request` | ~180 ms ease-out, non-blocking |
| Tooltips / a11y | `gtk_widget_set_tooltip_text`, `gtk_accessible_update_property` | labels survive collapse |
| Pages | `GtkStack` + per-page `GtkScrolledWindow` | one page visible at a time |
| Metric card | `GtkFrame` + `GtkBox` + `GtkLabel`s | CSS classes `.np-card*` |
| Chart | `GtkDrawingArea` + `gtk_drawing_area_set_draw_func` (Cairo) | `src/ui/chart.c` |
| Tables / lists | `GtkBox` rows in a `GtkScrolledWindow` | compact monospace rows |
| Forms | `GtkEntry`, `GtkSearchEntry`, `GtkSpinButton`, `GtkDropDown`, `GtkSwitch` | validated before save |
| Dialogs | `GtkFileDialog` / `GtkAlertDialog` (GTK ≥ 4.10), legacy fallbacks otherwise | version-guarded in `src/ui/widgets.c` |
| Toast | `GtkRevealer` overlay | auto-hide timer |
| Busy | `GtkSpinner` | disables the triggering control |
| Styling | `GtkCssProvider` + `resources/styles/*.css` | light/dark/system |
| Persistence | `GKeyFile` at `~/.config/netpulse/netpulse.conf` | includes `sidebar-collapsed` |
| Async work | `GThread` + `g_idle_add` | GTK touched only on the main loop |

Threading rule: monitoring logic never calls GTK. Widgets are updated only from
main-loop callbacks.

---

## 9. Review checklist

**Structure**
- [ ] Three-region layout present on every page; sidebar and header identical
- [ ] All 7 nav items present, ordered, single selection, active state visible
- [ ] Page title updates on navigation

**Sidebar**
- [ ] Toggle expands/collapses; icon and tooltip match state
- [ ] Collapsed: icons only, logo kept, tooltips present, active indicator kept
- [ ] Selected page preserved through toggling
- [ ] Content reflows with no clipping or overlap
- [ ] Preference persists across restarts; missing/invalid key falls back safely
- [ ] Keyboard: focus ring visible, rows reachable, Enter/Space activates

**Data honesty**
- [ ] No invented metrics anywhere; unmeasured values show `n/a` with a reason
- [ ] Packet loss labeled as unmeasured rather than 0%
- [ ] Latency reads `n/a` until a test actually succeeds

**Content**
- [ ] Each page matches its wireframe above
- [ ] Loading, empty, disconnected, permission, invalid-input, paused, and
      export outcomes all implemented
- [ ] Long hostnames/IPv6/errors wrap without truncation

**Visuals**
- [ ] Light, dark, and system themes all readable
- [ ] Status never conveyed by color alone
- [ ] 1024×768 / 1280×800 / 1440×900 verified — no overlap or clipping

**Behavior**
- [ ] Every visible control performs a real action
- [ ] Long tests stay async; UI responsive throughout
- [ ] Destructive actions confirm; exports report success/failure

**Defect log (honest status)**

| # | Severity | Status | Item |
|---|---|---|---|
| 1 | Low | Fixed | Sidebar is a plain `GtkBox` + qdata state rather than a GObject subclass; avoids vfunc complexity for a size animation |
| 2 | Low | Fixed | Animation falls back to an immediate transition under reduced motion or before map — deliberately stable, not partial |
| 3 | Info | Open | Traffic CSV timestamps are monotonic offsets; wall-clock join pending |
| 4 | Info | Open | No packaged application icon; symbolic fallback in use |
| 5 | Low | Open | `GtkFileDialog`/`GtkAlertDialog` used on GTK ≥ 4.10; GTK 4.6–4.9 keeps the deprecated chooser/message dialogs |