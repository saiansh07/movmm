#!/usr/bin/env pwsh
# MoVMM-Engine Startup Script
# Windows 11 compatible - no Linux/WSL/Docker required

param([ValidateRange(1, 65535)][int]$Port = 8000)

Write-Host "=== MoVMM-Engine ===" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = 'Stop'
$dashboard = "http://127.0.0.1:$Port"
$portCheck = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$portCheck.ExclusiveAddressUse = $true
try {
    $portCheck.Start()
} catch [System.Net.Sockets.SocketException] {
    $bindError = $_.Exception.Message
    $api = $null
    try {
        $api = Invoke-RestMethod -Uri "$dashboard/openapi.json" -TimeoutSec 3
    } catch {
        # The existing listener may belong to a different application.
    }
    if ($api.info.title -like 'MoVMM-Engine*' -and $api.paths.'/api/v1/health') {
        $health = Invoke-RestMethod -Uri "$dashboard/api/v1/health" -TimeoutSec 3
        if (-not $health.engine_initialized) {
            throw "MoVMM is already listening at $dashboard, but its engine is unavailable. Stop the existing launcher before restarting."
        }
        Write-Host "MoVMM is already running at $dashboard" -ForegroundColor Green
        return
    }
    throw "Cannot start MoVMM at $dashboard. Port $Port is unavailable ($bindError)."
} finally {
    $portCheck.Stop()
}

Push-Location -LiteralPath $PSScriptRoot
try {
    $venvPath = Join-Path $env:USERPROFILE 'movmm_venv'
    $venvPython = Join-Path $venvPath 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
        $candidates = @(
            if ($env:LOCALAPPDATA) {
                Join-Path $env:LOCALAPPDATA 'Programs\Python\Python314\python.exe'
            }
            Get-Command python.exe -CommandType Application -All -ErrorAction SilentlyContinue |
                Where-Object { $_.Source -notlike '*\Microsoft\WindowsApps\*' } |
                Select-Object -ExpandProperty Source
        )
        $python = $null
        foreach ($candidate in ($candidates | Select-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
            & $candidate -c 'import venv, ensurepip'
            if ($LASTEXITCODE -eq 0) {
                $python = $candidate
                break
            }
        }
        if (-not $python) { throw 'No usable Python executable found. Install Python 3.14 or add Python to PATH.' }

        & $python -m venv $venvPath
        if ($LASTEXITCODE -ne 0) { throw 'Failed to create the Python environment.' }
    }

    & $venvPython -m pip install -r cpp/python/requirements.txt
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install backend dependencies.' }

    $engine = Get-Item -LiteralPath 'engine_cli.exe' -ErrorAction SilentlyContinue
    $sources = Get-ChildItem -Path 'cpp/*.cpp','cpp/*.h'
    if (-not $engine -or ($sources | Where-Object LastWriteTime -gt $engine.LastWriteTime)) {
        $compiler = Get-Command g++ -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $compilerPath = if ($compiler) { $compiler.Source } else { 'C:\msys64\ucrt64\bin\g++.exe' }
        & $compilerPath -std=c++17 -static cpp/engine_cli.cpp cpp/engine.cpp cpp/heap_allocator.cpp cpp/mmu.cpp cpp/paging.cpp cpp/swap.cpp cpp/events.cpp -o engine_cli.exe
        if ($LASTEXITCODE -ne 0) { throw 'Failed to build the engine CLI.' }
    }

    & $venvPython -m uvicorn cpp.python.main:app --host 127.0.0.1 --port $Port
    if ($LASTEXITCODE -ne 0) { throw "Uvicorn exited with code $LASTEXITCODE. See the server output above for the cause." }
} finally {
    Pop-Location
}
