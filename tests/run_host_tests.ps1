# Build and run the host tests with MinGW gcc.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root 'build\host'
New-Item -ItemType Directory -Force $outDir | Out-Null

$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) {
    $gcc = 'C:\Users\HP\mingw64\bin\gcc.exe'
}
if (-not (Test-Path $gcc)) {
    throw "gcc not found; add C:\Users\HP\mingw64\bin to PATH"
}

$common = @('-std=c99', '-Wall', '-Wextra', '-Wno-unused-parameter')
$incApp = @('-Iapp/inc')
$incAnalog = @('-I../library/interface/analog_input')
$incStubs = @('-Itests/stubs')
$incModbus = @('-I../library/protocol/modbus_slave/inc')
$incQp = @('-I../middleware/qp/qpc/include', '-Iboard/ports/qp')

function Invoke-Test {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$Includes,
        [string[]]$Extra = @()
    )

    $exe = Join-Path $outDir "$Name.exe"
    Write-Output "=== $Name : compile ==="
    & $gcc @common @Includes @Extra @Sources -o $exe
    if ($LASTEXITCODE -ne 0) {
        throw "$Name compile failed"
    }

    Write-Output "=== $Name : run ==="
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Push-Location $root
try {
    Invoke-Test 'test_modbus_app_regs' `
        @('tests/app/test_modbus_app_regs.c', 'app/src/modbus_app_regs.c') `
        ($incApp + $incAnalog)

    Invoke-Test 'test_cal_regs' `
        @('tests/app/test_cal_regs.c', 'app/src/modbus_cal_regs.c',
          'app/src/modbus_app_regs.c') `
        ($incApp + $incAnalog)

    Invoke-Test 'test_param_app' `
        @('tests/app/test_param_app.c', 'app/src/param_app.c') `
        ($incApp + $incStubs + $incAnalog)

    Invoke-Test 'test_cal_data' `
        @('tests/app/test_cal_data.c', 'app/src/cal_data.c',
          'tests/stubs/analog_input_board_stub.c') `
        ($incApp + $incStubs + $incAnalog)

    $modbusAppSources = @('tests/app/test_modbus_app.c', 'app/src/modbus_app.c',
          'app/src/calibrate_app.c', 'app/src/cal_data.c',
          'app/src/modbus_app_regs.c', 'app/src/modbus_cal_regs.c',
          'app/src/param_app.c', 'app/src/channel_app_stub.c',
          'tests/stubs/analog_input_board_stub.c', 'tests/stubs/qp_host_stub.c',
          'tests/stubs/modbus_slave_board_stub.c',
          '../middleware/qp/qpc/src/qf/qep_hsm.c')
    $modbusAppIncludes = $incApp + $incStubs + $incAnalog + $incModbus +
        $incQp + @('-Iboard/inc')
    Invoke-Test 'test_modbus_app' $modbusAppSources $modbusAppIncludes `
        @('-DQ_UNSAFE')
    Invoke-Test 'test_modbus_app_safe' $modbusAppSources $modbusAppIncludes

    $calibrateAppSources = @('tests/app/test_calibrate_app.c',
          'app/src/calibrate_app.c', 'app/src/cal_data.c',
          'app/src/channel_app_stub.c',
          'tests/stubs/analog_input_board_stub.c', 'tests/stubs/qp_host_stub.c',
          '../middleware/qp/qpc/src/qf/qep_hsm.c')
    $calibrateAppIncludes = $incApp + $incStubs + $incAnalog + $incQp
    Invoke-Test 'test_calibrate_app' $calibrateAppSources $calibrateAppIncludes `
        @('-DQ_UNSAFE')
    Invoke-Test 'test_calibrate_app_safe' $calibrateAppSources `
        $calibrateAppIncludes

    $storageAppSources = @('tests/app/test_storage_app.c',
          'app/src/storage_app.c', 'app/src/param_app.c',
          'tests/stubs/storage_flash_stub.c', 'tests/stubs/qp_host_stub.c',
          '../middleware/qp/qpc/src/qf/qep_hsm.c')
    $storageAppIncludes = $incApp + $incStubs + $incAnalog + $incModbus + $incQp
    Invoke-Test 'test_storage_app' $storageAppSources $storageAppIncludes `
        @('-DQ_UNSAFE')
    Invoke-Test 'test_storage_app_safe' $storageAppSources `
        $storageAppIncludes

    Invoke-Test 'test_storage_flash' `
        @('tests/app/test_storage_flash.c', 'app/src/storage_flash.c',
          'app/src/param_app.c', 'app/src/cal_data.c',
          'app/src/modbus_app_regs.c', 'tests/stubs/flashdb_stub.c',
          'tests/stubs/analog_input_board_stub.c') `
        ($incApp + $incStubs + $incAnalog)

    Invoke-Test 'test_analog_input_board' `
        @('tests/app/test_analog_input_board.c',
          'board/src/analog_input_board.c',
          '../library/interface/analog_input/analog_input_math.c',
          'tests/stubs/sig24131_stub.c') `
        @('-Iboard/inc', '-I../library/interface/analog_input',
          '-I../library/device/sig24131/inc', '-Itests/stubs') `
        @('-include', 'tests/stubs/board.h',
          '-include', 'tests/stubs/at32f421_wk_config.h',
          '-include', 'tests/stubs/sig24131_board.h',
          '-include', 'tests/stubs/wk_system.h', '-lm')

    Write-Output 'OVERALL: PASS'
}
finally {
    Pop-Location
}
