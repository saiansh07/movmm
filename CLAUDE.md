# MoVMM-Engine: Design and Implementation of a Modular Virtual Memory Manager and Custom Allocator Engine

## Project Identity
- **Course**: Operating Systems Lab (24B11CS312)
- **Institution**: Jaypee Institute of Information Technology, Noida
- **Supervisor**: Dr. Neeraj Pathak
- **Team**: 4 members (college project)
- **Project name**: MoVMM-Engine

## Technology Constraints
- **Core**: C++17 or newer, CMake
- **Backend**: Python, FastAPI, Pydantic, Uvicorn
- **Frontend**: HTML5, Basic CSS, Vanilla JavaScript, Tailwind CSS
- **No React, Vue, Angular, Next.js, Vite, TypeScript, Redux, or other frontend frameworks**
- **Windows 11 native** — no Linux, WSL, Docker, VM, Bash, or POSIX-only APIs
- Use PowerShell, MinGW-w64, CMake, Python, standard C++ filesystem APIs, Windows-compatible process management

## Architecture Rules
- C++ engine is the **single source of truth** for all simulated memory state
- Browser must never independently fabricate memory operations or maintain a competing simulation
- Communication: C++ engine ↔ FastAPI backend (JSON)
- State snapshots and event reporting via JSON over stdio or file-based handoff
- Modular architecture with clearly defined C++ header interfaces
- Configurable page sizes from 256 bytes to 4 KB

## Git & GitHub Rules (MANDATORY)
- Remote: `origin` at `https://github.com/saiansh07/movmm.git`
- Target branch: `main`
- After every completed and successfully tested development phase:
  1. Inspect git status
  2. Review modified files
  3. Run relevant builds and tests
  4. Stage only files belonging to that phase
  5. Commit with a descriptive conventional commit message
  6. Push the commit to `origin/main`
  7. Verify that the push succeeded
- **Never**: force-push, rewrite existing history, overwrite user changes, blindly `git add .`, commit credentials/API keys/.env/binaries/build directories/swap files/temporary artifacts
- If authentication, remote configuration, or push fails, stop and report the exact problem
- If initial commit or branch setup is needed, handle it safely — inspect the remote before reconciling any existing history

## Development Rules
- Inspect before modifying
- Preserve working code
- Do not rewrite existing modules without a technical reason
- Do not leave placeholder implementations
- Do not use fake statistics
- Do not silently swallow errors
- Do not claim untested features work
- Keep the project modular
- Test every meaningful subsystem
- Document important architectural decisions
- Ask only when a genuine design decision cannot be resolved safely from the existing project

## Windows 11 Requirement
- Entire application must run natively on Windows 11
- Use PowerShell, MinGW-w64, CMake, Python, standard C++ filesystem APIs, Windows-compatible process management
- Provide Windows-compatible commands and paths
- Do not require Linux, WSL, Docker, VM, Bash, or POSIX-only APIs

## Project Objectives
Build a modular virtual memory management framework that simulates important memory-management mechanisms found in modern operating systems. The project implements its own memory-management logic rather than relying on the host OS memory allocator for simulated operations. The core is a C++ simulation engine extended with a browser-based interactive interface for configuring a simulated computer, executing memory workloads, and observing subsystem responses.

## Team Responsibilities
- **Member 1 — Anirudh Goel**: User-Space Heap Manager (my_malloc/my_free, free-list management, First-Fit/Best-Fit, block splitting, adjacent free-block coalescing, fragmentation tracking)
- **Member 2 — Saiansh Singh**: MMU and TLB (page-table structures, 16-bit virtual address decomposition, virtual-to-physical translation, TLB lookup/updates, hit/miss tracking, address permissions/validation)
- **Member 3 — Avish Mittal**: Demand Paging and Replacement (page-fault handling, physical-frame management, FIFO/LRU replacement, reference/dirty bits, fault/replacement statistics)
- **Member 4 — Vinayak Arora**: Swap File Subsystem (persistent binary swap.bin, page-in/page-out, swap-space indexing, dirty-page writeback, disk I/O statistics)

## Application Sections (Frontend)
- Overview
- Machine Configuration
- Memory Map
- Process Manager
- Heap Allocator
- MMU & TLB
- Paging Simulator
- Swap Manager
- Performance Analytics
- Execution History

## Color Scheme
- Charcoal backgrounds
- Green for free memory
- Blue for occupied memory
- Orange for warnings and page faults
- Red for errors
- Purple for swap operations
- Minimal, technical appearance; no generic SaaS styling or excessive gradients

## Completion Conditions for Phase 1
- Repository inspected and documented
- Existing engine status documented
- Architecture documented
- CLAUDE.md exists with permanent instructions
- docs/project/PROJECT_CONTEXT.md exists
- docs/project/ARCHITECTURE.md exists
- docs/project/DEVELOPMENT_STATUS.md exists
- .gitignore is appropriate
- Phased implementation plan is documented
- No existing working functionality has been damaged