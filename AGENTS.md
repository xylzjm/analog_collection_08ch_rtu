# AGENTS.md

8-channel analog-input RTU firmware (4-20 mA, 0-10 V, PT100, thermocouple) for
AT32F421C8T7: Cortex-M4, 120 MHz, 60 KB usable flash, 16 KB SRAM. C99, armclang
(AC6), microlib, `-O0` debug build. EIDE project only - no Makefile, CMake, CI,
lint or typecheck step exists.

## Start with README.md

README.md is the design reference: boot order, clock and interrupt priorities,
pin mux, SIG24131/TMP75/I2C/Modbus board resources, logging chain, flash
partition, QP port and assertion policy. When designed behavior changes, update
the README and keep source comments to short Doxygen `@brief` lines.

Docs are UTF-8 with CRLF line endings; source and headers are pure ASCII. Note
that this Windows shell mangles non-ASCII output, so judge file encoding from
bytes, not from printed text.

## Dependencies live outside this repo

The build reads sibling folders that are neither vendored nor submodules:
`../library` (sig24131, tmp75, analog_input, i2c_soft, modbus_slave) and
`../middleware` (qp, SEGGER RTT, EasyLogger, flashDB/FAL). The multi-root
workspace file adds both folders; without them nothing compiles.

## Build and flash

- No command-line build. Use EIDE: VS Code tasks `build`, `rebuild`, `clean`,
  `flash`, `build and flash`, or the EIDE MCP tools with project uid
  `2b2c107904df4cdb1e97e57d6947db29`.
- Never chip-erase when flashing: the last 4 KB (`0x0800F000..0x0800FFFF`) holds
  the `fdb_kvdb` parameter partition, and ROM is linked to 60 KB (`0xF000`).
- A clean build is 60 C files, ROM about 30.8/60 KB, RAM 4.2/16 KB. Budget
  matters; `-O0` is in use.
- `build/` is gitignored and often stale. Never treat `build/Debug/*.lnp`,
  `*.map` or `.obj` as the current file list - rebuild first.
  `build/Debug/compile_commands.json` is regenerated per build and feeds clangd.

## Adding source files

`.eide/eide.yml` lists every file explicitly (`srcDirs: []`). A new `.c` that is
not added to the target's virtual folder/file list is silently left out of the
build. Register it in the EIDE UI (or the YAML) and rebuild.

Include order in `incList` is load-bearing: `board/ports/easylogger` must stay
before `../middleware/EasyLogger/easylogger/inc`, otherwise the template
`elog_cfg.h` shadows `board/ports/easylogger/elog_cfg.h`.

## Generated and vendor code

- `board/vendor_wk/project/` is AT32 WorkBench output driven by
  `board/vendor_wk/vendor_wk.ATWP`; `.pack/` is the AT32F421 device pack. Keep
  their copyright and generated comments and their MS-style formatting.
- `wk_gpio.c` / `wk_usart.c` already carry hand edits (DE pin, USART setup).
  Regenerating from the `.ATWP` can silently drop them - diff before accepting.
- Vendor `main.c` and `at32f421_int.c` are excluded from the build. Entry point
  is `app/main.c`; IRQ handlers live in board modules (`SysTick_Handler` in
  `board/src/board.c`, `USART1_IRQHandler` / `TMR14_GLOBAL_IRQHandler` in
  `board/src/modbus_slave_board.c`).
- `.eide/eide.yml` writes `board/ports/flashdb` in lower case while the folder is
  `board/ports/flashDB`. Harmless on Windows, breaks on case-sensitive hosts.

## Current state of the firmware

`app/main.c` only runs `log_init()` then `board_init()` and idles. Board ports
for Modbus, SIG24131, analog input, TMP75 and the soft I2C bus are constructed,
but no QP active objects exist, `on_frame_received` is not routed to
`modbus_slave_advance()`, and FAL/KVDB parameter storage is not initialized. Do
not assume the RTU already runs end to end.

## Hard invariants

- `log_init()` must run before `board_init()`, or that function's `elog_error()`
  diagnostics are lost. No logging from interrupt context:
  `elog_port_output_lock` is empty.
- Critical sections only through `board_critical_enter()` /
  `board_critical_exit()` (`board/inc/board.h`). The nest counter is shared with
  QP, which reaches it via the strong `QF_crit_entry_`/`QF_crit_exit_` overrides
  in `board/src/board.c`. Do not add `__disable_irq()`, a second QP port, or a
  local nesting counter; contract violations trap in `Q_onError` with 110/111.
- Assertions use `Q_ASSERT`/`Q_onError`; `Q_onError` writes raw RTT and is safe
  from ISRs. FlashDB and EasyLogger asserts already bridge to it.
- `FDB_WRITE_GRAN` (8) must equal `fal_flash_dev.write_gran` (8); a mismatch
  corrupts the KV layout silently.
- USART1 runs 9600 8N1 (`wk_usart.c` and `modbus_slave_board.c`); changing the
  baud rate means editing both places.
- USART1 and TMR14 share preemption priority 5 and must not preempt each other.
  Modbus RX/TX buffers are 256 B each; TMR14 counts 10 us ticks.

## Verifying changes

1. EIDE build must succeed.
2. Pure-logic code in `../library` has host tests under `<module>/test/*.c` -
   self-contained `main()` plus a `CHECK` macro, excluded from the firmware
   build. Compile with host gcc, for example:

   ```powershell
   gcc -std=c99 -I ../library/protocol/modbus_slave/inc `
     ../library/protocol/modbus_slave/test/test_modbus_slave.c `
     ../library/protocol/modbus_slave/src/*.c -o build/host/test_modbus_slave.exe
   ```

   Run the executable; it prints `N checks, 0 failures`. Only the
   `test_modbus_slave`, `test_i2c_soft` and `tmp75_test` sources exist today,
   and no harness runs them automatically.

## Style

- `.clang-format`: Microsoft base, `ColumnLimit: 0` (no wrapping), 4 spaces,
  `if ( x )` / `while ( x )` inner spaces, `int* p`, brace on the same line for
  functions. clang-format is not on PATH, so match the file by hand.
- Source comments in English, documentation in Chinese. Public functions get
  Doxygen `@brief`/`@param`/`@return`; `/**< */` for struct members; no `//`.
- Commit subjects use `type(scope): summary` (`feat(board):`, `fix(ports):`);
  the checkout is on `develop`. Never commit `build/`.
