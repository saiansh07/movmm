#!/usr/bin/env pwsh
# MoVMM-Engine Phase 4 Startup Script
# Windows 11 compatible - no Linux/WSL/Docker required

Write-Host "=== MoVMM-Engine: Phase 4 - FastAPI Backend ===" -ForegroundColor Cyan
Write-Host ""

# 1. Verify required tools
Write-Host "Checking prerequisites..." -ForegroundColor Yellow

$has_gplusplus = Test-Path "C:\Program Files\Git\bin\g++.exe" -or (Get-Command g++ -ErrorAction SilentlyContinue)
$has_python = Get-Command python -ErrorAction SilentlyContinue
$has_cmake = Get-Command cmake -ErrorAction SilentlyContinue

if (-not $has_python) {
    Write-Warning "Python not found in PATH. Installing Python..."
    # Note: In production, use chocolatey or winget
    Write-Host "Please install Python from https://python.org" -ForegroundColor Red
    return 1
}

Write-Host "Python found: $($host.version)" -ForegroundColor Green

# 2. Set up Python virtual environment
$venv_path = Join-Path $env:USERPROFILE "movmm_venv"

if (-not (Test-Path $venv_path)) {
    Write-Host "Creating Python virtual environment..." -ForegroundColor Yellow
    try {
        python -m venv $venv_path
    } catch {
        Write-Host "Failed to create venv. Install python venv module." -ForegroundColor Red
        return 1
    }
}

# Activate virtual environment
$activate_script = Join-Path $venv_path "Scripts\Activate.ps1"
if (Test-Path $activate_script) {
    . $activate_script
    Write-Host "Virtual environment activated." -ForegroundColor Green
}

# 3. Install Python dependencies
Write-Host "Installing Python dependencies..." -ForegroundColor Yellow
$requirements_path = Join-Path $PSScriptRoot "python\requirements.txt"
if (Test-Path $requirements_path) {
    python -m pip install --upgrade pip
    python -m pip install -r $requirements_path
} else {
    Write-Host "python\requirements.txt not found. Installing core packages..." -ForegroundColor Yellow
    python -m pip install fastapi "uvicorn[standard]" pydantic
}

Write-Host "Dependencies installed." -ForegroundColor Green

# 4. Compile C++ engine if needed
$engine_exe = Join-Path $PSScriptRoot "engine_cli.exe"
$cpp_src = Join-Path $PSScriptRoot "cpp\engine_cli.cpp"

if (-not (Test-Path $engine_exe)) {
    Write-Host "Compiling C++ engine..." -ForegroundColor Yellow
    try {
        # Use g++ from MSYS2 if available, otherwise warn
        if (Test-Path "C:\msys64\ucrt64\bin\g++.exe") {
            & "C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -Icpp -Icpp/json $cpp_src -o $engine_exe
        } else if (Test-Path "C:\Program Files\Git\bin\g++.exe") {
            & "C:\Program Files\Git\bin\g++.exe" -std=c++17 -Icpp -Icpp/json $cpp_src -o $engine_exe
        } else {
            Write-Warning "Cannot find g++. C++ engine must be pre-compiled."
        }
    } catch {
        Write-Warning "C++ compilation failed. Pre-compile engine_cli.exe manually."
    }
}

if (Test-Path $engine_exe) {
    Write-Host "C++ engine ready: $engine_exe" -ForegroundColor Green
}

# 5. Start FastAPI server
Write-Host "Starting FastAPI server..." -ForegroundColor Yellow
$app_path = Join-Path $PSScriptRoot "python\main.py"

if (Test-Path $app_path) {
    $port = 8000
    Write-Host "Starting server at http://127.0.0.1:$port" -ForegroundColor Cyan
    Write-Host "API docs at: http://127.0.0.1:$port/docs" -ForegroundColor Cyan
    Write-Host "Simulation WS at: ws://127.0.0.1:$port/ws/simulation" -ForegroundColor Cyan

    # Start uvicorn
    $host_url = "127.0.0.1:$port"
    python -m uvicorn.main:main --host $host_url --port $port --app-path $app_path
} else {
    Write-Error "Python app not found at $app_path"
    return 1
}