# SPDX-License-Identifier: GPL-3.0-or-later
"""
HelixPrint - Moonraker component for handling modified G-code files.

This component provides a single API endpoint that handles the complete workflow
for printing modified G-code while preserving original file attribution in
Klipper's print_stats and Moonraker's history.

API v2.0: Path-based interface
- Client uploads modified file first via standard Moonraker file upload
- Then calls print_modified with path to the already-uploaded file
- This avoids memory-intensive JSON payloads for large G-code files

Key features:
- Single API endpoint: POST /server/helix/print_modified
- Path-based interface (receives file path, not content)
- Symlink-based filename preservation (Klipper sees original name)
- Automatic history patching to record original filename
- Configurable cleanup of temporary files

Configuration (moonraker.conf):
    [helix_print]
    enabled: True
    temp_dir: .helix_temp
    symlink_dir: .helix_print
    cleanup_delay: 86400
"""

from __future__ import annotations

import asyncio
import glob as glob_module
import json
import logging
import os
import shutil
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

if TYPE_CHECKING:
    from moonraker.common import RequestType, WebRequest
    from moonraker.confighelper import ConfigHelper
    from moonraker.server import Server

# Database table name for tracking temp files
HELIX_TEMP_TABLE = "helix_temp_files"

# Maximum age for cleaned database records before deletion (30 days)
DB_RECORD_MAX_AGE = 30 * 86400

# Plugin version - used for API version detection by clients
PLUGIN_VERSION = "1.0.0"

# Namespace for key-value storage fallback (Moonraker v0.8.x)
HELIX_NAMESPACE = "helix_temp_files"


class PrintInfo:
    """Tracks information about an active modified print."""

    def __init__(
        self,
        original_filename: str,
        temp_filename: str,
        symlink_filename: str,
        modifications: List[str],
        start_time: float,
    ) -> None:
        self.original_filename = original_filename
        self.temp_filename = temp_filename
        self.symlink_filename = symlink_filename
        self.modifications = modifications
        self.start_time = start_time
        self.job_id: Optional[str] = None
        self.db_id: Optional[int] = None


class HelixPrint:
    """
    Moonraker component for handling modified G-code files.

    Provides:
    - Single API endpoint for modified print workflow
    - Symlink-based filename preservation for print_stats
    - History patching to record original filename
    - Automatic cleanup of temp files
    """

    def __init__(self, config: ConfigHelper) -> None:
        self.server: Server = config.get_server()
        self.eventloop = self.server.get_event_loop()

        # Configuration options
        self.temp_dir = config.get("temp_dir", ".helix_temp")
        self.symlink_dir = config.get("symlink_dir", ".helix_print")
        self.cleanup_delay = config.getint("cleanup_delay", 86400)  # 24 hours
        self.enabled = config.getboolean("enabled", True)

        # Validate directory names don't contain path separators
        if "/" in self.temp_dir:
            raise config.error("temp_dir cannot contain path separators")
        if "/" in self.symlink_dir:
            raise config.error("symlink_dir cannot contain path separators")

        # Component references (resolved after init)
        self.file_manager: Optional[Any] = None
        self.history: Optional[Any] = None
        self.klippy: Optional[Any] = None
        self.database: Optional[Any] = None

        # State tracking
        self.active_prints: Dict[str, PrintInfo] = {}
        self.gc_path: Optional[Path] = None

        # Database backend flags (mutually exclusive, one will be True after init)
        self._use_sqlite = False      # Moonraker 0.9+ with execute_db_command
        self._use_namespace = False   # Moonraker 0.8.x with insert_item/get_item

        # Register API endpoints
        self.server.register_endpoint(
            "/server/helix/print_modified",
            ["POST"],
            self._handle_print_modified,
        )
        self.server.register_endpoint(
            "/server/helix/status",
            ["GET"],
            self._handle_status,
        )

        # Phase tracking endpoints
        self.server.register_endpoint(
            "/server/helix/phase_tracking/enable",
            ["POST"],
            self._handle_phase_tracking_enable,
        )
        self.server.register_endpoint(
            "/server/helix/phase_tracking/disable",
            ["POST"],
            self._handle_phase_tracking_disable,
        )
        self.server.register_endpoint(
            "/server/helix/phase_tracking/status",
            ["GET"],
            self._handle_phase_tracking_status,
        )

        # Register event handlers
        self.server.register_event_handler(
            "job_state:state_changed", self._on_job_state_changed
        )
        self.server.register_event_handler(
            "server:klippy_ready", self._on_klippy_ready
        )

        logging.info(
            f"HelixPrint v{PLUGIN_VERSION} initialized: temp={self.temp_dir}, "
            f"symlink={self.symlink_dir}, cleanup={self.cleanup_delay}s"
        )

    # =========================================================================
    # Input Validation
    # =========================================================================

    def _validate_filename(self, filename: str) -> None:
        """
        Validate filename for security issues.

        Raises server.error if validation fails.
        """
        if not filename:
            raise self.server.error("Filename cannot be empty", 400)

        # Check for null bytes and control characters
        if "\0" in filename or any(ord(c) < 32 for c in filename):
            raise self.server.error("Filename contains invalid characters", 400)

        # Check for absolute paths
        if filename.startswith("/"):
            raise self.server.error("Filename cannot be absolute path", 400)

        # Check for path traversal
        if ".." in filename:
            raise self.server.error("Filename cannot contain '..'", 400)

    def _validate_path_within_gcodes(self, path: Path) -> Path:
        """
        Resolve path and ensure it stays within gcodes directory.

        Returns resolved path if valid, raises server.error otherwise.
        """
        if self.gc_path is None:
            raise self.server.error("File manager not initialized", 500)

        # Resolve to absolute path (follows symlinks, resolves ..)
        try:
            resolved = path.resolve()
            gc_resolved = self.gc_path.resolve()
        except (OSError, RuntimeError) as e:
            raise self.server.error(f"Invalid path: {e}", 400)

        # Ensure resolved path is under gcodes directory
        try:
            resolved.relative_to(gc_resolved)
        except ValueError:
            raise self.server.error(
                "Path traversal detected: path escapes gcodes directory", 400
            )

        return resolved

    def _escape_gcode_string(self, s: str) -> str:
        """Escape a string for use in G-code commands."""
        # Remove any quotes that could break the command
        return s.replace('"', "").replace("'", "")

    # =========================================================================
    # Component Lifecycle
    # =========================================================================

    async def component_init(self) -> None:
        """Called after all components are loaded."""
        self.file_manager = self.server.lookup_component("file_manager")
        self.history = self.server.lookup_component("history", None)
        self.klippy = self.server.lookup_component("klippy_connection")
        self.database = self.server.lookup_component("database")

        # Get gcodes path
        self.gc_path = Path(self.file_manager.get_directory("gcodes"))

        # Ensure directories exist
        await self._ensure_directories()

        # Initialize database table
        await self._init_database()

        # Schedule startup cleanup
        self.eventloop.register_callback(self._startup_cleanup)

    async def _ensure_directories(self) -> None:
        """Ensure temp and symlink directories exist."""
        if self.gc_path is None:
            return

        temp_path = self.gc_path / self.temp_dir
        symlink_path = self.gc_path / self.symlink_dir

        temp_path.mkdir(parents=True, exist_ok=True)
        symlink_path.mkdir(parents=True, exist_ok=True)

        logging.debug(
            f"HelixPrint: Ensured directories exist: {temp_path}, {symlink_path}"
        )

    async def _init_database(self) -> None:
        """Initialize database for tracking temp files.

        Tries SQLite API first (Moonraker 0.9+), then falls back to
        namespace key-value API (Moonraker 0.8.x).
        """
        if self.database is None:
            logging.warning(
                "HelixPrint: Database not available, persistence disabled"
            )
            return

        # Try SQLite API first (Moonraker 0.9+)
        if hasattr(self.database, "execute_db_command"):
            try:
                await self.database.execute_db_command(
                    f"""
                    CREATE TABLE IF NOT EXISTS {HELIX_TEMP_TABLE} (
                        id INTEGER PRIMARY KEY AUTOINCREMENT,
                        original_filename TEXT NOT NULL,
                        temp_filename TEXT NOT NULL,
                        symlink_filename TEXT NOT NULL,
                        modifications TEXT,
                        job_id TEXT,
                        created_at REAL NOT NULL,
                        cleanup_scheduled_at REAL,
                        status TEXT DEFAULT 'active'
                    )
                    """
                )
                self._use_sqlite = True
                logging.info("HelixPrint: Using SQLite database for persistence")
                return
            except Exception as e:
                logging.warning(f"HelixPrint: SQLite init failed: {e}, trying namespace API")

        # Fall back to namespace API (Moonraker 0.8.x)
        if hasattr(self.database, "insert_item"):
            self._use_namespace = True
            logging.info("HelixPrint: Using namespace API for persistence (Moonraker 0.8.x)")
            return

        logging.warning("HelixPrint: No compatible database API found, persistence disabled")

    # =========================================================================
    # API Handlers
    # =========================================================================

    async def _handle_status(self, web_request: WebRequest) -> Dict[str, Any]:
        """Handle status request - useful for plugin detection and version checking."""
        return {
            "enabled": self.enabled,
            "temp_dir": self.temp_dir,
            "symlink_dir": self.symlink_dir,
            "cleanup_delay": self.cleanup_delay,
            "active_prints": len(self.active_prints),
            "version": PLUGIN_VERSION,
        }

    async def _handle_print_modified(
        self, web_request: WebRequest
    ) -> Dict[str, Any]:
        """
        Handle the print_modified API request (v2.0 - path-based).

        This is the main entry point for printing modified G-code files.
        The client must upload the modified file first via standard Moonraker
        file upload, then call this endpoint with the path.

        Workflow:
        1. Client uploads modified file to .helix_temp/ via /server/files/upload
        2. Client calls this endpoint with temp_file_path
        3. Plugin validates paths, copies metadata, creates symlink
        4. Plugin starts print via symlink

        Parameters:
            original_filename: Path to the original G-code file (for history)
            temp_file_path: Path to the already-uploaded modified file
            modifications: List of modification identifiers for tracking
            copy_metadata: Whether to copy thumbnails from original (default: True)
        """
        if not self.enabled:
            raise self.server.error("HelixPrint component is disabled", 503)

        if self.gc_path is None:
            raise self.server.error("File manager not initialized", 500)

        # Get and validate parameters
        original_filename = web_request.get_str("original_filename")
        temp_file_path = web_request.get_str("temp_file_path")
        modifications = web_request.get_list("modifications", [])
        copy_metadata = web_request.get_boolean("copy_metadata", True)

        # Security validations
        self._validate_filename(original_filename)
        self._validate_filename(temp_file_path)

        # Validate original file exists and is within gcodes
        original_path = self.gc_path / original_filename
        original_resolved = self._validate_path_within_gcodes(original_path)

        if not original_resolved.exists():
            raise self.server.error(
                f"Original file not found: {original_filename}", 400
            )

        # Don't allow following symlinks for the original file
        if original_path.is_symlink():
            raise self.server.error(
                "Original file cannot be a symlink", 400
            )

        # Validate temp file exists and is within gcodes
        temp_path = self.gc_path / temp_file_path
        temp_resolved = self._validate_path_within_gcodes(temp_path)

        if not temp_resolved.exists():
            raise self.server.error(
                f"Temp file not found: {temp_file_path}. "
                "Upload the modified file first via /server/files/upload", 400
            )

        # Use the provided temp path (client already uploaded it)
        temp_filename = temp_file_path
        logging.info(f"HelixPrint: Using uploaded temp file {temp_filename}")

        # Copy metadata (thumbnails) from original
        if copy_metadata:
            await self._copy_metadata(original_resolved, temp_resolved)

        # Extract base name from original for symlink
        base_name = Path(original_filename).name

        # Create symlink with original filename
        symlink_filename = f"{self.symlink_dir}/{base_name}"
        symlink_path = self.gc_path / symlink_filename

        # Validate symlink path
        self._validate_path_within_gcodes(symlink_path.parent)

        symlink_path.parent.mkdir(parents=True, exist_ok=True)

        # Create symlink atomically (handles race condition)
        try:
            self._create_symlink_atomic(symlink_path, temp_path)
            logging.info(
                f"HelixPrint: Created symlink {symlink_filename} -> {temp_filename}"
            )
        except Exception as e:
            # Clean up temp file on symlink failure
            temp_path.unlink(missing_ok=True)
            raise self.server.error(f"Failed to create symlink: {e}", 500)

        # Track this print
        print_info = PrintInfo(
            original_filename=original_filename,
            temp_filename=temp_filename,
            symlink_filename=symlink_filename,
            modifications=modifications,
            start_time=time.time(),
        )
        self.active_prints[symlink_filename] = print_info

        # Persist to database for crash recovery
        await self._persist_print_info(print_info)

        # Start the print with symlink path (escape filename for G-code)
        safe_symlink = self._escape_gcode_string(symlink_filename)
        try:
            await self.klippy.run_gcode(
                f'SDCARD_PRINT_FILE FILENAME="{safe_symlink}"'
            )
            logging.info(f"HelixPrint: Started print with {symlink_filename}")
        except Exception as e:
            # Clean up on print start failure
            symlink_path.unlink(missing_ok=True)
            temp_path.unlink(missing_ok=True)
            del self.active_prints[symlink_filename]
            raise self.server.error(f"Failed to start print: {e}", 500)

        return {
            "original_filename": original_filename,
            "print_filename": symlink_filename,
            "temp_filename": temp_filename,
            "status": "printing",
        }

    def _create_symlink_atomic(self, symlink_path: Path, target_path: Path) -> None:
        """
        Create symlink atomically, handling existing files.

        Uses try/except pattern to avoid TOCTOU race conditions.
        """
        try:
            symlink_path.symlink_to(target_path)
        except FileExistsError:
            # Remove existing and retry
            if symlink_path.is_symlink() or symlink_path.exists():
                symlink_path.unlink()
            symlink_path.symlink_to(target_path)

    # =========================================================================
    # Metadata Handling
    # =========================================================================

    async def _copy_metadata(
        self, original_path: Path, temp_path: Path
    ) -> None:
        """Copy slicer metadata (thumbnails) from original to temp file."""
        if self.gc_path is None:
            return

        thumbs_dir = self.gc_path / ".thumbs"
        if not thumbs_dir.exists():
            return

        original_stem = original_path.stem
        temp_stem = temp_path.stem

        # Escape glob special characters in the stem
        escaped_stem = glob_module.escape(original_stem)

        # Find and link thumbnails for the original file
        for thumb in thumbs_dir.glob(f"{escaped_stem}*"):
            try:
                # Create symlink to original thumbnail with new name
                new_name = thumb.name.replace(original_stem, temp_stem)
                temp_thumb = thumbs_dir / new_name
                if not temp_thumb.exists():
                    temp_thumb.symlink_to(thumb)
                    logging.debug(
                        f"HelixPrint: Linked thumbnail {new_name} -> {thumb.name}"
                    )
            except Exception as e:
                logging.warning(f"HelixPrint: Failed to link thumbnail: {e}")

    # =========================================================================
    # Database Operations
    # =========================================================================

    async def _persist_print_info(self, print_info: PrintInfo) -> None:
        """Save print info to database for crash recovery."""
        record = {
            "original_filename": print_info.original_filename,
            "temp_filename": print_info.temp_filename,
            "symlink_filename": print_info.symlink_filename,
            "modifications": print_info.modifications,
            "created_at": time.time(),
            "cleanup_scheduled_at": None,
            "status": "active",
        }

        try:
            if self._use_sqlite:
                result = await self.database.execute_db_command(
                    f"""
                    INSERT INTO {HELIX_TEMP_TABLE}
                    (original_filename, temp_filename, symlink_filename,
                     modifications, created_at, status)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (
                        record["original_filename"],
                        record["temp_filename"],
                        record["symlink_filename"],
                        json.dumps(record["modifications"]),
                        record["created_at"],
                        record["status"],
                    ),
                )
                print_info.db_id = result.lastrowid
            elif self._use_namespace:
                # Use temp_filename as key (unique per print)
                await self.database.insert_item(
                    HELIX_NAMESPACE,
                    print_info.temp_filename,
                    record,
                )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to persist print info: {e}")

    # =========================================================================
    # Event Handlers
    # =========================================================================

    async def _on_klippy_ready(self) -> None:
        """Handle Klipper ready event - recover from any interrupted prints."""
        logging.debug("HelixPrint: Klipper ready, checking for interrupted prints")
        # Recovery logic would go here if needed

    async def _on_job_state_changed(
        self,
        job_event: Any,
        prev_stats: Dict[str, Any],
        new_stats: Dict[str, Any],
    ) -> None:
        """Handle job state changes to patch history."""
        state = new_stats.get("state", "")
        filename = new_stats.get("filename", "")

        # Check if this is one of our modified prints
        if not filename.startswith(f"{self.symlink_dir}/"):
            return

        print_info = self.active_prints.get(filename)
        if not print_info:
            logging.warning(f"HelixPrint: Unknown modified file: {filename}")
            return

        # Capture job_id when print starts
        if state == "printing":
            job_id = new_stats.get("job_id")
            if job_id:
                print_info.job_id = job_id
                logging.info(f"HelixPrint: Job started with ID {job_id}")

        # Handle completion states
        if state in ("complete", "cancelled", "error"):
            logging.info(f"HelixPrint: Job finished ({state}): {filename}")

            # Patch history entry
            if self.history is not None:
                await self._patch_history_entry(print_info, state)

            # Schedule cleanup
            await self._schedule_cleanup(print_info)

            # Remove from active tracking
            del self.active_prints[filename]

    async def _patch_history_entry(
        self, print_info: PrintInfo, final_state: str
    ) -> None:
        """Patch the history entry to show original filename."""
        if not self.history or not print_info.job_id:
            return

        # Check if history API is compatible
        if not hasattr(self.history, "get_job") or not hasattr(
            self.history, "modify_job"
        ):
            logging.warning(
                "HelixPrint: History API not compatible "
                "(missing get_job or modify_job)"
            )
            return

        try:
            # Get the job from history
            job = await self.history.get_job(print_info.job_id)
            if not job:
                logging.warning(
                    f"HelixPrint: Job {print_info.job_id} not in history"
                )
                return

            # Extract original filename (strip symlink dir prefix if present)
            original = print_info.original_filename
            if original.startswith(f"{self.symlink_dir}/"):
                original = original[len(self.symlink_dir) + 1 :]

            # Update auxiliary_data with modification info
            aux_data = job.get("auxiliary_data", {}) or {}
            aux_data["helix_modifications"] = print_info.modifications
            aux_data["helix_temp_file"] = print_info.temp_filename
            aux_data["helix_symlink"] = print_info.symlink_filename
            aux_data["helix_original"] = print_info.original_filename

            # Update the history entry
            await self.history.modify_job(
                print_info.job_id,
                filename=original,
                auxiliary_data=aux_data,
            )

            logging.info(
                f"HelixPrint: Patched history {print_info.job_id} "
                f"filename to '{original}'"
            )

        except Exception as e:
            logging.exception(f"HelixPrint: Failed to patch history: {e}")

    # =========================================================================
    # Cleanup Operations
    # =========================================================================

    async def _schedule_cleanup(self, print_info: PrintInfo) -> None:
        """Schedule cleanup of temp files after delay."""
        if self.gc_path is None:
            return

        # Immediately delete symlink (no longer needed)
        symlink_path = self.gc_path / print_info.symlink_filename
        if symlink_path.is_symlink():
            symlink_path.unlink()
            logging.debug(f"HelixPrint: Removed symlink {symlink_path}")

        # Also clean up thumbnail symlinks
        await self._cleanup_thumbnail_symlinks(print_info.temp_filename)

        # Update database status
        cleanup_time = time.time() + self.cleanup_delay
        try:
            if self._use_sqlite:
                await self.database.execute_db_command(
                    f"""
                    UPDATE {HELIX_TEMP_TABLE}
                    SET cleanup_scheduled_at = ?, status = ?
                    WHERE temp_filename = ?
                    """,
                    (cleanup_time, "pending_cleanup", print_info.temp_filename),
                )
            elif self._use_namespace:
                # Update record in namespace storage
                record = await self.database.get_item(
                    HELIX_NAMESPACE, print_info.temp_filename
                )
                if record:
                    record["cleanup_scheduled_at"] = cleanup_time
                    record["status"] = "pending_cleanup"
                    await self.database.update_item(
                        HELIX_NAMESPACE, print_info.temp_filename, record
                    )
                else:
                    logging.warning(
                        f"HelixPrint: Record not found for cleanup scheduling: "
                        f"{print_info.temp_filename}"
                    )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to update cleanup status: {e}")

        # Schedule delayed cleanup
        self.eventloop.delay_callback(
            self.cleanup_delay,
            self._cleanup_temp_file,
            print_info.temp_filename,
        )

        logging.info(
            f"HelixPrint: Scheduled cleanup of {print_info.temp_filename} "
            f"in {self.cleanup_delay}s"
        )

    async def _cleanup_thumbnail_symlinks(self, temp_filename: str) -> None:
        """Clean up thumbnail symlinks for a temp file."""
        if self.gc_path is None:
            return

        thumbs_dir = self.gc_path / ".thumbs"
        if not thumbs_dir.exists():
            return

        temp_stem = Path(temp_filename).stem

        # Escape glob special characters
        escaped_stem = glob_module.escape(temp_stem)

        for thumb in thumbs_dir.glob(f"{escaped_stem}*"):
            if thumb.is_symlink():
                thumb.unlink()
                logging.debug(f"HelixPrint: Removed thumbnail symlink {thumb}")

    async def _cleanup_temp_file(self, temp_filename: str) -> None:
        """Delete a temp file after cleanup delay."""
        if self.gc_path is None:
            return

        temp_path = self.gc_path / temp_filename
        file_deleted = False
        try:
            if temp_path.exists():
                temp_path.unlink()
                file_deleted = True
                logging.info(f"HelixPrint: Cleaned up {temp_filename}")
            else:
                # File already gone (manual deletion or previous cleanup)
                file_deleted = True
        except OSError as e:
            logging.error(f"HelixPrint: Failed to delete {temp_filename}: {e}")
            return  # Don't mark as cleaned if file delete failed

        # Only update database if file was actually deleted
        if not file_deleted:
            return

        try:
            if self._use_sqlite:
                await self.database.execute_db_command(
                    f"""
                    UPDATE {HELIX_TEMP_TABLE}
                    SET status = ?
                    WHERE temp_filename = ?
                    """,
                    ("cleaned", temp_filename),
                )
            elif self._use_namespace:
                record = await self.database.get_item(HELIX_NAMESPACE, temp_filename)
                if record:
                    record["status"] = "cleaned"
                    await self.database.update_item(
                        HELIX_NAMESPACE, temp_filename, record
                    )
                else:
                    logging.debug(
                        f"HelixPrint: No record to update for cleaned file: {temp_filename}"
                    )
        except Exception as e:
            logging.warning(f"HelixPrint: Failed to update cleanup status: {e}")

    async def _startup_cleanup(self) -> None:
        """Clean up stale temp files on startup."""
        if self.gc_path is None:
            return
        if not self._use_sqlite and not self._use_namespace:
            return

        now = time.time()

        try:
            # Get pending cleanup records
            pending_records: List[Dict[str, Any]] = []

            if self._use_sqlite:
                rows = await self.database.execute_db_command(
                    f"""
                    SELECT temp_filename, symlink_filename
                    FROM {HELIX_TEMP_TABLE}
                    WHERE status = 'pending_cleanup' AND cleanup_scheduled_at < ?
                    """,
                    (now,),
                )
                if rows:
                    pending_records = [dict(r) for r in rows]

            elif self._use_namespace:
                # Get all items and filter in Python
                try:
                    all_items = await self.database.ns_items(HELIX_NAMESPACE)
                except self.server.error:
                    # Namespace doesn't exist yet (no records inserted)
                    all_items = []
                for key, record in all_items:
                    if (
                        record.get("status") == "pending_cleanup"
                        and record.get("cleanup_scheduled_at")
                        and record["cleanup_scheduled_at"] < now
                    ):
                        pending_records.append(record)

            # Clean up each pending file
            cleaned_count = 0
            for record in pending_records:
                temp_filename = record["temp_filename"]
                symlink_filename = record["symlink_filename"]

                # Clean up files
                temp_path = self.gc_path / temp_filename
                symlink_path = self.gc_path / symlink_filename

                if temp_path.exists():
                    temp_path.unlink()
                if symlink_path.is_symlink():
                    symlink_path.unlink()

                # Clean up thumbnail symlinks
                await self._cleanup_thumbnail_symlinks(temp_filename)

                # Update status
                if self._use_sqlite:
                    await self.database.execute_db_command(
                        f"""
                        UPDATE {HELIX_TEMP_TABLE}
                        SET status = ?
                        WHERE temp_filename = ?
                        """,
                        ("cleaned", temp_filename),
                    )
                elif self._use_namespace:
                    record["status"] = "cleaned"
                    await self.database.update_item(
                        HELIX_NAMESPACE, temp_filename, record
                    )
                cleaned_count += 1

            if cleaned_count > 0:
                logging.info(
                    f"HelixPrint: Startup cleanup removed {cleaned_count} stale files"
                )

            # Purge old database records to prevent unbounded growth
            purge_cutoff = now - DB_RECORD_MAX_AGE
            purged_count = 0

            if self._use_sqlite:
                deleted = await self.database.execute_db_command(
                    f"""
                    DELETE FROM {HELIX_TEMP_TABLE}
                    WHERE status = 'cleaned' AND created_at < ?
                    """,
                    (purge_cutoff,),
                )
                if deleted and deleted.rowcount > 0:
                    purged_count = deleted.rowcount

            elif self._use_namespace:
                # Get all items and delete old ones
                try:
                    all_items = await self.database.ns_items(HELIX_NAMESPACE)
                except self.server.error:
                    # Namespace doesn't exist yet
                    all_items = []
                for key, record in all_items:
                    if (
                        record.get("status") == "cleaned"
                        and record.get("created_at")
                        and record["created_at"] < purge_cutoff
                    ):
                        await self.database.delete_item(HELIX_NAMESPACE, key)
                        purged_count += 1

            if purged_count > 0:
                logging.info(
                    f"HelixPrint: Purged {purged_count} old database records"
                )

        except Exception as e:
            logging.exception(f"HelixPrint: Startup cleanup failed: {e}")

    # =========================================================================
    # Phase Tracking API
    # =========================================================================

    # Markers used to identify injected tracking code
    # v2 uses HELIX_PHASE_* macros from helix_macros.cfg
    TRACKING_MARKER_BEGIN = "# <<< HELIX_TRACKING v2 >>>"
    TRACKING_MARKER_END = "# <<< /HELIX_TRACKING >>>"

    # Operations to detect and their corresponding HELIX_PHASE_* macro names
    PHASE_PATTERNS = [
        (r"\bG28\b", "HELIX_PHASE_HOMING"),
        (r"\bQUAD_GANTRY_LEVEL\b", "HELIX_PHASE_QGL"),
        (r"\bZ_TILT_ADJUST\b", "HELIX_PHASE_Z_TILT"),
        (r"\bBED_MESH_CALIBRATE\b", "HELIX_PHASE_BED_MESH"),
        (r"\b(CLEAN|WIPE)_NOZZLE\b", "HELIX_PHASE_CLEANING"),
        (r"\b\w*PURGE\w*\b", "HELIX_PHASE_PURGING"),
        (r"\bM109\b", "HELIX_PHASE_HEATING_NOZZLE"),
        (r"\bM190\b", "HELIX_PHASE_HEATING_BED"),
    ]

    async def _handle_phase_tracking_enable(
        self, web_request: WebRequest
    ) -> Dict[str, Any]:
        """
        Enable phase tracking by instrumenting the PRINT_START macro.

        POST /server/helix/phase_tracking/enable
        """
        import re

        try:
            # Get the PRINT_START macro definition
            macro_name, gcode = await self._get_print_start_macro()
            if not gcode:
                return {
                    "success": False,
                    "error": "PRINT_START macro not found",
                    "macro_name": macro_name,
                }

            # Check if already instrumented
            if self.TRACKING_MARKER_BEGIN in gcode:
                return {
                    "success": True,
                    "already_instrumented": True,
                    "macro_name": macro_name,
                }

            # Instrument the macro
            instrumented = self._instrument_gcode(gcode)

            # Write the modified macro back
            success = await self._update_macro(macro_name, instrumented)

            # Trigger Klipper restart to load the modified config
            klipper_restarted = False
            if success:
                try:
                    kc: KlippyConnection = self.server.lookup_component("klippy_connection")
                    await kc.request("printer/restart", {})
                    klipper_restarted = True
                    logging.info("HelixPrint: Triggered Klipper restart after enabling phase tracking")
                except Exception as e:
                    logging.warning(f"HelixPrint: Could not restart Klipper: {e}")

            return {
                "success": success,
                "macro_name": macro_name,
                "instrumented": success,
                "klipper_restarted": klipper_restarted,
            }

        except Exception as e:
            logging.exception(f"HelixPrint: Phase tracking enable failed: {e}")
            return {"success": False, "error": str(e)}

    async def _handle_phase_tracking_disable(
        self, web_request: WebRequest
    ) -> Dict[str, Any]:
        """
        Disable phase tracking by removing instrumentation from PRINT_START.

        POST /server/helix/phase_tracking/disable
        """
        try:
            # Get the PRINT_START macro definition
            macro_name, gcode = await self._get_print_start_macro()
            if not gcode:
                return {
                    "success": False,
                    "error": "PRINT_START macro not found",
                    "macro_name": macro_name,
                }

            # Check if instrumented
            if self.TRACKING_MARKER_BEGIN not in gcode:
                return {
                    "success": True,
                    "was_instrumented": False,
                    "macro_name": macro_name,
                }

            # Strip instrumentation
            stripped = self._strip_instrumentation(gcode)

            # Write the modified macro back
            success = await self._update_macro(macro_name, stripped)

            # Trigger Klipper restart to load the modified config
            klipper_restarted = False
            if success:
                try:
                    kc: KlippyConnection = self.server.lookup_component("klippy_connection")
                    await kc.request("printer/restart", {})
                    klipper_restarted = True
                    logging.info("HelixPrint: Triggered Klipper restart after disabling phase tracking")
                except Exception as e:
                    logging.warning(f"HelixPrint: Could not restart Klipper: {e}")

            return {
                "success": success,
                "macro_name": macro_name,
                "was_instrumented": True,
                "klipper_restarted": klipper_restarted,
            }

        except Exception as e:
            logging.exception(f"HelixPrint: Phase tracking disable failed: {e}")
            return {"success": False, "error": str(e)}

    async def _handle_phase_tracking_status(
        self, web_request: WebRequest
    ) -> Dict[str, Any]:
        """
        Get phase tracking status.

        GET /server/helix/phase_tracking/status
        """
        try:
            macro_name, gcode = await self._get_print_start_macro()
            # Use bool() to ensure we return False (not None) when gcode is None
            instrumented = bool(gcode and self.TRACKING_MARKER_BEGIN in gcode)

            return {
                "enabled": instrumented,
                "instrumented": instrumented,
                "macro_name": macro_name,
                "version": "v2" if instrumented else None,
            }

        except Exception as e:
            logging.exception(f"HelixPrint: Phase tracking status failed: {e}")
            return {"enabled": False, "error": str(e)}

    async def _get_print_start_macro(self) -> tuple:
        """
        Get the PRINT_START macro definition from Klipper.

        Returns (macro_name, gcode) tuple. Returns (name, None) if not found.
        """
        if not self.klippy:
            return (None, None)

        # Try common macro names
        macro_names = ["PRINT_START", "START_PRINT", "_PRINT_START"]

        for name in macro_names:
            try:
                result = await self.klippy.request(
                    "gcode_macro_variable",
                    {"macro": name},
                )
                if result and "gcode" in result:
                    return (name, result["gcode"])
            except Exception:
                continue

        # Not found via API, try reading from config files
        config_dir = await self._get_config_dir()
        if config_dir:
            for name in macro_names:
                gcode = self._read_macro_from_config(config_dir, name)
                if gcode:
                    return (name, gcode)

        return (macro_names[0], None)

    def _read_macro_from_config(
        self, config_dir: Path, macro_name: str
    ) -> Optional[str]:
        """Read a macro definition from Klipper config files."""
        import re

        # Search all .cfg files
        for cfg_file in config_dir.glob("**/*.cfg"):
            try:
                content = cfg_file.read_text()

                # Find the macro section
                pattern = rf"\[gcode_macro\s+{macro_name}\]"
                match = re.search(pattern, content, re.IGNORECASE)
                if not match:
                    continue

                # Extract the gcode block
                section_start = match.end()
                lines = []
                in_gcode = False

                for line in content[section_start:].split("\n"):
                    # Check for next section
                    if line.startswith("[") and not line.startswith("[gcode_macro"):
                        break
                    if re.match(r"^\[gcode_macro", line, re.IGNORECASE):
                        break

                    # Check for gcode: line
                    if line.strip().startswith("gcode:"):
                        in_gcode = True
                        # Get content after gcode: on same line
                        after_gcode = line.split("gcode:", 1)[1].strip()
                        if after_gcode:
                            lines.append(after_gcode)
                        continue

                    # Collect gcode lines (indented continuation)
                    if in_gcode:
                        if line and not line[0].isspace() and line.strip():
                            # End of gcode block
                            break
                        lines.append(line)

                if lines:
                    return "\n".join(lines)

            except Exception as e:
                logging.debug(f"Error reading {cfg_file}: {e}")
                continue

        return None

    async def _get_config_dir(self) -> Optional[Path]:
        """Get the Klipper config directory."""
        # Common locations
        locations = [
            Path.home() / "printer_data" / "config",
            Path.home() / "klipper_config",
            Path("/home/pi/printer_data/config"),
            Path("/home/pi/klipper_config"),
        ]

        for loc in locations:
            if loc.exists() and (loc / "printer.cfg").exists():
                return loc

        return None

    def _instrument_gcode(self, gcode: str) -> str:
        """
        Inject phase tracking code into gcode.

        Uses HELIX_PHASE_* macros from helix_macros.cfg which emit
        RESPOND messages that HelixScreen can parse.

        Requires helix_macros.cfg to be installed on the printer.
        """
        import re

        lines = gcode.split("\n")
        result = []

        # Note: No "STARTING" marker needed - phases are detected as they occur

        for line in lines:
            result.append(line)

            # Check if this line matches any phase pattern
            line_upper = line.upper().strip()
            if not line_upper or line_upper.startswith("#"):
                continue

            for pattern, macro_name in self.PHASE_PATTERNS:
                if re.search(pattern, line_upper, re.IGNORECASE):
                    result.append(self.TRACKING_MARKER_BEGIN)
                    result.append(macro_name)
                    result.append(self.TRACKING_MARKER_END)
                    break  # Only one marker per line

        # Add HELIX_READY at the end to signal preparation complete
        result.append(self.TRACKING_MARKER_BEGIN)
        result.append("HELIX_READY")
        result.append(self.TRACKING_MARKER_END)

        return "\n".join(result)

    def _strip_instrumentation(self, gcode: str) -> str:
        """Remove phase tracking code from gcode."""
        lines = gcode.split("\n")
        result = []
        skip = False

        for line in lines:
            if self.TRACKING_MARKER_BEGIN in line:
                skip = True
                continue
            if self.TRACKING_MARKER_END in line:
                skip = False
                continue
            if not skip:
                result.append(line)

        return "\n".join(result)

    async def _update_macro(self, macro_name: str, gcode: str) -> bool:
        """
        Update a macro definition in the config file.

        This is a simplified implementation that updates printer.cfg.
        A production version would need to handle includes properly.
        """
        import re

        config_dir = await self._get_config_dir()
        if not config_dir:
            logging.error("HelixPrint: Config directory not found")
            return False

        # Find the file containing the macro
        for cfg_file in config_dir.glob("**/*.cfg"):
            try:
                content = cfg_file.read_text()

                # Check if this file contains the macro
                pattern = rf"\[gcode_macro\s+{re.escape(macro_name)}\]"
                match = re.search(pattern, content, re.IGNORECASE)
                if not match:
                    continue

                # Found the file - update the gcode section
                # This is complex because we need to preserve the section structure

                # Create backup
                backup_path = cfg_file.with_suffix(
                    f".bak.{int(time.time())}"
                )
                shutil.copy(cfg_file, backup_path)
                logging.info(f"HelixPrint: Created backup: {backup_path}")

                # Find section boundaries
                section_start = match.start()
                section_end = len(content)

                # Find next section
                next_section = re.search(r"\n\[", content[match.end():])
                if next_section:
                    section_end = match.end() + next_section.start()

                # Extract the section
                section = content[section_start:section_end]

                # Find and replace gcode block within section
                gcode_match = re.search(
                    r"gcode:\s*\n((?:[ \t]+.*\n)*)",
                    section,
                    re.MULTILINE
                )

                if gcode_match:
                    # Preserve indentation
                    indent = "    "
                    indented_gcode = "\n".join(
                        indent + line if line.strip() else line
                        for line in gcode.split("\n")
                    )

                    new_section = (
                        section[:gcode_match.start(1)]
                        + indented_gcode
                        + "\n"
                        + section[gcode_match.end(1):]
                    )

                    new_content = (
                        content[:section_start]
                        + new_section
                        + content[section_end:]
                    )

                    cfg_file.write_text(new_content)
                    logging.info(
                        f"HelixPrint: Updated {macro_name} in {cfg_file}"
                    )
                    return True

            except Exception as e:
                logging.exception(f"Error updating {cfg_file}: {e}")
                continue

        logging.error(f"HelixPrint: Could not find {macro_name} in config files")
        return False


def load_component(config: ConfigHelper) -> HelixPrint:
    """Factory function to load the HelixPrint component."""
    return HelixPrint(config)
