# MSX1 for RP2350

Independent public-source candidate for Adafruit Fruit Jam. The program runs
MSX1 cartridge files with an external C-BIOS image; no BIOS or game data is
included in this repository.

## Validation boundary

On 2026-10-28 the user accepted a scoped RP2350B bench run of *The Menace
from Triton* v1.2: 8,640 emulated and submitted frames over 144.177451 s
(59.926153 FPS), with no recorded audio underflow, overflow, resync, slow
frame or replaced frame. The user confirmed the original game audio sounded
normal. This does not establish compatibility with other games, complete
controls or long-run stability. The public Fruit Jam configuration uses a
different frontend and has **not** been tested on physical hardware.

The public source builds against pinned dependencies. A host regression with
the externally supplied Triton cartridge and C-BIOS ran 3,600 frames each
in idle, keyboard and controller modes under ASan/UBSan; all three reload
checks passed. See [VALIDATION.md](VALIDATION.md).

## Source and licenses

The machine glue and public-board integration are new. The VDP and mapper
derive from [BareMSX](https://github.com/joskwanten/BareMSX) at
`66df161d0356bffd9f73b366e663437f2074b621`. The CPU uses
[redcode/Z80](https://github.com/redcode/Z80) at
`dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9` with
[Zeta](https://github.com/redcode/Zeta) headers at
`42830b0e735cc3a83c7630e2dcf21949e8441c6d`.
AY and SCC use [emu2149](https://github.com/digital-sound-antiques/emu2149)
at `441f3dc295a9b82db16921461f0678b483db8304` and
[emu2212](https://github.com/digital-sound-antiques/emu2212) at
`e1735c83707e6ee739a123476344659f7fa9fb18`.
The selected video/SD/USB platform comes from
[pico_shared](https://github.com/fhoedemakers/pico_shared) at
`df683bf00ff95e1ceda88f132bbf72f8cc7cadb0`, with local changes.
Original notices and license files remain in `deps/`; see
[THIRD_PARTY.md](THIRD_PARTY.md) for modifications and distribution terms.

## Build

The target is `adafruit_fruit_jam` (`HW_CONFIG=8`). Install CMake 3.20+,
Ninja, Python 3, Git and Arm GNU Toolchain 14.2.Rel1. `dependencies.lock.json`
pins Pico SDK 2.3.0, its TinyUSB gitlink, and Pico-PIO-USB. Fetch them with
`scripts/fetch-dependencies.ps1 -Destination <outside-source-directory>` or
use matching clean checkouts, then run:

```powershell
./scripts/build.ps1 -PicoSdkPath <pico-sdk> -PicoPioUsbPath <Pico-PIO-USB> `
  -ToolchainPath <arm-toolchain> -NinjaPath <ninja.exe>
```

The SDK can build or fetch pioasm and picotool. For an offline build, pass
`-PioasmDir`, `-PicotoolDir` and optionally `-PythonExe`.
The output is `build/fruit-jam/picoMSX1.uf2`, linked at `0x10080000` for a
compatible application loader, not for standalone boot at flash offset zero.
Fruit Jam uses SD SPI pins 34/35/36/39, USB D+/D- 1/2 with power GPIO11,
and SDK-default UART1 pins 8/9; this wiring has not been validated with the
MSX1 program on hardware.

Place a legally obtained `cbios_main_msx1.rom` and `.rom` cartridge files on
the SD card under `/roms/MSX1`. C-BIOS is available from
[C-BIOS](https://cbios.sourceforge.net/); the test game came from the
[Triton v1.2 release](https://github.com/santiontanon/triton/releases/tag/v1.2).
Neither file is part of the source release. To repeat the host check on Linux
or WSL, run `bash scripts/test-host.sh <directory-containing-these-two-files>`.
Run `python scripts/audit-source.py` to check source selection and notices.

The public firmware remains experimental until Fruit Jam gameplay, original
audio and input are tested physically.
