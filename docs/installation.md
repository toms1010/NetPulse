# NetPulse Installation & Build

## Prerequisites (Ubuntu / Kubuntu 24.04+)

```sh
sudo apt-get update
sudo apt-get install -y \
  build-essential gcc cmake ninja-build pkg-config \
  libgtk-4-dev libglib2.0-dev libcairo2-dev \
  libadwaita-1-dev iputils-ping git
```

`libadwaita` is optional (theme integration only).
`iputils-ping` is needed for the optional ICMP test.

## Configure & build

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

Release:

```sh
cmake --preset release
cmake --build --preset release
```

Run:

```sh
./build/debug/netpulse        # GUI (needs a display / Wayland / X11)
```

### Theming

`System` (the default) follows the desktop. `Light` and `Dark` set
`gtk-application-prefer-dark-theme`, which is GTK 4's supported switch and
drives every colour in `resources/styles/main.css` (the sheet uses GTK's
theme-aware named colours only — there are no separate dark/light sheets,
because layering a second sheet that re-declares a colour name invalidates the
references the first sheet already resolved).

On **KDE Plasma with a dark global theme**, the bundled Qt platform-theme
plugin supplies its own palette and keeps the window dark even when `Light` is
selected. That is a desktop-shell override, not an app bug; run with
`GTK_THEME=Adwaita ./build/debug/netpulse` to confirm GTK's own light palette
renders correctly.

### Opening a specific page

```sh
NETPULSE_PAGE=1 ./build/debug/netpulse   # start on Network Interfaces
```

`NETPULSE_PAGE` takes the page index (0 Overview, 1 Interfaces,
2 Connectivity, 3 Traffic, 4 Logs, 5 Settings, 6 About). It exists for
screenshots and manual QA.

> If `libgtk-4-dev` is missing, CMake still configures and builds
> `netpulse_core` + all unit tests, but skips the `netpulse` GUI target
> with a clear warning. Install the dev package and re-run cmake.

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
# or per-binary verbose TAP output:
./build/debug/tests/test_traffic_monitor -v
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Package 'gtk4' not found` | `sudo apt-get install libgtk-4-dev`, re-run cmake |
| `Wayland: cannot open display` | run under a desktop session, or `xvfb-run ./build/debug/netpulse` for smoke tests |
| ping test says "not permitted" | `ping` needs `cap_net_raw`; on most desktops it already has it. Otherwise use DNS/TCP tests (no privileges needed) |
| `ninja: build stopped` after editing CMakeLists | `cmake -S . -B build/debug` again to regenerate |
| CSS not applied | the stylesheet is compiled into the binary via GResource, so the working directory no longer matters; if it is missing you will see `No stylesheet found` on stderr |
| Light/Dark ignored on KDE Plasma | the Qt platform-theme plugin overrides GTK's palette; see **Theming** above |
| No interfaces listed | check `/proc/net/dev` exists (needs Linux); containers may only show `lo`/`eth0` |
