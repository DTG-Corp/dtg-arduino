$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$common = @("-std=c++17")
$probeSrc = Join-Path $env:TEMP "dtg_asan_probe.cpp"
$probeOut = Join-Path $env:TEMP "dtg_asan_probe.exe"
Set-Content -Path $probeSrc -Value "int main(){return 0;}" -Encoding ASCII
& g++ "-std=c++17" "-fsanitize=address,undefined" $probeSrc -o $probeOut 2>$null
if ($LASTEXITCODE -eq 0) {
    $common += "-fsanitize=address,undefined"
    Write-Host "ASan/UBSan enabled"
} else {
    Write-Host "ASan/UBSan not available on this toolchain; compiling without sanitizers"
}

function Invoke-HostTest {
    param([string]$Name, [string[]]$GccArgs)
    Write-Host "=== $Name ==="
    & g++ @common @GccArgs
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $Name" }
    & ".\$Name"
    if ($LASTEXITCODE -ne 0) { throw "run failed: $Name" }
}

Invoke-HostTest "ota_sha256_test" @("-I../src", "-o", "ota_sha256_test", "ota_sha256_test.cpp")
Invoke-HostTest "tls_roots_parse_test" @("-I../src", "-o", "tls_roots_parse_test", "tls_roots_parse_test.cpp")
Invoke-HostTest "modbus_codec_test" @("-I../src", "-o", "modbus_codec_test", "modbus_codec_test.cpp", "../src/modbus/modbus_codec.cpp")
Invoke-HostTest "dtg_param_test" @("-Istubs", "-I../src", "-o", "dtg_param_test", "dtg_param_test.cpp")
Invoke-HostTest "timer_test" @("-DDTG_TIMER_MAX=4", "-I../src", "-o", "timer_test", "timer_test.cpp", "../src/dtg_timer.cpp")
Invoke-HostTest "heap_guard_logic_test" @("-I../src", "-o", "heap_guard_logic_test", "heap_guard_logic_test.cpp")
Invoke-HostTest "heap_telemetry_test" @("-I../src", "-o", "heap_telemetry_test", "heap_telemetry_test.cpp")
Invoke-HostTest "wifi_cmd_payload_test" @("-I../src", "-o", "wifi_cmd_payload_test", "wifi_cmd_payload_test.cpp")

Write-Host "All host tests OK"
