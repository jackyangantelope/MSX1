# Validation, 2026-10-29

| Evidence | Result | Limit |
|---|---|---|
| Prior RP2350B bench run | Scoped pass, user-confirmed 2026-10-28 | Triton v1.2 only; 8,640 produced/submitted frames in 144.177451 s, 59.926153 FPS. Last 60.077655 s: 59.922445 FPS. Under/over/resync/slow and replaced/failed/pending stayed zero; user confirmed original game audio sounded normal. Other games, complete controls and long stability unverified. |
| Public source host regression | Passed | Triton with external C-BIOS, idle/keyboard/pad modes, 3,600 frames each under ASan/UBSan; reload checks passed. Not board performance evidence. |
| Public Fruit Jam build | Passed from independent clean clone | Flash 192,408 B and static RAM 495,036 B (94.42%). Runtime heap/stack high-water and physical behavior unmeasured. |
| Public Fruit Jam board | Not tested | Different frontend, pins and shared platform selection from the bench build. |

The original bench UART capture was preserved privately; its SHA-256 is
`ab995c70613a791936da9cee784d257ec1144620a6803967f575f2bca7194567`.
Neither the capture nor the test ROM/BIOS is in this repository. No board
flash, SD card or serial port was changed for this source release.

The independent clone also passed all three host regression modes and reload
checks. Its UF2 SHA-256 matched the initial build:
`efcd63a2d26626aa0b02f23506a3870ad76a1e25bb6fc9ce3fa480e44a2a1fbb`.
Source selection and release blocking audits passed. Only documentation was
updated after that clone verification.
