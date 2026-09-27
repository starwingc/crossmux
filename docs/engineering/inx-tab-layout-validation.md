# Inx tab layout validation

Validated against main `03a9c7ce4daa28fbac81c62b4d9b82d5a792b60e`, with its pinned
FreeInk SDK `34d36ecc37b192f0082cb889a8bb53c887b8d096` and simulator
`dacbbbcdc133a052f9122347429fdcb8cddf80af`.

The touch-only bottom-tab layout reserves a 44 px status bar inside the board's
viewable margins, then a 6 px gap, content, another 6 px gap, and navigation.
All five tabs share this geometry. The clock follows the configured format and
time zone; invalid time displays `--:--`. It updates on page renders, without
adding periodic display refreshes. Battery percentage follows its existing setting.

Bottom navigation keeps its 66 px height: the 1 px separator sits on its top
edge, with the centered 38 × 5 px selected marker covering that segment.
The 38 px icons sit 16 px above the navigation area's bottom edge, leaving a
7 px gap between the selected marker and the icon box. The inset is visual
padding inside the navigation area, additional to the board's safe margins rather than a
replacement for them. Physical-button hints, tab hit regions and their 6 px gaps
are unchanged. Top-tab rendering, content and the time/battery status bar are
unchanged.

## Layout ownership

- `Activity::mainTabLayout()` resolves device margins once and returns the
  complete status, content and navigation rectangles. Drawing and input consume
  those rectangles; there is no retained layout cache or extra allocation.
- `Activity::pageContentRect()` owns the main-tab / regular-header choice for
  all five pages. Individual pages only reserve their own internal spacing.
- App-grid drawing and touch lookup share the same content rectangle and cell
  bounds. Geometry uses the existing `Rect` type from a lightweight header,
  without importing theme implementations or duplicating rectangle types.
- Tab preference defaults are initialized directly from the board's touch
  capability. Existing saved values and the settings schema are unchanged;
  no separate default-value function or host-test stub is needed.

## Automated checks

```sh
cmake -S test -B build/test
cmake --build build/test --target InxNavigationTest TimeUtilsTest -j 4
ctest --test-dir build/test -R 'InxNavigation|TimeUtils|ControlCenterGesture|InxStyleCompatibility' --output-on-failure
cmake --build build/test -j 4
ctest --test-dir build/test --output-on-failure -j 4
python3 -m unittest discover -v -s scripts/tests
./bin/clang-format-fix --check
pio run -e simulator -e simulator_eego_a4 -e simulator_murphy_m4
```

The 30 focused checks cover tab order, drawing/hit bounds and gaps, status-bar
eligibility, content reservations, app-grid hit bounds, and valid/invalid
12/24-hour time formatting. Layout tests include nonzero horizontal/vertical
safe-area origins and theme top padding in both tab positions. The control-center
dispatch harness uses the production layout type and exercises all five pages
at each status-rectangle edge and outside it, including the content gap,
zero-height status bars (top tabs), and non-touch input.

All 579 host tests pass. The Python script suite also passes: 70 tests passed
and one font-regeneration test skipped by its default policy.

## Native simulator checks

Twelve runs produced 63 screenshots using isolated simulated SD cards:

| Device | Scenarios |
| --- | --- |
| A4 | English empty state, Chinese 25-book list, English landscape, top tabs, hidden battery percentage |
| Murphy M4 | Chinese empty state, English 25-book list, Chinese landscape, top tabs, Classic theme |
| X4 (no touch) | Default top tabs and explicitly selected bottom tabs, with button navigation |

Each Inx run visits Recent, Library, Apps, Settings and Statistics. Additional
touch checks open and dismiss the control center from every bottom-tab page,
tap the status/content gap and a tab gap without activation, and open Book 01
and Book 22 (the last fully visible item after scrolling) from the library.
Selecting Top in Settings persists across a process restart.

Pixel checks verified the top-edge separator and 38 × 5 px selected segment,
the 7 px marker-to-icon-box gap, and the 16 px clear bottom inset. After the
layout-ownership refactor, all twelve scenarios were rerun: 56 main-page
screenshots are pixel-identical to the accepted layout outside the live-clock
area. This includes content, battery, tab strips, safe margins and X4 button
hints. The Murphy M4 portrait captures use the requested 480 × 800 resolution;
A4 and landscape runs are additional regressions, not resized M4 previews.

Use `CROSSPOINT_SIM_SD` to select an isolated SD directory. Its
`.crosspoint/settings.json` can start with
`{"uiTheme":5,"language":"EN","onboardingVersion":1,"clockUtcOffsetQ":80}`.
Omitting `inxTabPosition` tests the board default; `0` selects top, `1` bottom.
Use `language: "ZH_CN"` and `clockFormat: 1` for Chinese and a 12-hour clock.

Example A4 bottom-tab navigation, after creating `/tmp/inx-shots`:

```sh
CROSSPOINT_SIM_SD=/tmp/inx-sd \
CROSSPOINT_SIM_INPUT_SCRIPT='2100:TAP:0.3,0.93;3200:TAP:0.5,0.93;4300:TAP:0.7,0.93;5400:TAP:0.9,0.93;6500:TAP:0.3,0.04;7800:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='1600:/tmp/inx-shots/recent.bmp;2700:/tmp/inx-shots/library.bmp;3800:/tmp/inx-shots/apps.bmp;4900:/tmp/inx-shots/settings.bmp;6000:/tmp/inx-shots/stats.bmp;7100:/tmp/inx-shots/control.bmp' \
xvfb-run -a .pio/build/simulator_eego_a4/program
```

Main menus currently run in portrait. The landscape stress checks temporarily
set the live renderer orientation using GDB at `InxRecentActivity::onEnter()`;
they do not add a menu-rotation setting. Use normalized scripted coordinates
because the simulator parses its input schedule before this breakpoint.

```gdb
break InxRecentActivity::onEnter()
commands
silent
call (void) 'GfxRenderer::setOrientation(GfxRenderer::Orientation)'(&renderer, 1)
disable 1
continue
end
run
```

## Captured screens

These are losslessly converted native simulator screenshots of the accepted
layout. They remain valid after the pixel-equivalent refactor; only the live
clock changes between captures. The simulator retains selection highlighting;
hardware touch-focus policy is unchanged.

| Recent (A4) | Library (A4, Chinese) |
| --- | --- |
| ![Recent](images/inx-tabs/a4-recent.png) | ![Library](images/inx-tabs/a4-library.png) |

![A4 landscape applications](images/inx-tabs/a4-landscape-apps.png)

![M4 Chinese settings](images/inx-tabs/m4-settings.png)

![M4 landscape statistics](images/inx-tabs/m4-landscape-stats.png)

[X4 bottom tabs with physical-button hints](images/inx-tabs/x4-bottom.png)

Physical touch-controller behavior, EPD ghosting, refresh timing, and power
consumption still require device validation; the simulator does not model them.

## Hardware build results

PaperMono builds successfully with an isolated PlatformIO core/package
directory: 116,076 bytes static RAM and 5,957,066 bytes Flash reported by
PlatformIO. These are complete-image sizes, not incremental costs of this change.

The default C3 build compiles but fails to link with missing
`ble_base_funcs_reset`, `ble_42_adv_funcs_reset` and related BLE controller symbols
referenced by ESP-IDF's `bt.c`. The same failure occurs in the existing package
cache, a freshly provisioned isolated core, and an unmodified checkout of main
`03a9c7ce`. Therefore the C3 build is **not passing in this environment**; the
baseline comparison establishes that this failure also occurs without the Inx
changes. No BLE/toolchain workaround is included in this UI change.
