#!/usr/bin/env pwsh
# MoVMM-Engine Phase 4 Startup Script
# Windows 11 compatible - no Linux/WSL/Docker required

Write-Host "=== MoVMM-Engine: Phase 4 - FastAPI Backend ===" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    $python = (Get-Command python -CommandType Application).Source
    $venvPath = Join-Path $env:USERPROFILE 'movmm_venv'
    $venvPython = Join-Path $venvPath 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $venvPython)) {
        & $python -m venv $venvPath
        if ($LASTEXITCODE -ne 0) { throw 'Failed to create the Python environment.' }
    }

    & $venvPython -m pip install -r cpp/python/requirements.txt
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install backend dependencies.' }

    if (-not (Test-Path -LiteralPath 'engine_cli.exe')) {
        $compiler = Get-Command g++ -CommandType Application -ErrorAction SilentlyContinue
        $compilerPath = if ($compiler) { $compiler.Source } else { 'C:\msys64\ucrt64\bin\g++.exe' }
        & $compilerPath -std=c++17 cpp/engine_cli.cpp cpp/engine.cpp cpp/heap_allocator.cpp cpp/mmu.cpp cpp/paging.cpp cpp/swap.cpp cpp/events.cpp -o engine_cli.exe
        if ($LASTEXITCODE -ne 0) { throw 'Failed to build the engine CLI.' }
    }

    & $venvPython -m uvicorn cpp.python.main:app --host 127.0.0.1 --port 8000
    if ($LASTEXITCODE -ne 0) { throw 'The backend exited with an error.' }
} finally {
    Pop-Location
}
