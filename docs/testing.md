# NetPulse Testing

## Automated (GLib g_test, `ctest`)

| Binary | Covers |
|---|---|
| `test_formatters` | byte/rate/latency/timestamp formatting |
| `test_traffic_monitor` | rate math, zero-dt, counter reset, 32-bit wraparound, first-sample zero, rolling window, pause, clear, peak |
| `test_connectivity` | hostname/port/timeout validation, shell-metachar rejection, invalid-host graceful failure, type strings |
| `test_network` | real `getifaddrs` discovery (asserts `lo`), missing-iface error, type classifier |
| `test_settings_export` | defaults validate, bad values rejected, CSV escaping, logs/interfaces CSV headers + content |
| `test_sidebar_state` | sidebar defaults to expanded, save/load round-trip, missing config → safe defaults, corrupt key → expanded |
| `test_iface_model` | row text/rate/byte formatting, sort keys, search filter (needs GTK for `GtkSorter`/`GtkFilter`) |

Run: `ctest --test-dir build/debug --output-on-failure` — all 7 must pass.

## Manual checklist

- [ ] Wi-Fi off: status pill shows Disconnected; overview cards keep last
      values with honest subtitles; no fake zeros presented as live.
- [ ] Wi-Fi back on: pill returns to Connected within one sample interval.
- [ ] USB tether / interface plug/unplug: interfaces page updates, pruned
      entries vanish, selection falls back sanely.
- [ ] DNS failure (e.g. target `nonexistent.invalid`): DNS test FAILs with
      resolver message; log records WARNING.
- [ ] Unreachable TCP (e.g. `10.255.255.1:9`, timeout 1000 ms): FAILs after
      ~1 s; UI stays responsive during the wait.
- [ ] Missing `ping`: ping test reports "Cannot run 'ping'" instead of hanging.
- [ ] Resize to 1024×768, 1280×800, 1440×900: no clipped text or overlap;
      sidebar stays ~230 px; cards wrap.
- [ ] Sidebar toggle: expands/collapses, icon + tooltip match state, content
      reflows with no clipping.
- [ ] Collapsed sidebar: icons only, logo visible, tooltips appear, active row
      still highlighted, selected page unchanged.
- [ ] Sidebar preference persists: collapse, quit, relaunch → still collapsed.
- [ ] Toggle during active monitoring: chart keeps updating, no interruption.
- [ ] Dark / Light / System themes readable; pills distinguishable by text,
      not color alone.
- [ ] Export CSV (interfaces, logs): file has headers + units; toast confirms;
      unwritable path shows failure toast.
- [ ] Clear logs asks for confirmation; cancel preserves entries.
- [ ] Invalid settings (e.g. interval 10 ms, host `a;b`) rejected with message.
- [ ] Pause: sampling stops, pill shows Paused, charts label PAUSED, resume
      continues without a rate spike.
