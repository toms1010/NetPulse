# NetPulse User Guide

## Layout
- **Sidebar (left, 230 px expanded / 68 px collapsed):** Overview, Network
  Interfaces, Connectivity Tests, Traffic Monitor, Diagnostics & Logs,
  Settings, About NetPulse.
- **Collapsible sidebar:** the `[=]` button in the toolbar (or its tooltip,
  "Collapse/Expand navigation sidebar") toggles it. Collapsed shows icons only
  with tooltips and keeps the logo and active-page indicator. The width
  animates over ~180 ms unless your desktop requests reduced motion, in which
  case it switches instantly. The choice is remembered between launches.
  Collapsing never changes the page you are on or interrupts monitoring.
- **Top toolbar:** page title, monitoring status pill (Connected /
  Disconnected / Paused / Unknown), Refresh, Pause/Resume, theme selector,
  settings shortcut.
- **Content:** 24 px padding, 16 px card gaps, wraps at 1024 px width.

## Pages

### Overview
Six cards (connectivity, active interface, download, upload, latency,
packet-loss `n/a`), a live traffic chart, interface summary, recent events,
and last-check timestamp. Nothing here is simulated.

### Network Interfaces
Left list (name + up/down pill) drives the right details panel (type, state,
IPv4/IPv6, MAC, byte counters, live rates, packet counts). Disappearing
interfaces are pruned automatically.

### Connectivity Tests
Enter target, pick DNS / TCP / ICMP ping, set port (TCP) and timeout, press
**Start Test**. Results prepend to history with timestamp, outcome, and
latency or the exact error. Invalid input is rejected with a message, not a
fake failure.

### Traffic Monitor
Interface drop-down, sampling interval, big current-rate cards, Cairo
time-series chart (green = download, blue = upload), peak stats, Pause and
Clear. Pausing freezes sampling and labels the chart PAUSED.

### Diagnostics & Logs
Search box, severity drop-down, category filter, scrollable rows
(timestamp, severity, category, message), **Export CSV**, and **Clear Logs**
(with confirmation dialog).

### Settings
Monitoring/sampling intervals, chart history, target host/port/timeout,
appearance, latency warning, warnings toggle, CSV export, restore defaults.
Invalid values are rejected; saved settings persist in
`~/.config/netpulse/netpulse.conf`.

## Keyboard & accessibility
- `Tab`/`Shift+Tab` moves focus; `Enter`/`Space` activates.
- All icon-only buttons have tooltips; status always pairs color with text.
- Charts have text legends and numeric labels; contrast meets both themes.
