# SPDX-License-Identifier: GPL-3.0-or-later
"""
Unit tests for the helix_print Moonraker plugin.

These tests verify the plugin's core functionality without requiring
a running Moonraker instance. They use mocks to simulate Moonraker's
server, file manager, and history components.

Run with: pytest tests/test_helix_print.py -v
"""

import asyncio
import json
import os
import tempfile
from pathlib import Path
from typing import Any, Dict, Optional
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

# Import the plugin (adjust path as needed)
import sys
sys.path.insert(0, str(Path(__file__).parent.parent))

from helix_print import HelixPrint, PrintInfo, load_component


# ============================================================================
# Test Fixtures and Mocks
# ============================================================================

class MockWebRequest:
    """Mock WebRequest for testing API endpoints."""

    def __init__(self, params: Dict[str, Any]):
        self._params = params

    def get_str(self, key: str, default: str = "") -> str:
        return str(self._params.get(key, default))

    def get_list(self, key: str, default: list = None) -> list:
        return self._params.get(key, default or [])

    def get_boolean(self, key: str, default: bool = False) -> bool:
        return bool(self._params.get(key, default))


class MockServer:
    """Mock Moonraker server for testing."""

    def __init__(self):
        self.endpoints = {}
        self.event_handlers = {}
        self.components = {}
        self._error_class = Exception

    def register_endpoint(self, path: str, methods: list, handler):
        self.endpoints[path] = handler

    def register_event_handler(self, event: str, callback):
        if event not in self.event_handlers:
            self.event_handlers[event] = []
        self.event_handlers[event].append(callback)

    def lookup_component(self, name: str, default=None):
        return self.components.get(name, default)

    def get_event_loop(self):
        return MockEventLoop()

    def error(self, message: str, code: int = 500):
        return Exception(f"{code}: {message}")


class MockEventLoop:
    """Mock event loop for testing."""

    def register_callback(self, callback, *args):
        pass

    def delay_callback(self, delay: float, callback, *args):
        pass


class MockFileManager:
    """Mock file manager for testing."""

    def __init__(self, gcodes_path: str):
        self._gcodes_path = gcodes_path

    def get_directory(self, name: str) -> str:
        if name == "gcodes":
            return self._gcodes_path
        return ""


class MockDatabase:
    """Mock database component for testing.

    Mirrors the real Moonraker v0.10.0 `database` component's public surface
    (verified from Moonraker source commit d5ee171): sql_execute, insert_item,
    get_item, update_item, delete_item, ns_items, ns_length. There is NO
    `execute_db_command` - that API was renamed/removed upstream. Only defining
    the real methods means calling anything else raises AttributeError, same as
    it would against the production object.
    """

    def __init__(self):
        self.namespaces: Dict[str, Dict[str, Any]] = {}
        self.sql_commands = []

    async def sql_execute(self, sql: str, params: Optional[list] = None):
        self.sql_commands.append((sql, params))
        return MagicMock(lastrowid=1, rowcount=0)

    async def insert_item(self, namespace: str, key: str, value: Any) -> None:
        self.namespaces.setdefault(namespace, {})[key] = value

    async def get_item(self, namespace: str, key: str, default: Any = None) -> Any:
        return self.namespaces.get(namespace, {}).get(key, default)

    async def update_item(self, namespace: str, key: str, value: Any) -> None:
        self.namespaces.setdefault(namespace, {})[key] = value

    async def delete_item(self, namespace: str, key: str) -> None:
        self.namespaces.get(namespace, {}).pop(key, None)

    async def ns_items(self, namespace: str) -> list:
        return list(self.namespaces.get(namespace, {}).items())

    async def ns_length(self, namespace: str) -> int:
        return len(self.namespaces.get(namespace, {}))


class MockKlippy:
    """Mock klippy_connection component for testing.

    Mirrors the real Moonraker v0.10.0 `KlippyConnection`'s public async surface:
    request(web_request) and rollover_log(). It has NO `run_gcode` - that method
    lives on klippy_apis (see MockKlippyApis below). Calling run_gcode on this
    mock raises AttributeError, reproducing the production crash reported in
    debug bundle RA6EPJTZ ("'KlippyConnection' object has no attribute
    'run_gcode'").
    """

    def __init__(self):
        self.requests_sent = []

    async def request(self, web_request):
        self.requests_sent.append(web_request)
        return {}

    async def rollover_log(self):
        pass


class MockKlippyApis:
    """Mock klippy_apis component for testing.

    Mirrors the real Moonraker v0.10.0 `KlippyAPI`'s public async surface
    (verified from Moonraker source commit d5ee171): run_gcode, start_print,
    do_restart, pause_print, resume_print, cancel_print, emergency_stop,
    query_objects, get_object_list, list_endpoints, subscribe_objects,
    get_klippy_info. This is the correct component for Klipper interaction
    (SDCARD_PRINT_FILE, RESTART, etc.) - not klippy_connection.
    """

    def __init__(self):
        self.run_gcode = AsyncMock(return_value="ok")
        self.start_print = AsyncMock(return_value=None)
        self.do_restart = AsyncMock(return_value=None)
        self.pause_print = AsyncMock(return_value=None)
        self.resume_print = AsyncMock(return_value=None)
        self.cancel_print = AsyncMock(return_value=None)
        self.emergency_stop = AsyncMock(return_value=None)
        self.query_objects = AsyncMock(return_value={})
        self.get_object_list = AsyncMock(return_value=[])
        self.list_endpoints = AsyncMock(return_value={})
        self.subscribe_objects = AsyncMock(return_value={})
        self.get_klippy_info = AsyncMock(return_value={})


class MockHistory:
    """Mock history component for testing.

    Mirrors the real Moonraker v0.10.0 `history` component: get_job(job_id) and
    save_job(job, job_id). There is NO `modify_job` - that method does not exist
    on modern Moonraker. HelixPrint's history filename-patching feature degrades
    safely (best-effort) when it isn't available; see
    HelixPrint._patch_history_entry.
    """

    def __init__(self):
        self.jobs = {}
        self.save_job_calls = []

    async def get_job(self, job_id: str):
        return self.jobs.get(job_id)

    async def save_job(self, job, job_id=None):
        self.save_job_calls.append((job, job_id))


class MockConfigHelper:
    """Mock config helper for testing."""

    def __init__(self, server: MockServer, options: Dict[str, Any] = None):
        self._server = server
        self._options = options or {}

    def get_server(self):
        return self._server

    def get(self, key: str, default: str = None) -> str:
        return self._options.get(key, default)

    def getint(self, key: str, default: int = None) -> int:
        return int(self._options.get(key, default))

    def getboolean(self, key: str, default: bool = None) -> bool:
        return bool(self._options.get(key, default))


@pytest.fixture
def temp_gcodes_dir():
    """Create a temporary directory for G-code files."""
    with tempfile.TemporaryDirectory() as tmpdir:
        yield tmpdir


@pytest.fixture
def mock_server():
    """Create a mock Moonraker server."""
    return MockServer()


@pytest.fixture
def helix_print_component(mock_server, temp_gcodes_dir):
    """Create a HelixPrint component instance for testing."""
    # Set up mock components
    mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
    mock_server.components["database"] = MockDatabase()
    mock_server.components["klippy_connection"] = MockKlippy()
    mock_server.components["klippy_apis"] = MockKlippyApis()
    mock_server.components["history"] = MockHistory()

    # Create config
    config = MockConfigHelper(mock_server, {
        "temp_dir": ".helix_temp",
        "symlink_dir": ".helix_print",
        "cleanup_delay": 3600,
        "enabled": True,
    })

    # Create component
    component = load_component(config)
    return component


# ============================================================================
# PrintInfo Tests
# ============================================================================

class TestPrintInfo:
    """Tests for the PrintInfo data class."""

    def test_creation(self):
        """Test PrintInfo can be created with all fields."""
        info = PrintInfo(
            original_filename="benchy.gcode",
            temp_filename=".helix_temp/mod_123_benchy.gcode",
            symlink_filename=".helix_print/benchy.gcode",
            modifications=["bed_leveling_disabled"],
            start_time=1234567890.0,
        )

        assert info.original_filename == "benchy.gcode"
        assert info.temp_filename == ".helix_temp/mod_123_benchy.gcode"
        assert info.symlink_filename == ".helix_print/benchy.gcode"
        assert info.modifications == ["bed_leveling_disabled"]
        assert info.start_time == 1234567890.0
        assert info.job_id is None
        assert info.db_id is None

    def test_job_id_assignment(self):
        """Test job_id can be assigned after creation."""
        info = PrintInfo(
            original_filename="test.gcode",
            temp_filename="temp.gcode",
            symlink_filename="symlink.gcode",
            modifications=[],
            start_time=0.0,
        )

        info.job_id = "ABC123"
        assert info.job_id == "ABC123"


# ============================================================================
# Component Initialization Tests
# ============================================================================

class TestHelixPrintInit:
    """Tests for HelixPrint component initialization."""

    def test_load_component(self, mock_server):
        """Test component loads successfully."""
        config = MockConfigHelper(mock_server)
        component = load_component(config)

        assert component is not None
        assert isinstance(component, HelixPrint)

    def test_default_config(self, mock_server):
        """Test default configuration values."""
        config = MockConfigHelper(mock_server)
        component = load_component(config)

        assert component.temp_dir == ".helix_temp"
        assert component.symlink_dir == ".helix_print"
        assert component.cleanup_delay == 86400  # 24 hours
        assert component.enabled is True

    def test_custom_config(self, mock_server):
        """Test custom configuration values."""
        config = MockConfigHelper(mock_server, {
            "temp_dir": "custom_temp",
            "symlink_dir": "custom_symlink",
            "cleanup_delay": 7200,
            "enabled": False,
        })
        component = load_component(config)

        assert component.temp_dir == "custom_temp"
        assert component.symlink_dir == "custom_symlink"
        assert component.cleanup_delay == 7200
        assert component.enabled is False

    def test_endpoints_registered(self, mock_server):
        """Test API endpoints are registered."""
        config = MockConfigHelper(mock_server)
        load_component(config)

        assert "/server/helix/print_modified" in mock_server.endpoints
        assert "/server/helix/status" in mock_server.endpoints

    def test_event_handlers_registered(self, mock_server):
        """Test event handlers are registered."""
        config = MockConfigHelper(mock_server)
        load_component(config)

        assert "job_state:state_changed" in mock_server.event_handlers
        assert "server:klippy_ready" in mock_server.event_handlers


# ============================================================================
# Status API Tests
# ============================================================================

class TestStatusAPI:
    """Tests for the /server/helix/status endpoint."""

    @pytest.mark.asyncio
    async def test_status_returns_config(self, helix_print_component, mock_server):
        """Test status endpoint returns configuration."""
        handler = mock_server.endpoints["/server/helix/status"]
        request = MockWebRequest({})

        result = await handler(request)

        assert result["enabled"] is True
        assert result["temp_dir"] == ".helix_temp"
        assert result["symlink_dir"] == ".helix_print"
        assert result["cleanup_delay"] == 3600
        assert result["version"] == "1.0.1"
        assert result["active_prints"] == 0


# ============================================================================
# Print Modified API Tests (v2.0 path-based API)
# ============================================================================

class TestPrintModifiedAPI:
    """Tests for the /server/helix/print_modified endpoint (path-based API)."""

    @pytest.mark.asyncio
    async def test_rejects_missing_original(self, helix_print_component, mock_server,
                                            temp_gcodes_dir):
        """Test API rejects request when original file doesn't exist."""
        # Initialize component
        await helix_print_component.component_init()

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "nonexistent.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        with pytest.raises(Exception) as exc_info:
            await handler(request)

        assert "not found" in str(exc_info.value).lower()

    @pytest.mark.asyncio
    async def test_uses_uploaded_temp_file(self, helix_print_component, mock_server,
                                           temp_gcodes_dir):
        """Test API uses the pre-uploaded temp file."""
        # Create original file
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\nBED_MESH_CALIBRATE\nG1 X0 Y0\n")

        # Create temp file with modified content (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n; BED_MESH_CALIBRATE disabled\nG1 X0 Y0\n")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": ["bed_leveling_disabled"],
        })

        result = await handler(request)

        assert result["original_filename"] == "benchy.gcode"
        assert result["status"] == "printing"
        assert result["temp_filename"] == ".helix_temp/mod_benchy.gcode"

    @pytest.mark.asyncio
    async def test_creates_symlink(self, helix_print_component, mock_server,
                                   temp_gcodes_dir):
        """Test API creates symlink to temp file."""
        # Create original file
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\n")

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        result = await handler(request)

        # Verify symlink was created
        symlink_path = Path(temp_gcodes_dir) / result["print_filename"]
        assert symlink_path.is_symlink()

    @pytest.mark.asyncio
    async def test_starts_print_with_symlink(self, helix_print_component, mock_server,
                                             temp_gcodes_dir):
        """Test API starts print using symlink path."""
        # Create original file
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\n")

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        await handler(request)

        # Verify print command was sent via klippy_apis (NOT klippy_connection,
        # which has no run_gcode method - see MockKlippy/MockKlippyApis docs)
        klippy_apis = mock_server.components["klippy_apis"]
        assert klippy_apis.run_gcode.await_count == 1
        sent_command = klippy_apis.run_gcode.await_args.args[0]
        assert ".helix_print/benchy.gcode" in sent_command

    @pytest.mark.asyncio
    async def test_print_start_calls_klippy_apis_not_klippy_connection(
        self, helix_print_component, mock_server, temp_gcodes_dir
    ):
        """Regression test for production crash (debug bundle RA6EPJTZ):

        'Failed to start print: 'KlippyConnection' object has no attribute
        run_gcode''. Starting a print must call klippy_apis.run_gcode exactly
        once with an SDCARD_PRINT_FILE command naming the symlink - not
        klippy_connection, which has no run_gcode method in real Moonraker.

        This test FAILS against the old (klippy_connection.run_gcode) code and
        PASSES after routing through klippy_apis.
        """
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\n")

        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        result = await handler(request)
        assert result["status"] == "printing"

        klippy_apis = mock_server.components["klippy_apis"]
        assert klippy_apis.run_gcode.await_count == 1
        sent_command = klippy_apis.run_gcode.await_args.args[0]
        assert "SDCARD_PRINT_FILE" in sent_command
        assert 'FILENAME=".helix_print/benchy.gcode"' in sent_command

        # klippy_connection must never be touched for this - it has no
        # run_gcode method on real Moonraker.
        klippy_connection = mock_server.components["klippy_connection"]
        assert not hasattr(klippy_connection, "run_gcode")

    @pytest.mark.asyncio
    async def test_disabled_returns_error(self, mock_server, temp_gcodes_dir):
        """Test API returns error when component is disabled."""
        mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
        mock_server.components["database"] = MockDatabase()

        config = MockConfigHelper(mock_server, {"enabled": False})
        component = load_component(config)

        # Create temp file
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_test.gcode"
        temp_file.write_text("G28\n")

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "test.gcode",
            "temp_file_path": ".helix_temp/mod_test.gcode",
        })

        with pytest.raises(Exception) as exc_info:
            await handler(request)

        assert "disabled" in str(exc_info.value).lower()


# ============================================================================
# Symlink Conflict Tests
# ============================================================================

class TestSymlinkConflicts:
    """Tests for symlink conflict handling."""

    @pytest.mark.asyncio
    async def test_replaces_existing_symlink(self, helix_print_component, mock_server,
                                             temp_gcodes_dir):
        """Test that existing symlinks are replaced."""
        # Create original file
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\n")

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        # Create existing symlink
        symlink_dir = Path(temp_gcodes_dir) / ".helix_print"
        symlink_dir.mkdir(parents=True, exist_ok=True)
        existing_symlink = symlink_dir / "benchy.gcode"
        existing_symlink.symlink_to("/nonexistent")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        # Should succeed, replacing the existing symlink
        result = await handler(request)
        assert result["status"] == "printing"


# ============================================================================
# Active Print Tracking Tests
# ============================================================================

class TestActivePrintTracking:
    """Tests for active print tracking."""

    @pytest.mark.asyncio
    async def test_tracks_active_print(self, helix_print_component, mock_server,
                                       temp_gcodes_dir):
        """Test that active prints are tracked."""
        # Create original file
        original = Path(temp_gcodes_dir) / "benchy.gcode"
        original.write_text("G28\n")

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": ["test_mod"],
        })

        result = await handler(request)

        # Check active prints
        assert len(helix_print_component.active_prints) == 1
        print_info = helix_print_component.active_prints[result["print_filename"]]
        assert print_info.original_filename == "benchy.gcode"
        assert print_info.modifications == ["test_mod"]


# ============================================================================
# Path Validation Tests
# ============================================================================

class TestPathValidation:
    """Tests for path validation and security."""

    @pytest.mark.asyncio
    async def test_handles_subdirectory_path(self, helix_print_component, mock_server,
                                             temp_gcodes_dir):
        """Test handling of files in subdirectories."""
        # Create subdirectory and file
        subdir = Path(temp_gcodes_dir) / "prints" / "2024"
        subdir.mkdir(parents=True, exist_ok=True)
        original = subdir / "benchy.gcode"
        original.write_text("G28\n")

        # Create temp file (simulating client upload)
        temp_dir = Path(temp_gcodes_dir) / ".helix_temp"
        temp_dir.mkdir(parents=True, exist_ok=True)
        temp_file = temp_dir / "mod_benchy.gcode"
        temp_file.write_text("G28\n")

        # Initialize component
        await helix_print_component.component_init()

        handler = mock_server.endpoints["/server/helix/print_modified"]
        request = MockWebRequest({
            "original_filename": "prints/2024/benchy.gcode",
            "temp_file_path": ".helix_temp/mod_benchy.gcode",
            "modifications": [],
        })

        result = await handler(request)
        assert result["status"] == "printing"


# ============================================================================
# Phase Tracking Instrumentation Tests
# ============================================================================

class TestInstrumentGcode:
    """Tests for the _instrument_gcode method."""

    @pytest.fixture
    def helix(self, mock_server):
        """Create a HelixPrint instance for testing instrumentation."""
        config = MockConfigHelper(mock_server)
        return load_component(config)

    def test_adds_starting_marker_at_beginning(self, helix):
        """Test that STARTING phase is added at the beginning."""
        gcode = "G28\nG0 X0 Y0\n"
        result = helix._instrument_gcode(gcode)

        # Should start with the STARTING marker
        lines = result.split("\n")
        assert lines[0] == helix.TRACKING_MARKER_BEGIN
        assert 'VARIABLE=phase VALUE=\'"STARTING"\'' in lines[1]
        assert lines[2] == helix.TRACKING_MARKER_END

    def test_adds_complete_marker_at_end(self, helix):
        """Test that COMPLETE phase is added at the end."""
        gcode = "G28\nG0 X0 Y0\n"
        result = helix._instrument_gcode(gcode)

        # Should end with the COMPLETE marker
        lines = result.split("\n")
        assert lines[-3] == helix.TRACKING_MARKER_BEGIN
        assert 'VARIABLE=phase VALUE=\'"COMPLETE"\'' in lines[-2]
        assert lines[-1] == helix.TRACKING_MARKER_END

    def test_detects_g28_homing(self, helix):
        """Test G28 is detected as HOMING phase."""
        gcode = "G28\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"HOMING"\'' in result

    def test_detects_quad_gantry_level(self, helix):
        """Test QUAD_GANTRY_LEVEL is detected as QGL phase."""
        gcode = "QUAD_GANTRY_LEVEL\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"QGL"\'' in result

    def test_detects_z_tilt_adjust(self, helix):
        """Test Z_TILT_ADJUST is detected as Z_TILT phase."""
        gcode = "Z_TILT_ADJUST\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"Z_TILT"\'' in result

    def test_detects_bed_mesh_calibrate(self, helix):
        """Test BED_MESH_CALIBRATE is detected as BED_MESH phase."""
        gcode = "BED_MESH_CALIBRATE ADAPTIVE=1\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"BED_MESH"\'' in result

    def test_detects_clean_nozzle(self, helix):
        """Test CLEAN_NOZZLE variants are detected as CLEANING phase."""
        for cmd in ["CLEAN_NOZZLE", "WIPE_NOZZLE"]:
            gcode = f"{cmd}\n"
            result = helix._instrument_gcode(gcode)
            assert 'VALUE=\'"CLEANING"\'' in result, f"Failed for {cmd}"

    def test_detects_purge_macros(self, helix):
        """Test purge-related macros are detected as PURGING phase."""
        for cmd in ["PURGE", "PURGE_LINE", "LINE_PURGE", "VORON_PURGE"]:
            gcode = f"{cmd}\n"
            result = helix._instrument_gcode(gcode)
            assert 'VALUE=\'"PURGING"\'' in result, f"Failed for {cmd}"

    def test_detects_m109_heating(self, helix):
        """Test M109 is detected as HEATING_NOZZLE phase."""
        gcode = "M109 S220\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"HEATING_NOZZLE"\'' in result

    def test_detects_m190_heating(self, helix):
        """Test M190 is detected as HEATING_BED phase."""
        gcode = "M190 S60\n"
        result = helix._instrument_gcode(gcode)

        assert 'VALUE=\'"HEATING_BED"\'' in result

    def test_ignores_comments(self, helix):
        """Test that comment lines are ignored."""
        gcode = "# G28 - this is just a comment\nG0 X0\n"
        result = helix._instrument_gcode(gcode)

        # Should NOT contain HOMING because G28 is in a comment
        assert 'VALUE=\'"HOMING"\'' not in result

    def test_preserves_original_gcode(self, helix):
        """Test that original gcode lines are preserved."""
        gcode = "G28\nG0 X150 Y150\nM104 S220\n"
        result = helix._instrument_gcode(gcode)

        assert "G28" in result
        assert "G0 X150 Y150" in result
        assert "M104 S220" in result

    def test_multiple_phases_in_sequence(self, helix):
        """Test a realistic PRINT_START macro with multiple phases."""
        gcode = """G28
QUAD_GANTRY_LEVEL
BED_MESH_CALIBRATE
M109 S220
LINE_PURGE
"""
        result = helix._instrument_gcode(gcode)

        # Check all phases are detected in order
        assert 'VALUE=\'"STARTING"\'' in result
        assert 'VALUE=\'"HOMING"\'' in result
        assert 'VALUE=\'"QGL"\'' in result
        assert 'VALUE=\'"BED_MESH"\'' in result
        assert 'VALUE=\'"HEATING_NOZZLE"\'' in result
        assert 'VALUE=\'"PURGING"\'' in result
        assert 'VALUE=\'"COMPLETE"\'' in result

    def test_only_one_marker_per_line(self, helix):
        """Test that only one phase marker is added per line."""
        # This line matches both HOMING (G28) pattern, but should only get one marker
        gcode = "G28\n"
        result = helix._instrument_gcode(gcode)

        # Count HOMING occurrences - should be exactly 1
        homing_count = result.count('VALUE=\'"HOMING"\'')
        assert homing_count == 1


class TestStripInstrumentation:
    """Tests for the _strip_instrumentation method."""

    @pytest.fixture
    def helix(self, mock_server):
        """Create a HelixPrint instance for testing."""
        config = MockConfigHelper(mock_server)
        return load_component(config)

    def test_removes_tracking_blocks(self, helix):
        """Test that tracking blocks are removed."""
        gcode = f"""G28
{helix.TRACKING_MARKER_BEGIN}
SET_GCODE_VARIABLE MACRO=_HELIX_PHASE_STATE VARIABLE=phase VALUE='"HOMING"'
{helix.TRACKING_MARKER_END}
G0 X0 Y0
"""
        result = helix._strip_instrumentation(gcode)

        assert helix.TRACKING_MARKER_BEGIN not in result
        assert helix.TRACKING_MARKER_END not in result
        assert "HELIX_PHASE_STATE" not in result
        assert "G28" in result
        assert "G0 X0 Y0" in result

    def test_handles_multiple_blocks(self, helix):
        """Test that multiple tracking blocks are removed."""
        gcode = f"""{helix.TRACKING_MARKER_BEGIN}
SET_GCODE_VARIABLE MACRO=_HELIX_PHASE_STATE VARIABLE=phase VALUE='"STARTING"'
{helix.TRACKING_MARKER_END}
G28
{helix.TRACKING_MARKER_BEGIN}
SET_GCODE_VARIABLE MACRO=_HELIX_PHASE_STATE VARIABLE=phase VALUE='"HOMING"'
{helix.TRACKING_MARKER_END}
BED_MESH_CALIBRATE
{helix.TRACKING_MARKER_BEGIN}
SET_GCODE_VARIABLE MACRO=_HELIX_PHASE_STATE VARIABLE=phase VALUE='"BED_MESH"'
{helix.TRACKING_MARKER_END}
{helix.TRACKING_MARKER_BEGIN}
SET_GCODE_VARIABLE MACRO=_HELIX_PHASE_STATE VARIABLE=phase VALUE='"COMPLETE"'
{helix.TRACKING_MARKER_END}
"""
        result = helix._strip_instrumentation(gcode)

        # All markers should be gone
        assert result.count(helix.TRACKING_MARKER_BEGIN) == 0
        assert result.count(helix.TRACKING_MARKER_END) == 0

        # Original gcode preserved
        assert "G28" in result
        assert "BED_MESH_CALIBRATE" in result

    def test_preserves_non_tracking_content(self, helix):
        """Test that non-tracking content is preserved."""
        gcode = "G28\nQUAD_GANTRY_LEVEL\nBED_MESH_CALIBRATE\n"
        result = helix._strip_instrumentation(gcode)

        # Should be unchanged since no tracking markers
        assert result == gcode

    def test_roundtrip_instrument_then_strip(self, helix):
        """Test that stripping instrumented gcode returns to original."""
        original = "G28\nQUAD_GANTRY_LEVEL\nM109 S220\nLINE_PURGE\n"

        instrumented = helix._instrument_gcode(original)
        stripped = helix._strip_instrumentation(instrumented)

        # Should return to original (allowing for some whitespace variation)
        assert "G28" in stripped
        assert "QUAD_GANTRY_LEVEL" in stripped
        assert "M109 S220" in stripped
        assert "LINE_PURGE" in stripped

        # No tracking code should remain
        assert "HELIX_PHASE_STATE" not in stripped
        assert helix.TRACKING_MARKER_BEGIN not in stripped


class TestPhaseTrackingEndpoints:
    """Tests for the phase tracking API endpoints."""

    @pytest.fixture
    def helix_with_klippy(self, mock_server, temp_gcodes_dir):
        """Create a HelixPrint instance with a mock klippy connection."""
        mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
        mock_server.components["database"] = MockDatabase()
        mock_server.components["klippy_connection"] = MockKlippy()
        mock_server.components["klippy_apis"] = MockKlippyApis()
        mock_server.components["history"] = MockHistory()

        config = MockConfigHelper(mock_server, {
            "temp_dir": ".helix_temp",
            "symlink_dir": ".helix_print",
            "cleanup_delay": 3600,
            "enabled": True,
        })

        return load_component(config)

    @pytest.mark.asyncio
    async def test_status_not_instrumented(self, helix_with_klippy, mock_server):
        """Test status endpoint when not instrumented."""
        # Initialize component to set up klippy reference
        await helix_with_klippy.component_init()

        handler = mock_server.endpoints["/server/helix/phase_tracking/status"]
        request = MockWebRequest({})

        result = await handler(request)

        # Should report not enabled (klippy mock doesn't provide macros)
        assert result["enabled"] is False

    @pytest.mark.asyncio
    async def test_enable_endpoint_registered(self, helix_with_klippy, mock_server):
        """Test that the enable endpoint is registered."""
        assert "/server/helix/phase_tracking/enable" in mock_server.endpoints

    @pytest.mark.asyncio
    async def test_disable_endpoint_registered(self, helix_with_klippy, mock_server):
        """Test that the disable endpoint is registered."""
        assert "/server/helix/phase_tracking/disable" in mock_server.endpoints

    @pytest.mark.asyncio
    async def test_status_endpoint_registered(self, helix_with_klippy, mock_server):
        """Test that the status endpoint is registered."""
        assert "/server/helix/phase_tracking/status" in mock_server.endpoints


class TestPhasePatterns:
    """Tests for the PHASE_PATTERNS regex patterns."""

    @pytest.fixture
    def helix(self, mock_server):
        """Create a HelixPrint instance."""
        config = MockConfigHelper(mock_server)
        return load_component(config)

    def test_pattern_count(self, helix):
        """Test expected number of phase patterns."""
        assert len(helix.PHASE_PATTERNS) == 8

    def test_all_patterns_have_tuple_format(self, helix):
        """Test all patterns are (regex, phase_name) tuples."""
        for pattern in helix.PHASE_PATTERNS:
            assert isinstance(pattern, tuple)
            assert len(pattern) == 2
            assert isinstance(pattern[0], str)  # regex
            assert isinstance(pattern[1], str)  # phase name

    def test_pattern_phases_are_uppercase(self, helix):
        """Test all phase names are uppercase."""
        for _, phase in helix.PHASE_PATTERNS:
            assert phase == phase.upper()


class TestMarkerConstants:
    """Tests for the tracking marker constants."""

    @pytest.fixture
    def helix(self, mock_server):
        """Create a HelixPrint instance."""
        config = MockConfigHelper(mock_server)
        return load_component(config)

    def test_marker_begin_contains_version(self, helix):
        """Test that the begin marker contains version info."""
        assert "v1" in helix.TRACKING_MARKER_BEGIN

    def test_marker_end_matches_begin(self, helix):
        """Test that end marker is the closing version of begin marker."""
        assert "HELIX_TRACKING" in helix.TRACKING_MARKER_BEGIN
        assert "HELIX_TRACKING" in helix.TRACKING_MARKER_END
        assert "/" in helix.TRACKING_MARKER_END  # closing marker

    def test_markers_are_comments(self, helix):
        """Test that markers are G-code comments (start with #)."""
        assert helix.TRACKING_MARKER_BEGIN.startswith("#")
        assert helix.TRACKING_MARKER_END.startswith("#")


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
