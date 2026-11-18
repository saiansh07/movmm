"""MoVMM-Engine Phase 4 - Backend Configuration"""

import os
import json
import subprocess
import sys
from pathlib import Path
from typing import Optional, Dict, Any, List

# Configuration defaults
DEFAULT_ENGINE_CONFIG = {
    "ram_size_bytes": 4096,
    "page_size": 256,
    "tlb_capacity": 4,
    "swap_capacity_pages": 64,
    "fit_policy": "first_fit",
    "replacement_algo": "fifo"
}

# Path to the compiled C++ engine CLI
DEFAULT_ENGINE_CLI = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "engine_cli.exe")

# Platform-specific engine executable extensions
ENGINE_EXE_EXT = ".exe" if os.name == "nt" else ""

# Engine process management
class EngineProcess:
    """Manages a persistent C++ engine subprocess."""

    def __init__(self, cli_path: str = DEFAULT_ENGINE_CLI):
        self.cli_path = Path(cli_path).resolve()
        self.process: Optional[subprocess.Popen] = None
        self.request_counter = 0
        self pending_responses: Dict[int, Dict] = {}

    def start(self) -> bool:
        """Start the persistent C++ engine subprocess."""
        if not self.cli_path.exists():
            raise FileNotFoundError(f"Engine CLI not found: {self.cli_path}")

        try:
            self.process = subprocess.Popen(
                [str(self.cli_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0,  # Unbuffered
                creationflags=subprocess.CREATE_NEW_CONSOLE if os.name == "nt" else 0
            )
            return True
        except Exception as e:
            raise RuntimeError(f"Failed to start engine subprocess: {e}")

    def stop(self) -> None:
        """Stop the persistent engine subprocess."""
        if self.process and self.process.poll() is None:
            # Send shutdown command
            try:
                request = json.dumps({"id": "shutdown", "command": "terminate"})
                self.process.stdin.write(request.encode() + "\n")
                self.process.stdin.flush()
                self.process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()
        self.process = None

    def send_command(self, command: str, params: Optional[Dict] = None) -> Optional[Dict]:
        """Send a JSON command and get the response."""
        if not self.process or self.process.poll() is not None:
            raise RuntimeError("Engine subprocess not running")

        self.request_counter += 1
        request_id = self.request_counter

        # Build JSON request
        request_obj = {"id": request_id, "command": command}
        if params:
            request_obj["params"] = params

        try:
            # Send request
            self.process.stdin.write(json.dumps(request_obj).encode() + "\n")
            self.process.stdin.flush()

            # Read response (with timeout handling)
            import select
            ready, _, _ = select.select(
                [self.process.stdout], [], [], 5.0
            )

            if ready:
                response_line = self.process.stdout.readline().decode().strip()
                try:
                    response = json.loads(response_line)

                    # Store by request ID for correlation
                    if "id" in response and response["id"] == request_id:
                        self.pending_responses[request_id] = response

                    # Remove processed response
                    if request_id in self.pending_responses:
                        del self.pending_responses[request_id]

                    return response
                except json.JSONDecodeError:
                    return {"error": "Invalid JSON response", "raw": response_line}

            return {"error": "Timeout waiting for engine response"}

        except Exception as e:
            return {"error": str(e)}

    def is_running(self) -> bool:
        """Check if the engine subprocess is still running."""
        return self.process is not None and self.process.poll() is None


# Global engine process instance
_engine_process: Optional[EngineProcess] = None

def get_engine_process() -> EngineProcess:
    """Get or create the global engine process instance."""
    global _engine_process
    if _engine_process is None:
        _engine_process = EngineProcess()
        _engine_process.start()
    return _engine_process

def cleanup_engine() -> None:
    """Clean up the engine subprocess on shutdown."""
    global _engine_process
    if _engine_process:
        _engine_process.stop()
        _engine_process = None