"""MoVMM-Engine Phase 4 - Backend Configuration"""

import os
import json
import subprocess
import threading
import queue
from pathlib import Path
from typing import Optional, Dict

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

class EngineProcess:
    """Manages a persistent C++ engine subprocess with a dedicated stdout reader."""

    def __init__(self, cli_path: str = DEFAULT_ENGINE_CLI):
        self.cli_path = Path(cli_path).resolve()
        self.process: Optional[subprocess.Popen] = None
        self.request_counter = 0
        self._response_queue: queue.Queue = queue.Queue()
        self._reader_thread: Optional[threading.Thread] = None
        self._command_lock = threading.Lock()

    def start(self) -> bool:
        with self._command_lock:
            if self.is_running():
                return True
            if not self.cli_path.is_file():
                raise FileNotFoundError(f"Engine CLI not found: {self.cli_path}")

            self.process = subprocess.Popen(
                [str(self.cli_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                encoding="utf-8",
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
            # Inherit stderr so diagnostics stay visible without filling a pipe.
            self._response_queue = queue.Queue()
            self._reader_thread = threading.Thread(
                target=self._read_stdout_loop,
                args=(self.process.stdout, self._response_queue),
                daemon=True,
            )
            self._reader_thread.start()
            return True

    @staticmethod
    def _read_stdout_loop(stdout, responses) -> None:
        try:
            for line in stdout:
                if line.strip():
                    responses.put(line)
        except (OSError, UnicodeError) as exc:
            responses.put(exc)
        finally:
            responses.put(None)

    def stop(self) -> None:
        with self._command_lock:
            if self.process is None:
                return
            # The CLI exits on stdin EOF.
            try:
                self.process.stdin.close()
                self.process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()
            self._reader_thread.join()
            self.process.stdout.close()
            self.process = None
            self._reader_thread = None

    def send_command(self, command: str, params: Optional[Dict] = None) -> Dict:
        with self._command_lock:
            if not self.is_running():
                return {"error": "Engine subprocess not running"}

            self.request_counter += 1
            request_id = str(self.request_counter)
            request = {"id": request_id, "command": command}
            if params is not None:
                request["params"] = params

            try:
                self.process.stdin.write(json.dumps(request, allow_nan=False) + "\n")
                self.process.stdin.flush()
                line = self._response_queue.get(timeout=5)
                if line is None:
                    return {"error": "Engine subprocess closed stdout"}
                if isinstance(line, Exception):
                    return {"error": f"Failed to read engine response: {line}"}

                response = json.loads(line)
                if not isinstance(response, dict) or response.get("id") != request_id:
                    return {"error": "Engine response ID did not match request"}
                return response
            except queue.Empty:
                # The command's outcome is unknown. Stop accepting commands until
                # shutdown so a late reply cannot be mistaken for a later response.
                self.process.kill()
                self.process.wait()
                return {"error": "Timeout waiting for engine response"}
            except (OSError, ValueError) as exc:
                return {"error": f"Engine communication failed: {exc}"}

    def is_running(self) -> bool:
        """Check if the engine subprocess is still running."""
        return self.process is not None and self.process.poll() is None


# Global engine process instance
_engine_process: Optional[EngineProcess] = None

def get_engine_process() -> EngineProcess:
    """Get or create the global engine process instance."""
    global _engine_process
    if _engine_process is None:
        process = EngineProcess()
        process.start()
        _engine_process = process
    return _engine_process

def cleanup_engine() -> None:
    """Clean up the engine subprocess on shutdown."""
    global _engine_process
    if _engine_process:
        _engine_process.stop()
        _engine_process = None