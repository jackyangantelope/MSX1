# Third-party provenance and distribution scope

The combined source is distributed under GPL-3.0-only. Individual files keep
their original licenses; this statement does not relicense them. The Z80/Zeta
LGPL-3.0-or-later sources and full `COPYING`/`COPYING.LESSER` texts are retained.
This repository publishes source, not a prelinked firmware binary.

| Component | Public revision and license | Included code and changes |
|---|---|---|
| BareMSX | [joskwanten/BareMSX](https://github.com/joskwanten/BareMSX) `66df161d0356bffd9f73b366e663437f2074b621`, MIT (`deps/BareMSX/LICENSE`) | Selected `mapper.c/h`, `tms9918.h` and host `pico.h` compatibility header. `runtime/msx1_vdp.c` is a local derivative of `emu/tms9918.c`: buffered VRAM reads, wrap/clear semantics, SCREEN 3, indexed scanline output and collision masks. |
| Z80 | [redcode/Z80](https://github.com/redcode/Z80) `dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9`, LGPL-3.0-or-later (`deps/Z80/COPYING.LESSER`) | `Z80.c` and API header are unmodified; CMake relocates code/table sections to SRAM after compilation. |
| Zeta | [redcode/Zeta](https://github.com/redcode/Zeta) `42830b0e735cc3a83c7630e2dcf21949e8441c6d`, LGPL-3.0-or-later (`deps/Zeta/COPYING.LESSER`) | API headers required by Z80. |
| AY PSG | [emu2149](https://github.com/digital-sound-antiques/emu2149) `441f3dc295a9b82db16921461f0678b483db8304`, MIT (`deps/emu2149/LICENSE`) | `runtime/msx1_psg.c` is a locally adapted copy of the upstream chip implementation with SRAM placement; the original source is retained for comparison. |
| SCC | [emu2212](https://github.com/digital-sound-antiques/emu2212) `e1735c83707e6ee739a123476344659f7fa9fb18`, MIT (`deps/emu2212/LICENSE`) | Chip source is unchanged; machine glue handles cold reset and audio timing. |
| Shared platform | [fhoedemakers/pico_shared](https://github.com/fhoedemakers/pico_shared) `df683bf00ff95e1ceda88f132bbf72f8cc7cadb0`, GPL-3.0-only root license | Selected HDMI, FatFs, TinyUSB configuration and support files. Local HDMI queue/audio and double-buffer changes are documented in `provenance/pico_shared-changes.diff`. FatFs and SD wrapper retain their own notices. |
| Font | [dhepper/font8x8](https://github.com/dhepper/font8x8), source SHA-256 `49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5` | Public-domain ASCII 32..126 extraction; attribution is retained in `platform/text/font_8x8.h`. |
| New machine/frontend | This project, BSD-3-Clause file notices | `runtime/msx1_runtime.c` provides slot/PPI/keyboard/controller mapping, cartridge loading and audio scheduling. `firmware/msx1.c` provides menu, SD, HDMI and input integration for Fruit Jam. |

No game ROM, BIOS, save, firmware binary, private board configuration or SDK
checkout is shipped. Pico SDK, TinyUSB and Pico-PIO-USB are pinned in
`dependencies.lock.json` and fetched separately. `dependencies.upstream.json`
records the public component revisions. The physical bench validation does
not establish public Fruit Jam behavior.
