"""Load and validate the prebuilt native BuDDy compose adapter."""

from __future__ import annotations

import ctypes
import hashlib
import json
import pathlib
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE / "native" / "buddy_veccompose_adapter.cc"
SIDECAR_SCHEMA = "acacia-buddy-veccompose-adapter-v1"
BUDDY_MAX_VARIABLE_COUNT = 2_097_150
# BuDDy does not publish its variable ceiling through the C API or installed
# header.  Keep the measured ceiling tied to the exact library image that was
# validated when this adapter was introduced; unknown builds fail closed.
PINNED_BDDX_MAX_VARIABLE_COUNTS = {
    "a991f2049c44e3f3cf9102b7d40d9efc2c5bb2b8a3a1af7d120f7c23c9caf44d":
        BUDDY_MAX_VARIABLE_COUNT,
}


class BuddyAdapterError(RuntimeError):
    """The native adapter could not safely complete a BuDDy operation."""


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def adapter_sidecar_path(adapter: pathlib.Path) -> pathlib.Path:
    return adapter.with_suffix(adapter.suffix + ".json")


def _loaded_bddx_paths() -> tuple[pathlib.Path, ...]:
    maps = pathlib.Path("/proc/self/maps")
    if not maps.is_file():
        raise BuddyAdapterError("/proc/self/maps is required for BuDDy identity checks")
    paths = {
        pathlib.Path(fields[-1]).resolve()
        for line in maps.read_text(encoding="utf-8").splitlines()
        if (fields := line.split())
        and fields[-1].startswith("/")
        and pathlib.Path(fields[-1]).name.startswith("libbddx.so")
    }
    return tuple(sorted(paths))


def _one_loaded_bddx() -> pathlib.Path:
    paths = _loaded_bddx_paths()
    if len(paths) != 1:
        raise BuddyAdapterError(
            "expected exactly one loaded libbddx, found "
            + (", ".join(map(str, paths)) if paths else "none")
        )
    return paths[0]


def _max_variable_count(bddx: pathlib.Path) -> int:
    digest = _sha256(bddx)
    try:
        return PINNED_BDDX_MAX_VARIABLE_COUNTS[digest]
    except KeyError as error:
        raise BuddyAdapterError(
            "no validated variable ceiling for loaded libbddx "
            f"{bddx} (sha256 {digest})"
        ) from error


def _linked_bddx(adapter: pathlib.Path) -> pathlib.Path:
    result = subprocess.run(
        ["ldd", str(adapter)], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if result.returncode != 0:
        raise BuddyAdapterError(
            f"cannot resolve adapter dependencies: {(result.stderr or result.stdout).strip()}"
        )
    matches = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields and fields[0].startswith("libbddx.so"):
            if len(fields) < 3 or fields[1] != "=>" or fields[2] == "not":
                raise BuddyAdapterError(f"unresolved adapter BuDDy dependency: {line.strip()}")
            matches.append(pathlib.Path(fields[2]).resolve())
    if len(matches) != 1:
        raise BuddyAdapterError(
            f"expected one adapter libbddx dependency, found {len(matches)}"
        )
    return matches[0]


def _load_sidecar(adapter: pathlib.Path) -> dict[str, object]:
    sidecar = adapter_sidecar_path(adapter)
    if not sidecar.is_file():
        raise BuddyAdapterError(f"adapter sidecar not found: {sidecar}")
    try:
        payload = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BuddyAdapterError(f"cannot read adapter sidecar {sidecar}: {error}") from error
    if not isinstance(payload, dict) or payload.get("schema") != SIDECAR_SCHEMA:
        raise BuddyAdapterError(f"unsupported adapter sidecar schema: {sidecar}")
    return payload


def _sidecar_path(payload: dict[str, object], key: str) -> pathlib.Path:
    value = payload.get(key)
    if not isinstance(value, str) or not value:
        raise BuddyAdapterError(f"adapter sidecar has invalid {key}")
    return pathlib.Path(value).resolve()


def _sidecar_text(payload: dict[str, object], key: str) -> str:
    value = payload.get(key)
    if not isinstance(value, str) or not value:
        raise BuddyAdapterError(f"adapter sidecar has invalid {key}")
    return value


def _require_hash(payload: dict[str, object], key: str, path: pathlib.Path) -> None:
    expected = _sidecar_text(payload, key)
    if not path.is_file():
        raise BuddyAdapterError(f"adapter input not found: {path}")
    actual = _sha256(path)
    if actual != expected:
        raise BuddyAdapterError(
            f"adapter sidecar {key} mismatch for {path}: expected {expected}, got {actual}"
        )


def _validate_sidecar(
    adapter: pathlib.Path, loaded_bddx: pathlib.Path
) -> dict[str, object]:
    payload = _load_sidecar(adapter)
    if not isinstance(payload.get("flags"), list) or not all(
        isinstance(item, str) for item in payload["flags"]
    ):
        raise BuddyAdapterError("adapter sidecar has invalid flags")
    _sidecar_text(payload, "compiler")
    _sidecar_text(payload, "compiler_version")
    binding_interpreter = _sidecar_path(payload, "binding_interpreter")
    current_interpreter = pathlib.Path(sys.executable).resolve()
    if not binding_interpreter.samefile(current_interpreter):
        raise BuddyAdapterError(
            f"adapter was built for binding interpreter {binding_interpreter}, "
            f"but is loaded by {current_interpreter}"
        )
    source = _sidecar_path(payload, "source_path")
    if not source.samefile(SOURCE):
        raise BuddyAdapterError(
            f"adapter source path {source} does not match runtime source {SOURCE}"
        )
    _require_hash(payload, "source_sha256", source)
    _require_hash(payload, "adapter_sha256", adapter)
    header = _sidecar_path(payload, "bdd_header_path")
    _require_hash(payload, "bdd_header_sha256", header)
    recorded_bddx = _sidecar_path(payload, "libbddx_path")
    if not recorded_bddx.samefile(loaded_bddx):
        raise BuddyAdapterError(
            f"adapter sidecar binds {recorded_bddx}, but buddy mapped {loaded_bddx}"
        )
    _require_hash(payload, "libbddx_sha256", loaded_bddx)
    return payload


class BuddyVariableAdapter:
    """Checked variable-count access for the no-native-compose route."""

    _error_callback_type = ctypes.CFUNCTYPE(None, ctypes.c_int)

    def __init__(self, buddy: object, extension_path: pathlib.Path):
        bddx = _one_loaded_bddx()
        library = ctypes.CDLL(str(bddx))
        library.bdd_varnum.argtypes = []
        library.bdd_varnum.restype = ctypes.c_int
        library.bdd_setvarnum.argtypes = [ctypes.c_int]
        library.bdd_setvarnum.restype = ctypes.c_int
        library.bdd_error_hook.argtypes = [ctypes.c_void_p]
        library.bdd_error_hook.restype = ctypes.c_void_p
        library.bdd_clear_error.argtypes = []
        library.bdd_clear_error.restype = None

        binding_library = ctypes.CDLL(str(extension_path))
        binding_address = ctypes.cast(
            binding_library.bdd_versionnum, ctypes.c_void_p
        ).value
        library_address = ctypes.cast(
            library.bdd_versionnum, ctypes.c_void_p
        ).value
        if binding_address != library_address:
            raise BuddyAdapterError(
                "buddy extension and checked variable adapter use different "
                "libbddx symbols"
            )
        self._buddy = buddy
        self._library = library
        self.bddx_path = bddx
        self.max_variable_count = _max_variable_count(bddx)

    def variable_count(self) -> int:
        count = int(self._library.bdd_varnum())
        if count < 0:
            raise BuddyAdapterError(f"bdd_varnum failed with status {count}")
        return count

    def set_variable_count(self, required: int) -> None:
        if not 0 <= required <= self.max_variable_count:
            raise OverflowError(
                f"BDD variable requirement {required} exceeds backend maximum "
                f"{self.max_variable_count}"
            )
        buddy_error = 0

        def record_error(status: int) -> None:
            nonlocal buddy_error
            if buddy_error == 0:
                buddy_error = status

        callback = self._error_callback_type(record_error)
        previous = self._library.bdd_error_hook(
            ctypes.cast(callback, ctypes.c_void_p)
        )
        try:
            status = int(self._library.bdd_setvarnum(required))
        finally:
            self._library.bdd_error_hook(previous)
        if buddy_error != 0 or status < 0:
            error_status = buddy_error or status
            self._library.bdd_clear_error()
            try:
                detail = self._buddy.bdd_errstring(error_status)
            except (AttributeError, TypeError):
                detail = ""
            raise BuddyAdapterError(
                "bdd_setvarnum failed "
                f"({error_status}): {detail or 'unknown BuDDy error'}"
            )


class BuddyVeccomposeAdapter:
    """A checked bridge from SWIG-owned ``bdd *`` proxies to bdd_veccompose."""

    def __init__(self, buddy: object, extension_path: pathlib.Path,
                 adapter_path: pathlib.Path):
        path = adapter_path.expanduser().resolve()
        if not path.is_file():
            raise BuddyAdapterError(f"configured adapter does not exist: {path}")
        loaded_before = _one_loaded_bddx()
        sidecar = _validate_sidecar(path, loaded_before)
        linked = _linked_bddx(path)
        if not linked.samefile(loaded_before):
            raise BuddyAdapterError(
                f"adapter resolves {linked}, but buddy owns handles in {loaded_before}"
            )
        library = ctypes.CDLL(str(path))
        loaded_after = _one_loaded_bddx()
        if not loaded_after.samefile(loaded_before):
            raise BuddyAdapterError("loading the adapter changed the active libbddx")

        library.p2a_bdd_veccompose.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_size_t,
        ]
        library.p2a_bdd_veccompose.restype = ctypes.c_int
        library.p2a_bdd_gbc.argtypes = []
        library.p2a_bdd_gbc.restype = ctypes.c_int
        library.p2a_bdd_setvarorder_for_testing.argtypes = [
            ctypes.POINTER(ctypes.c_int), ctypes.c_size_t,
        ]
        library.p2a_bdd_setvarorder_for_testing.restype = ctypes.c_int
        library.p2a_bdd_varnum.argtypes = []
        library.p2a_bdd_varnum.restype = ctypes.c_int
        library.p2a_bdd_max_variable_count.argtypes = []
        library.p2a_bdd_max_variable_count.restype = ctypes.c_int
        library.p2a_bdd_setvarnum_checked.argtypes = [ctypes.c_int]
        library.p2a_bdd_setvarnum_checked.restype = ctypes.c_int
        library.p2a_bdd_last_error.argtypes = []
        library.p2a_bdd_last_error.restype = ctypes.c_char_p
        library.p2a_bdd_versionnum_address.argtypes = []
        library.p2a_bdd_versionnum_address.restype = ctypes.c_void_p
        library.p2a_bdd_force_failure_for_testing.argtypes = [ctypes.c_int]
        library.p2a_bdd_force_failure_for_testing.restype = None
        library.p2a_bdd_trigger_error_for_testing.argtypes = []
        library.p2a_bdd_trigger_error_for_testing.restype = ctypes.c_int
        library.p2a_bdd_prior_handler_restored_for_testing.argtypes = []
        library.p2a_bdd_prior_handler_restored_for_testing.restype = ctypes.c_int

        binding_library = ctypes.CDLL(str(extension_path))
        binding_address = ctypes.cast(
            binding_library.bdd_versionnum, ctypes.c_void_p
        ).value
        adapter_address = library.p2a_bdd_versionnum_address()
        if binding_address != adapter_address:
            raise BuddyAdapterError(
                "buddy extension and native adapter use different libbddx symbols"
            )
        self._buddy = buddy
        self._library = library
        self.path = path
        self.sidecar = sidecar
        self.bddx_path = loaded_before
        self.symbol_address = adapter_address
        self.max_variable_count = _max_variable_count(loaded_before)
        native_maximum = int(library.p2a_bdd_max_variable_count())
        if native_maximum != self.max_variable_count:
            raise BuddyAdapterError(
                "native adapter variable ceiling does not match its validated "
                f"libbddx identity: {native_maximum} != {self.max_variable_count}"
            )

    def description(self) -> dict[str, object]:
        return {
            "path": str(self.path),
            "sidecar": str(adapter_sidecar_path(self.path)),
            "libbddx": str(self.bddx_path),
            "symbol_address": self.symbol_address,
            "sidecar_schema": self.sidecar["schema"],
            "max_variable_count": self.max_variable_count,
        }

    def _raise_status(self, operation: str, status: int) -> None:
        raw = self._library.p2a_bdd_last_error()
        detail = raw.decode("utf-8", errors="replace") if raw else ""
        try:
            buddy_detail = self._buddy.bdd_errstring(status) if status < 0 else ""
        except (AttributeError, TypeError):
            buddy_detail = ""
        message = detail or buddy_detail or f"status {status}"
        raise BuddyAdapterError(f"{operation} failed ({status}): {message}")

    def compose(self, function: object, variables: list[int],
                replacements: list[object]) -> object:
        if len(variables) != len(replacements):
            raise ValueError("substitution vectors have different lengths")
        if len(set(variables)) != len(variables):
            raise ValueError("substitution variables must be unique")
        var_count = self.variable_count()
        if any(variable < 0 or variable >= var_count for variable in variables):
            raise ValueError(
                f"substitution variables must be in [0, {var_count})"
            )
        output = self._buddy.bdd()
        native_variables = (ctypes.c_int * len(variables))(*variables)
        native_replacements = (ctypes.c_void_p * len(replacements))(
            *(int(replacement.this) for replacement in replacements)
        )
        status = self._library.p2a_bdd_veccompose(
            int(output.this), int(function.this), native_variables,
            native_replacements, len(variables),
        )
        if status != 0:
            self._raise_status("bdd_veccompose", status)
        return output

    def variable_count(self) -> int:
        count = int(self._library.p2a_bdd_varnum())
        if count < 0:
            self._raise_status("bdd_varnum", count)
        return count

    def set_variable_count(self, required: int) -> None:
        if not 0 <= required <= self.max_variable_count:
            raise OverflowError(
                f"BDD variable requirement {required} exceeds backend maximum "
                f"{self.max_variable_count}"
            )
        status = self._library.p2a_bdd_setvarnum_checked(required)
        if status != 0:
            self._raise_status("bdd_setvarnum", status)

    def collect_garbage(self) -> None:
        status = self._library.p2a_bdd_gbc()
        if status != 0:
            self._raise_status("bdd_gbc", status)

    def set_variable_order_for_testing(self, variables: list[int]) -> None:
        """Install a complete semantic-variable order for regression tests."""
        count = self.variable_count()
        if len(variables) != count or set(variables) != set(range(count)):
            raise ValueError("variable order must be a complete permutation")
        native_variables = (ctypes.c_int * count)(*variables)
        status = self._library.p2a_bdd_setvarorder_for_testing(
            native_variables, count)
        if status != 0:
            self._raise_status("bdd_setvarorder", status)

    def force_failure_for_testing(self, status: int = -11) -> None:
        self._library.p2a_bdd_force_failure_for_testing(status)

    def trigger_buddy_error_for_testing(self) -> None:
        status = self._library.p2a_bdd_trigger_error_for_testing()
        if status != 0:
            self._raise_status("bdd_setbddpair", status)

    def prior_handler_restored_for_testing(self) -> bool:
        return bool(self._library.p2a_bdd_prior_handler_restored_for_testing())


def validate_buddy_adapter(
    buddy: object, extension_path: pathlib.Path, adapter_path: pathlib.Path
) -> dict[str, object]:
    """Load and fully validate a configured adapter for ``--probe``."""
    return BuddyVeccomposeAdapter(buddy, extension_path, adapter_path).description()
