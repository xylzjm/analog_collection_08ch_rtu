# Build and run the host tests with MinGW gcc.
#
# The active object tests link the real QP/C framework sources (same files
# as the firmware build) through the host port in tests/stubs/qp_port.h, so
# queue, pool, publish and time-event semantics are the real ones. Fakes
# exist only at the device edges (analog_input ops, TMP75 transfer, Modbus
# line/timer/critical) and for the param component.
#
# Usage: pwsh tests/run_host_tests.ps1 [-KeepExe]

param(
    [switch]$KeepExe
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root 'build\host'
New-Item -ItemType Directory -Force $outDir | Out-Null

$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if ( -not $gcc ) {
    $gcc = 'C:\Users\HP\mingw64\bin\gcc.exe'
}
if ( -not (Test-Path $gcc) ) {
    throw "gcc not found; add C:\Users\HP\mingw64\bin to PATH"
}

$common = @('-std=c99', '-Wall', '-Wextra', '-Wno-unused-parameter')

# Include order is load-bearing: tests/stubs must come first so the host
# qp_port.h, elog.h and flashdb.h shadows win over the firmware headers.
$inc = @(
    '-Itests/stubs',
    '-Iapp',
    '-Iapp/ao/inc',
    '-Iapp/component/inc',
    '-Iboard/inc',
    '-Iboard/ports/qp',
    '-I../middleware/qp/qpc/include',
    '-I../library/interface/analog_input',
    '-I../library/protocol/modbus_slave/inc',
    '-I../library/device/tmp75/inc',
    '-I../library/device/sig24131/inc'
)

$qpSources = @(
    '../middleware/qp/qpc/src/qv/qv.c',
    '../middleware/qp/qpc/src/qf/qep_hsm.c',
    '../middleware/qp/qpc/src/qf/qf_act.c',
    '../middleware/qp/qpc/src/qf/qf_actq.c',
    '../middleware/qp/qpc/src/qf/qf_dyn.c',
    '../middleware/qp/qpc/src/qf/qf_mem.c',
    '../middleware/qp/qpc/src/qf/qf_ps.c',
    '../middleware/qp/qpc/src/qf/qf_qact.c',
    '../middleware/qp/qpc/src/qf/qf_qeq.c',
    '../middleware/qp/qpc/src/qf/qf_time.c'
)

$modbusSources = @(
    '../library/protocol/modbus_slave/src/modbus_slave.c',
    '../library/protocol/modbus_slave/src/modbus_slave_adu.c',
    '../library/protocol/modbus_slave/src/modbus_slave_crc.c',
    '../library/protocol/modbus_slave/src/modbus_slave_function_code.c',
    '../library/protocol/modbus_slave/src/modbus_slave_rtu.c'
)

$stubs = @(
    'tests/stubs/host_support.c',
    'tests/stubs/modbus_port_fake.c',
    'tests/stubs/param_stub.c',
    'tests/stubs/analog_input_fake.c',
    'tests/stubs/tmp75_port_fake.c'
)

$tests = @(
    @{
        Name = 'test_ch_config'
        Sources = @('app/component/test/test_ch_config.c',
                    'app/component/src/ch_config.c')
        Extra = @()
    },
    @{
        Name = 'test_reg_map'
        Sources = @('app/component/test/test_reg_map.c',
                    'app/component/src/reg_map.c',
                    'app/component/src/ch_config.c') + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_analog_input_convert'
        Sources = @('tests/app/test_analog_input_convert.c',
                    'board/src/analog_input_convert.c',
                    '../library/interface/analog_input/analog_input_math.c')
        Extra = @()
    },
    @{
        Name = 'test_channel_ao'
        Sources = @('tests/app/test_channel_ao.c',
                    'app/ao/src/channel_ao.c',
                    'app/component/src/reg_map.c',
                    'app/component/src/ch_config.c',
                    'tests/stubs/analog_input_fake.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_modbus_ao'
        Sources = @('tests/app/test_modbus_ao.c',
                    'app/ao/src/modbus_ao.c',
                    'app/component/src/reg_map.c',
                    'app/component/src/ch_config.c',
                    'tests/stubs/modbus_port_fake.c',
                    'tests/stubs/param_stub.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_boot'
        Sources = @('tests/app/test_boot.c',
                    'app/ao/src/channel_ao.c',
                    'app/ao/src/cold_junction_ao.c',
                    'app/ao/src/modbus_ao.c',
                    'app/component/src/reg_map.c',
                    'app/component/src/ch_config.c',
                    '../library/device/tmp75/src/tmp75.c',
                    'tests/stubs/analog_input_fake.c',
                    'tests/stubs/modbus_port_fake.c',
                    'tests/stubs/param_stub.c',
                    'tests/stubs/tmp75_port_fake.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
        Runs = @('1', '2')
    }
)

$failed = @()
foreach ( $t in $tests ) {
    $exe = Join-Path $outDir "$($t.Name).exe"
    Write-Host "==> building $($t.Name)"
    & $gcc @common @inc @($t.Sources) @($t.Extra) -o $exe
    if ( $LASTEXITCODE -ne 0 ) {
        $failed += $t.Name
        continue
    }

    # A test without Runs executes once; test_boot takes a scenario number
    # because the QP registry is not rebuilt twice inside one process.
    $runs = @()
    if ( $t.ContainsKey('Runs') ) {
        $runs = $t.Runs
    }
    else {
        $runs = @('')
    }
    foreach ( $r in $runs ) {
        Write-Host "==> running  $($t.Name)$(if ($r) { " scenario $r" })"
        if ( $r -eq '' ) {
            & $exe
        }
        else {
            & $exe $r
        }
        if ( $LASTEXITCODE -ne 0 ) {
            $failed += "$($t.Name)$(if ($r) { "/$r" })"
        }
    }
}

if ( -not $KeepExe ) {
    Remove-Item (Join-Path $outDir '*.exe') -ErrorAction SilentlyContinue
}

if ( $failed.Count -gt 0 ) {
    Write-Host "FAILED: $($failed -join ', ')"
    exit 1
}
Write-Host 'ALL HOST TESTS PASSED'
