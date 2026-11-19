# AGENTS.md

8-channel analog-input RTU firmware (4-20 mA, 0-10 V, PT100, thermocouple) for
AT32F421C8T7: Cortex-M4, 120 MHz, 60 KB usable flash, 16 KB SRAM. C99, armclang
(AC6), microlib, QP/C 8.1.3 (QV kernel), EIDE project only - no Makefile, CMake,
CI, lint or typecheck step exists.

## Start with README.md and docs/

- README.md is the board-level design reference: boot order, clock and interrupt
  priorities, pin mux, SIG24131/TMP75/I2C/Modbus resources, logging chain, flash
  partition, QP port and assertion policy.
- The application layer lives in `docs/`: `qp-application-design.md` (AO
  partition, event contract, capacity), `docs/ao/*.md` (one per active object)
  and `docs/component/*.md` (passive components).
- When designed behavior changes, update the doc and keep source comments to
  short Doxygen `@brief` lines.
- Docs are UTF-8 with CRLF line endings; source and headers are pure ASCII.
  This Windows shell mangles non-ASCII output, so judge file encoding from
  bytes, not from printed text.

## Dependencies live outside this repo

The build reads sibling folders that are neither vendored nor submodules:
`../library` (sig24131, tmp75, analog_input, i2c_soft, modbus_slave) and
`../middleware` (qp, SEGGER RTT, EasyLogger, flashDB/FAL). The multi-root
workspace file adds both folders; without them nothing compiles.

## Build and flash

- Firmware builds only through EIDE: VS Code tasks `build`, `rebuild`, `clean`,
  `flash`, `build and flash`, or the EIDE MCP tools with project uid
  `2b2c107904df4cdb1e97e57d6947db29` (`rebuild` is the reliable full check).
- A clean build is 70 C files + 1 assembly file, ROM about 52.3/60 KB (87 %),
  RAM 9.3/16 KB. `-O1` is deliberate: at `-O0` the full QP runtime plus FlashDB
  KV layer needs roughly 70 KB and does not fit. Treat ROM headroom as scarce.
- Never chip-erase when flashing: the last 4 KB (`0x0800F000..0x0800FFFF`) holds
  the `fdb_kvdb` parameter partition, and ROM is linked to 60 KB (`0xF000`).
- SKU variants fix the channel count at compile time: define
  `SIG24131_BOARD_COUNT=N` (1..8) in the EIDE preprocessor macros
  (`.eide/eide.yml`). The knob resolves in `board/inc/board_config.h`
  (`BOARD_CHANNEL_COUNT`); every layer name derives from it. Unpopulated
  positions answer ILLEGAL_DATA_ADDRESS, KV keys stay at the 8-position board
  layout. Host tests model the full 8-position board; verify a variant by
  compiling the firmware plus the on-target EX 02 checks, not by re-running the
  host tests.
- `build/` is gitignored and often stale. Never treat `build/Debug/*.lnp`,
  `*.map` or `.obj` as the current file list - rebuild first.
  `build/Debug/compile_commands.json` is regenerated per build and feeds clangd.

## Adding source files

- `.eide/eide.yml` lists every file explicitly (`srcDirs: []`). A new `.c` that
  is not added to the target's virtual folder/file list is silently left out of
  the build. Register it in the EIDE UI (or the YAML), reload/rebuild.
- Include order in `incList` is load-bearing: `board/ports/easylogger` must stay
  before `../middleware/EasyLogger/easylogger/inc`, otherwise the template
  `elog_cfg.h` shadows `board/ports/easylogger/elog_cfg.h`.
- `.eide/eide.yml` writes `board/ports/flashdb` in lower case while the folder is
  `board/ports/flashDB`. Harmless on Windows, breaks on case-sensitive hosts.

## Generated and vendor code

- `board/vendor_wk/project/` is AT32 WorkBench output driven by
  `board/vendor_wk/vendor_wk.ATWP`; `.pack/` is the AT32F421 device pack. Keep
  their copyright and generated comments and their MS-style formatting.
- No file under `board/vendor_wk/project/` carries hand edits: the DE pin,
  USART1, TMR14 and the TMR6 delay time base all live in the `.ATWP`, so
  regenerating from it is safe by construction. The board port owns its
  peripheral preconditions regardless of the generated init state
  (ownership comments in `board/src/modbus_slave_board.c`).
- Vendor `main.c` and `at32f421_int.c` are not part of the build. Entry point is
  `app/main.c`; IRQ handlers live in board modules (`SysTick_Handler` in
  `board/src/board.c`, `USART1_IRQHandler` / `TMR14_GLOBAL_IRQHandler` in
  `board/src/modbus_slave_board.c`).

## Application layer

- 10 active objects (8x AoChannel at prio 2..9, AoColdJunction 10, AoModbus 11)
  plus the passive components `ch_config` / `reg_map` / `modbus_image` / `param`
  (+ `param_defaults.c`; storage sits behind the `kv_store` adapter, target
  FlashDB in `kv_store_flashdb.c`, host memory store in `tests/stubs`). QV is
  cooperative: active objects never preempt each
  other; ISRs only post events to their queues. SysTick (1 ms) is the single
  tick source for QP time events and `board_tick_get()`.
- `modbus_image` owns the typed Modbus mirror and the write transaction
  (QP-free, host-tested directly through the data-space callbacks). AoModbus
  keeps only routing, boot publishing and the commit-to-publish conversion via
  the image's notifier callback.
- Active objects take their device objects by constructor injection (no board
  singletons inside `app/ao/`): `main.c` passes the board singletons to the
  channel and cold-junction constructors, and composes the Modbus slave from
  `modbus_ao_callbacks()` / `modbus_ao_identity()` before `modbus_ao_attach()`.
- Start order: channels, then AoModbus, then AoColdJunction. Every AO subscribes
  inside its own initial transition (QP/C example style; `QActive_start()`
  registers the AO before running it), and `param_boot_load()` runs inside the
  AoModbus initial transition before `QF_run()`.
- Channel indices are zero-based in the app layer (events, `cfg/ch0`..`cfg/ch7`
  KV keys) but one-based in the board API: `analog_input_board_get()` takes
  `i + 1`, as `main.c` does.
- `on_frame_received` is routed to `modbus_slave_advance()` via `FRAME_SIG`.
  On-target behavior (timing, flash commit, wire-break codes) is still
  unverified on hardware.

## Hard invariants

- `log_init()` must run before `board_init()`, or that function's `elog_error()`
  diagnostics are lost. No logging from interrupt context:
  `elog_port_output_lock` is empty.
- `param_bind_store()` must run before `QActive_start(AoModbus)`: the boot
  load runs in that AO's initial transition and fails safe (`db_ok = 0`, b3)
  when no store is bound. `main.c` binds `kv_store_flashdb_ops()`, host tests
  bind `kv_store_mem`.
- Critical sections only through `board_critical_enter()` /
  `board_critical_exit()` (`board/inc/board.h`). The nest counter is shared with
  QP, which reaches it via the strong `QF_crit_entry_`/`QF_crit_exit_` overrides
  in `board/src/board.c`. Do not add `__disable_irq()`, a second QP port, or a
  local nesting counter; contract violations trap in `Q_onError` with 110/111.
  The section masks all interrupts through PRIMASK (`QF_USE_BASEPRI` is not
  enabled), including the soft-I2C bit delays.
- Assertions use `Q_ASSERT`/`Q_onError`; `Q_onError` writes raw RTT and is safe
  from ISRs. FlashDB and EasyLogger asserts already bridge to it.
- `FDB_WRITE_GRAN` (8) is the single source: the FAL port builds
  `fal_flash_dev.write_gran` from it; a hand-edited divergence corrupts the
  KV layout silently.
- USART1 runs 9600 8N1. `modbus_slave_board.c` feeds the core from
  `BOARD_MODBUS_BAUDRATE` (`board_config.h`, the single home of the core's
  baud); the generated `wk_usart.c` init literal must stay in step with it.
- USART1 and TMR14 share preemption priority 5 and must not preempt each other.
  Modbus RX/TX buffers are 256 B each; TMR14 counts 10 us ticks.

## Verifying changes

1. EIDE rebuild must succeed.
2. Run the host tests from the repo root: `pwsh tests/run_host_tests.ps1`
   (MinGW gcc on PATH; `-KeepExe` keeps the executables). The script compiles
   and runs each test separately; a new test must be added to its `$tests`
   table. `tests/stubs` must stay first in the
   include order so the host `qp_port.h`, `elog.h` and `flashdb.h` shadows win
   over the firmware headers.
   - `app/component/test/`: pure component tests (self-contained `main()` +
     `CHECK`), no QP linkage. Today: `test_ch_config`, `test_reg_map`,
     `test_modbus_image`, `test_param`.
   - `tests/app/`: active-object tests driven by event injection. The real QP/C
     sources (`../middleware/qp/qpc/src/qf/*` + `qv.c`) link against the host
     port `tests/stubs/qp_port.h`, which shares the firmware `qp_config.h`; the
     dispatch loop replicates the QV triple (`QActive_get_` -> `QASM_DISPATCH`
     -> `QF_gc`). Fakes exist only at the device edges: `analog_input_fake`,
     `tmp75_port_fake` (behind the real `tmp75.c`), `modbus_port_fake` (behind
     the real slave core), and `kv_store_mem` (an in-memory KV store behind the
     real `param`). `test_boot` takes a scenario argument (1, 2) because the QP
     registry is not rebuilt twice inside one process.
     `test_analog_input_convert` instead tests the pure board conversion
     module (`board/src/analog_input_convert.c`) against hand-derived
     electrical anchors, with no QP linkage.
3. The script prints `N checks, 0 failures` per test. `tests/` is outside the
   EIDE build, so host tests say nothing about ROM or target timing.
4. Pure-logic code in `../library` has host tests under `<module>/test/*.c` -
   self-contained `main()` plus a `CHECK` macro, excluded from the firmware
   build. Four exist today (`analog_input_math`, `i2c_soft`, `modbus_slave`,
   `tmp75`) and no harness runs them automatically. Compile with host gcc, for
   example:

   ```powershell
   gcc -std=c99 -I ../library/protocol/modbus_slave/inc `
     ../library/protocol/modbus_slave/test/test_modbus_slave.c `
     ../library/protocol/modbus_slave/src/*.c -o build/host/test_modbus_slave.exe
   ```

## Style

- `.clang-format`: Microsoft base, `ColumnLimit: 0` (no wrapping), 4 spaces,
  `if ( x )` / `while ( x )` inner spaces, `int* p`, function opening brace on
  its own line. clang-format is not on PATH, so match the file by hand.
- Source comments in English, documentation in Chinese. Public functions get
  Doxygen `@brief`/`@param`/`@return`; `/**< */` for struct members; no `//`.
- Commit subjects use `type(scope): summary` (`feat(board):`, `fix(ports):`);
  the checkout is on `develop`. Never commit `build/`.
