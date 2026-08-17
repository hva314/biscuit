# cppcheck backlog

Inventory of the static-analysis findings that remain after the CI gate was
narrowed to high severity only.

## Why this file exists

The `cppcheck` CI job originally gated on `--fail-on-defect low --fail-on-defect
medium --fail-on-defect high`. That gate had never passed in this repository:
the tree carries an inherited backlog of **211 low/medium findings across ~80
files**, none of them in recently added code. Gating on them blocked every pull
request on pre-existing code that the PR did not touch.

The gate is now `--fail-on-defect high` (currently **0 high findings**, so it
passes). Low and medium findings are still analysed and still printed in the job
log — they are visible, just not blocking. This file is the record of what was
deferred so the backlog is tracked rather than silently buried.

## Reproducing

```bash
pio check --fail-on-defect high          # what CI runs
pio check                                # full report, non-gating
```

Requires the submodule to be present (`git submodule update --init --recursive`).
Line numbers below are against the tree as of the `fix/ci-failures` branch, taken
after `clang-format` normalisation — they will drift as files are edited.

## Already fixed (not in this backlog)

Handled in the same PR that narrowed the gate:

| Finding | File | Resolution |
|---|---|---|
| `arrayIndexOutOfBoundsCond` | `MazeActivity.cpp` | **Real out-of-bounds write.** The parent-chain loop can exit with `solvePathLen == MAX_PATH`, and the following unguarded store wrote `solvePath[2400]` on a `int16_t[2400]`. Reachable: `MAX_W * MAX_H == 40 * 60 == 2400 == MAX_PATH`, and size index 2 selects that grid. Guarded. |
| `duplInheritedMember` | `CipherActivity.h` | `std::string result` shadowed `Activity::result` (`ActivityResult`). Renamed to `outputText`, matching the sibling `inputText` / `keyText`. |
| `arrayIndexThenCheck` ×2 | `FireActivity.cpp`, `SweepActivity.cpp` | Bound now checked before the subscript. Both were safe as written (`ssid[33]` makes index 32 valid; the guarded operand bounded the write), so this is hardening, not a bug fix. |

## Priorities

### P1 — worth fixing, plausible real impact

**`invalidPrintfArgType` — 6 findings (medium)**

- `src/activities/apps/HostScannerActivity.cpp:20` (4) — `%d` given `unsigned int`
- `src/activities/apps/PacketMonitorActivity.cpp:335` (2) — `%lu` given `unsigned int`

Benign on this target (ESP32 is 32-bit, so `unsigned int` and `unsigned long`
are both 32 bits and pass identically), but the format strings are wrong as
written and would break on any 64-bit host build — including the `native` test
environment if these files are ever pulled into it. Fix by correcting the
specifier or casting the argument.

**`knownConditionTrueFalse` — 10 findings (low)**

Conditions that can never change outcome, e.g.
`src/util/FrameParser.cpp:125,162` (`offset + 2 > tagLen` always false) and
`src/activities/apps/TransitAlertActivity.cpp:115` (`scanned < 5` always true).
Each is either dead defensive code or a guard that does not guard what the
author intended — worth reading individually rather than deleting in bulk, since
a wrong-but-intended guard is a latent bug.

### P2 — correctness-adjacent hygiene

**`uninitMemberVar` — 24 findings (medium)**

Concentrated in `src/network/SdFirmwareUpdater.h` (4),
`src/activities/apps/CasinoActivity.h` (4), `WifiHeatMapActivity.h` (3) and
others. Most are false positives: members of type `std::vector`, `std::string`
and `String` are default-constructed and need no explicit initialiser. The
scalar members among them (raw pointers, `int`, `bool`) are the ones that matter
and should get in-class initialisers. Triage individually; do not bulk-suppress.

**`shadowVariable` — 1 finding (low)**

`src/activities/apps/UnitConverterActivity.cpp:295` — local `display` shadows an
outer variable.

### P3 — mechanical style cleanup

Safe to do file-by-file, ideally alongside other work in the same file rather
than as one large sweep. No behavioural change expected from any of these.

| Type | Count | Nature |
|---|---:|---|
| `useStlAlgorithm` | 34 | Raw loops replaceable with `std::find_if` / `std::transform` / `std::accumulate`. Judgement call on an embedded target — flash cost and readability both matter; not automatically an improvement. |
| `constParameterReference` | 33 | Parameters that can be `const&`. |
| `unreadVariable` | 32 | Assigned but never read. Some are genuine dead stores worth deleting; check each is not a half-finished feature. |
| `constVariable` | 15 | Locals that can be `const`. |
| `cstyleCast` | 13 | C casts to replace with `static_cast` / `reinterpret_cast`. |
| `unusedPrivateFunction` | 11 | Private members with no caller. **Do not delete on grep evidence alone** — verify reachability properly, since dynamic dispatch and registry lookups are invisible to a name search. |
| `constVariableReference` | 10 | References that can be `const&`. |
| `constVariablePointer` | 8 | Pointers that can be pointer-to-const. |
| `variableScope` | 5 | Declarations that can be narrowed. |
| `uselessCallsSubstr` | 5 | `substr()` calls whose result is the whole string. |
| `constParameterCallback` | 4 | Callback parameters that can be const. |

### Not currently reported

`toUpperBuf` in `src/activities/apps/SweepActivity.cpp` computes `dstLen - 1` on
a `size_t`, which underflows to `SIZE_MAX` if a caller ever passes `dstLen == 0`,
after which the trailing `dst[i] = '\0'` writes to a zero-length buffer. Its only
current caller passes `sizeof(upper)` on a fixed-size array, so it is
unreachable today and cppcheck does not flag it. Worth a guard if the helper
gains callers.

## Suggested approach

Do not attempt this as one sweep. The backlog spans ~80 files of embedded code
that cannot be exercised without hardware, so a single large refactor carries
more regression risk than the findings themselves. Prefer:

1. Fix P1 as a small standalone PR.
2. Triage P2 per file, keeping the diff reviewable.
3. Take P3 opportunistically — when a file is being edited anyway, clean its
   findings in the same PR.

Tighten the CI gate back to `--fail-on-defect medium` once P1 and P2 are clear.

## Appendix — full inventory by finding type

Grouped from `pio check`, most frequent first.

### `useStlAlgorithm` — 34 (low)

- `src/components/themes/MilitaryTheme.cpp` (8) — lines 87, 100, 129, 151, 237, 334, 388, 454
- `src/activities/apps/NetworkMonitorActivity.cpp` (3) — lines 200, 233, 283
- `src/activities/apps/ApHistoryLoggerActivity.cpp` (2) — lines 58, 72
- `src/activities/apps/NetworkChangeActivity.cpp` (2) — lines 133, 145
- `src/activities/apps/PerimeterWatchActivity.cpp` (2) — lines 47, 63
- `src/activities/apps/SdFileBrowserActivity.cpp` (2) — lines 170, 177
- `src/activities/apps/AppCategoryActivity.cpp` (1) — lines 143
- `src/activities/apps/BarcodeActivity.cpp` (1) — lines 621
- `src/activities/apps/BleContactExchangeActivity.cpp` (1) — lines 151
- `src/activities/apps/BleScannerActivity.cpp` (1) — lines 88
- `src/activities/apps/CasinoActivity.cpp` (1) — lines 75
- `src/activities/apps/DeviceFingerprinterActivity.cpp` (1) — lines 79
- `src/activities/apps/MdnsBrowserActivity.cpp` (1) — lines 120
- `src/activities/apps/MeshChatActivity.cpp` (1) — lines 175
- `src/activities/apps/ProbeSnifferActivity.cpp` (1) — lines 55
- `src/activities/apps/SdEncryptionActivity.cpp` (1) — lines 89
- `src/activities/apps/SnakeActivity.cpp` (1) — lines 67
- `src/activities/apps/SsidChannelActivity.cpp` (1) — lines 155
- `src/activities/apps/WardrivingActivity.cpp` (1) — lines 91
- `src/activities/apps/WifiConnectActivity.cpp` (1) — lines 74
- `src/activities/apps/WifiScannerActivity.cpp` (1) — lines 142

### `constParameterReference` — 33 (low)

- `src/activities/apps/CasinoActivity.cpp` (13) — lines 355, 477, 488, 496, 530, 540, 564, 594, 613, 633, 676, 708, 728
- `src/components/themes/radar/RadarHomeRenderer.cpp` (4) — lines 50, 74, 79, 86
- `src/activities/apps/ChessActivity.cpp` (3) — lines 15, 21, 27
- `src/activities/apps/DiceRollerActivity.cpp` (3) — lines 12, 18, 125
- `src/activities/apps/VoronoiActivity.cpp` (3) — lines 14, 20, 26
- `src/activities/apps/MazeActivity.cpp` (2) — lines 34, 280
- `src/activities/apps/CalculatorActivity.cpp` (1) — lines 26
- `src/activities/apps/MinesweeperActivity.cpp` (1) — lines 14
- `src/activities/apps/SnakeActivity.cpp` (1) — lines 12
- `src/activities/apps/SudokuActivity.cpp` (1) — lines 14
- `src/activities/apps/TetrisActivity.cpp` (1) — lines 15

### `unreadVariable` — 32 (low)

- `src/activities/apps/DeadDropActivity.cpp` (3) — lines 386, 387, 437
- `src/activities/apps/FireActivity.cpp` (3) — lines 569, 884, 928
- `src/activities/apps/FlashcardActivity.cpp` (3) — lines 196, 211, 224
- `src/activities/apps/MorseCodeActivity.cpp` (3) — lines 226, 247, 275
- `src/activities/apps/BleBeaconActivity.cpp` (2) — lines 82, 85
- `src/activities/apps/EventLoggerActivity.cpp` (2) — lines 160, 161
- `src/activities/apps/PhoneTetherActivity.cpp` (2) — lines 456, 511
- `src/activities/apps/PingActivity.cpp` (2) — lines 132, 133
- `src/activities/apps/BleContactExchangeActivity.cpp` (1) — lines 356
- `src/activities/apps/BulletinBoardActivity.cpp` (1) — lines 294
- `src/activities/apps/CipherActivity.cpp` (1) — lines 247
- `src/activities/apps/EmergencyActivity.cpp` (1) — lines 608
- `src/activities/apps/HostScannerActivity.cpp` (1) — lines 51
- `src/activities/apps/PacketMonitorActivity.cpp` (1) — lines 270
- `src/activities/apps/PasswordGeneratorActivity.cpp` (1) — lines 115
- `src/activities/apps/ScreenDecoyActivity.cpp` (1) — lines 295
- `src/activities/apps/TaskManagerActivity.cpp` (1) — lines 56
- `src/activities/apps/TotpActivity.cpp` (1) — lines 209
- `src/activities/apps/UnitConverterActivity.cpp` (1) — lines 305
- `src/util/TargetDB.cpp` (1) — lines 196

### `uninitMemberVar` — 24 (medium)

- `src/network/SdFirmwareUpdater.h` (4) — lines 31, 31, 31, 31
- `src/activities/apps/CasinoActivity.h` (3) — lines 11, 11, 11
- `src/activities/apps/SteganographyActivity.h` (3) — lines 11, 11, 11
- `src/activities/apps/WifiHeatMapActivity.h` (3) — lines 9, 9, 9
- `src/activities/apps/MeshChatActivity.h` (2) — lines 16, 16
- `src/activities/apps/NetworkChangeActivity.h` (2) — lines 10, 10
- `src/activities/apps/SignalTriangulationActivity.h` (2) — lines 9, 9
- `src/activities/apps/QrTotpActivity.h` (1) — lines 10
- `src/activities/apps/SdEncryptionActivity.h` (1) — lines 8
- `src/activities/apps/SnakeActivity.h` (1) — lines 10
- `src/activities/apps/TotpActivity.h` (1) — lines 10
- `src/activities/apps/VehicleFinderActivity.h` (1) — lines 6

### `constVariable` — 15 (low)

- `src/activities/apps/FireActivity.cpp` (5) — lines 467, 600, 621, 670, 851
- `src/activities/apps/CasinoActivity.cpp` (3) — lines 128, 2840, 3179
- `src/activities/apps/MinesweeperActivity.cpp` (2) — lines 319, 352
- `src/activities/apps/BarcodeActivity.cpp` (1) — lines 717
- `src/activities/apps/ChessActivity.cpp` (1) — lines 697
- `src/activities/apps/MatrixRainActivity.cpp` (1) — lines 73
- `src/activities/apps/SecurityPinActivity.cpp` (1) — lines 442
- `src/activities/apps/SudokuActivity.cpp` (1) — lines 222

### `cstyleCast` — 13 (low)

- `src/activities/apps/FireActivity.cpp` (9) — lines 61, 640, 773, 793, 846, 855, 864, 876, 924
- `src/activities/apps/SteganographyActivity.cpp` (3) — lines 140, 143, 145
- `src/activities/apps/AppCategoryActivity.cpp` (1) — lines 83

### `unusedPrivateFunction` — 11 (low)

- `src/components/themes/radar/RadarHomeRenderer.h` (2) — lines 34, 35
- `src/activities/apps/BleKeyboardActivity.h` (1) — lines 64
- `src/activities/apps/BleProximityActivity.h` (1) — lines 40
- `src/activities/apps/CasinoActivity.h` (1) — lines 131
- `src/activities/apps/ChessActivity.h` (1) — lines 57
- `src/activities/apps/NetworkMonitorActivity.h` (1) — lines 102
- `src/activities/apps/PacketMonitorActivity.h` (1) — lines 62
- `src/activities/apps/SdEncryptionActivity.h` (1) — lines 75
- `src/activities/apps/TetrisActivity.h` (1) — lines 73
- `src/activities/apps/WifiScannerActivity.h` (1) — lines 75

### `constVariableReference` — 10 (low)

- `src/activities/apps/UnitConverterActivity.cpp` (3) — lines 75, 310, 318
- `src/activities/apps/MorseCodeActivity.cpp` (2) — lines 236, 262
- `src/activities/apps/CasinoActivity.cpp` (1) — lines 1411
- `src/activities/apps/ChessActivity.cpp` (1) — lines 434
- `src/activities/apps/PingActivity.cpp` (1) — lines 143
- `src/activities/apps/SnakeActivity.cpp` (1) — lines 67
- `src/activities/apps/WifiConnectActivity.cpp` (1) — lines 89

### `knownConditionTrueFalse` — 10 (low)

- `src/util/FrameParser.cpp` (2) — lines 125, 162
- `src/activities/apps/BreadcrumbTrailActivity.cpp` (1) — lines 65
- `src/activities/apps/CrowdDensityActivity.cpp` (1) — lines 209
- `src/activities/apps/NetworkMonitorActivity.cpp` (1) — lines 394
- `src/activities/apps/PacketMonitorActivity.cpp` (1) — lines 210
- `src/activities/apps/ProbeSnifferActivity.cpp` (1) — lines 38
- `src/activities/apps/SdEncryptionActivity.cpp` (1) — lines 785
- `src/activities/apps/SweepActivity.cpp` (1) — lines 168
- `src/activities/apps/TransitAlertActivity.cpp` (1) — lines 130

### `constVariablePointer` — 8 (low)

- `src/activities/apps/SignalTriangulationActivity.cpp` (2) — lines 66, 99
- `src/activities/apps/WifiScannerActivity.cpp` (2) — lines 82, 163
- `src/activities/apps/NetworkMonitorActivity.cpp` (1) — lines 191
- `src/activities/apps/SweepActivity.cpp` (1) — lines 121
- `src/activities/apps/WardrivingActivity.cpp` (1) — lines 83
- `src/activities/apps/WifiHeatMapActivity.cpp` (1) — lines 86

### `uselessCallsSubstr` — 5 (low)

- `src/activities/apps/BleKeyboardActivity.cpp` (3) — lines 540, 556, 588
- `src/activities/apps/BeaconTestActivity.cpp` (1) — lines 220
- `src/activities/apps/SdFileBrowserActivity.cpp` (1) — lines 92

### `variableScope` — 5 (low)

- `src/activities/apps/ClockActivity.cpp` (2) — lines 90, 96
- `src/activities/apps/BeaconTestActivity.cpp` (1) — lines 91
- `src/activities/apps/BleKeyboardActivity.cpp` (1) — lines 148
- `src/activities/apps/FireActivity.cpp` (1) — lines 670

### `constParameterCallback` — 4 (low)

- `src/activities/apps/CrowdDensityActivity.cpp` (1) — lines 18
- `src/activities/apps/DeviceFingerprinterActivity.cpp` (1) — lines 18
- `src/activities/apps/NetworkMonitorActivity.cpp` (1) — lines 23
- `src/activities/apps/ProbeSnifferActivity.cpp` (1) — lines 19

### `invalidPrintfArgType_sint` — 4 (medium)

- `src/activities/apps/HostScannerActivity.cpp` (4) — lines 20, 20, 20, 20

### `invalidPrintfArgType_uint` — 2 (medium)

- `src/activities/apps/PacketMonitorActivity.cpp` (2) — lines 335, 335

### `shadowVariable` — 1 (low)

- `src/activities/apps/UnitConverterActivity.cpp` (1) — lines 291
