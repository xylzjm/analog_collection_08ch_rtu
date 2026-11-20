# 用 MinGW gcc 构建并运行宿主测试。
#
# 活动对象测试通过 tests/stubs/qp_port.h 中的宿主移植层链接真实的 QP/C 框架源文件
#（与固件构建相同的文件），因此队列、事件池、发布与时间事件语义都是真实语义。
# 假件只存在于设备边界（模拟量输入 ops、TMP75 传输、Modbus 线路/定时器/临界区）
# 以及 FlashDB 链接边界（fdb_fake，针对真实厂商头文件编译）。
#
# 用法：pwsh tests/run_host_tests.ps1 [-KeepExe]

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

# 包含顺序是有实际作用的：tests/stubs 必须排在首位，这样主机的 qp_port.h、
# elog.h 和 fdb_cfg.h 影子才能优先于固件头文件。
$inc = @(
    '-Itests/stubs',
    '-Iapp',
    '-Iboard/inc',
    '-Iboard/ports/qp',
    '-I../middleware/qp/qpc/include',
    '-I../library/interface/analog_input',
    '-I../library/protocol/modbus_slave/inc',
    '-I../library/device/tmp75/inc',
    '-I../library/device/sig24131/inc',
    '-I../middleware/flashDB/inc'
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

$tests = @(
    @{
        Name = 'test_reg_map'
        Sources = @('tests/test_reg_map.c',
                    'app/reg_map.c') + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_modbus_data'
        Sources = @('tests/test_modbus_data.c',
                    'app/modbus_ao.c',
                    'app/reg_map.c',
                    'tests/stubs/fdb_fake.c',
                    'tests/stubs/modbus_port_fake.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_channel_ao'
        Sources = @('tests/test_channel_ao.c',
                    'app/channel_ao.c',
                    'tests/stubs/analog_input_fake.c',
                    'tests/stubs/host_support.c') + $qpSources
        Extra = @()
    },
    @{
        Name = 'test_cold_junction_ao'
        Sources = @('tests/test_cold_junction_ao.c',
                    'app/cold_junction_ao.c',
                    '../library/device/tmp75/src/tmp75.c',
                    'tests/stubs/tmp75_port_fake.c',
                    'tests/stubs/host_support.c') + $qpSources
        Extra = @()
    },
    @{
        Name = 'test_modbus_ao'
        Sources = @('tests/test_modbus_ao.c',
                    'app/modbus_ao.c',
                    'app/reg_map.c',
                    'tests/stubs/fdb_fake.c',
                    'tests/stubs/modbus_port_fake.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
    },
    @{
        Name = 'test_boot'
        Sources = @('tests/test_boot.c',
                    'app/channel_ao.c',
                    'app/cold_junction_ao.c',
                    'app/modbus_ao.c',
                    'app/reg_map.c',
                    '../library/device/tmp75/src/tmp75.c',
                    'tests/stubs/analog_input_fake.c',
                    'tests/stubs/modbus_port_fake.c',
                    'tests/stubs/tmp75_port_fake.c',
                    'tests/stubs/fdb_fake.c',
                    'tests/stubs/host_support.c') + $qpSources + $modbusSources
        Extra = @()
        Runs = @('1', '2')
    },
    @{
        Name = 'test_analog_input_convert'
        Sources = @('tests/test_analog_input_convert.c',
                    '../library/interface/analog_input/analog_input_math.c')
        Extra = @()
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

    # 没有 Runs 的测试只执行一次；test_boot 接受场景编号，
    # 因为 QP 注册表不会在同一进程内重建两次。
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
