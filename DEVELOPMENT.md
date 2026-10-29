# Change record

## 2026-10-29

- Created an independent source-only MSX1 candidate from the accepted 0.1.6
  machine/VDP/PSG selection. Kept the frozen bench firmware and test content
  unchanged outside this repository.
- Replaced the private-board frontend with an Adafruit Fruit Jam target and
  a public selected platform source tree. Original ROM and C-BIOS are external.
- Retained the MIT, LGPL and GPL component notices and pinned public build
  dependencies. The repository separates prior bench acceptance from the new
  public target's still-pending physical validation.
- A local Fruit Jam Release build and three-mode Triton ASan/UBSan host run
  passed. Static RAM is high; compile success is not a heap/stack measurement.
