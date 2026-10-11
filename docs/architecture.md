# NetPulse Architecture

## Overview
NetPulse is a native Linux desktop app (C17 + GTK 4). Monitoring logic lives
in GLib/GIO-only modules with **no GTK dependency**; GTK pages are thin views
that poll shared state on the main loop.

```
                 +----------------------+
                 | GtkApplication (app) |
                 | settings + diag log  |
                 +----------+-----------+
                            | owns NpWindowState
                 +----------v-----------+
                 | main_window: tick()  |  g_timeout_add(sample_interval)
                 +--+-----+-----+---+---+
                    |     |     |   |
        interfaces  |     |     |   +-- logs/settings/about (views)
        overview----+     |     +------ traffic (NpTrafficMonitor + chart)
                    +---------- connectivity (worker threads + idle cb)
```

## Modules

| Area | Files | Depends on |
|---|---|---|
| Interface discovery | `include/netpulse/network.h`, `src/core/network.c` | `getifaddrs()`, `/proc/net/dev`, `/sys/class/net/*/operstate`, `/proc/net/route` |
| Rate sampling | `interface_monitor.h/.c` | pure counters + monotonic clock; handles reset/wrap |
| Rolling traffic | `traffic_monitor.h/.c` | GArray window, pause/clear/peak |
| Connectivity | `connectivity.h/.c` | GResolver (DNS), GSocketClient (TCP), `ping` child via `g_spawn_sync` (no shell) |
| Log store | `diagnostics.h/.c` | GMutex + GPtrArray, query/filter |
| Settings | `settings.h`, `src/services/settings.c` | GKeyFile at `~/.config/netpulse/netpulse.conf` |
| CSV export | `exporter.h/.c` | stdio, `np_csv_escape` |
| Formatting | `formatters.h/.c` | 1024-based B/KB/MB/GB |
| Errors | `error_utils.h/.c` | `NP_ERROR` GQuark domain |
| App/GUI | `app.h`, `app.c`, `main.c`, `src/ui/*` | GTK 4, Cairo charts, CSS |

## Threading rules
- Slow work (DNS/TCP/ping) runs on `GThread` via
  `np_connectivity_check_async()`; results return via `g_idle_add` so GTK
  widgets are only touched on the main thread.
- Interface scans and counter math run on the main-loop timeout (fast,
  non-blocking syscalls only).
- `NpDiagnostics` is mutex-protected so worker threads can log safely.

## Data honesty
- Every displayed rate/latency originates from a real syscall, counter delta,
  or measured timestamp.
- Packet loss is shown as `n/a` because none of the current test types measure
  loss; the UI labels this explicitly.
- Empty/disconnected/error states never render fabricated charts.

## Persistence
- Settings: INI-style GKeyFile, validated on load and save.
- Exports: CSV with headers + units, written on user action only.
- No packet payloads, passwords, or personal data are stored.
