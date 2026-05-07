"""Python FFI for GENIE shared-library random access.

The wrapper uses cdlml to load libgenie.so in an isolated dynamic-linker
namespace. This keeps GENIE dependencies scoped to this object while still
allowing returned C pointers to be read and freed in the Python process.
"""

from __future__ import annotations

import ctypes
from pathlib import Path

from cdlml import GLIBCPreloadedCDLL

PathLike = str | bytes | Path

GENIE_SHARED_SUCCESS = 0
GENIE_SHARED_INVALID_PARAMETER = 13
GENIE_LOG_DEBUG = 0
GENIE_LOG_INFO = 1
GENIE_LOG_WARNING = 2
GENIE_LOG_ERROR = 3


class GenieError(RuntimeError):
    """Raised when libgenie returns a non-success status."""

    def __init__(self, code: int, message: str) -> None:
        super().__init__(f"GENIE error {code}: {message}")
        self.code = code


def _path(value: PathLike | None) -> bytes | None:
    if value is None:
        return None
    if isinstance(value, bytes):
        return value
    return str(value).encode()


class Genie:
    """Wrapper for build/lib/libgenie.so.

    Methods expose access-unit counting and random-access decompression from
    .mgb to FASTQ bytes or text.
    """

    def __init__(self, library: PathLike | None = None) -> None:
        if library is None:
            repo = Path(__file__).resolve().parents[3]
            library = repo / "build" / "lib" / "libgenie.so"

        self._lib = GLIBCPreloadedCDLL(_path(library))
        self._configure()

    def _configure(self) -> None:
        self._lib.GenieSharedStrerror.argtypes = [ctypes.c_uint8]
        self._lib.GenieSharedStrerror.restype = ctypes.c_char_p

        self._lib.GenieSetLogSeverity.argtypes = [ctypes.c_uint8]
        self._lib.GenieSetLogSeverity.restype = ctypes.c_uint8

        self._lib.GenieGetAccessUnitCount.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint64),
        ]
        self._lib.GenieGetAccessUnitCount.restype = ctypes.c_uint8

        self._lib.GenieDecompressAccessUnit.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint64,
        ]
        self._lib.GenieDecompressAccessUnit.restype = ctypes.c_uint8

        self._lib.GenieDecompressAccessUnitToFastq.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_uint64),
        ]
        self._lib.GenieDecompressAccessUnitToFastq.restype = ctypes.c_uint8

        self._lib.GenieFree.argtypes = [ctypes.c_void_p]
        self._lib.GenieFree.restype = None

    def _check(self, code: int) -> None:
        if code == GENIE_SHARED_SUCCESS:
            return
        raw = self._lib.GenieSharedStrerror(code)
        message = raw.decode() if raw else "unknown error"
        raise GenieError(code, message)

    def set_log_severity(self, severity: int) -> None:
        """Set minimum GENIE log severity: 0 DEBUG, 1 INFO, 2 WARNING, 3 ERROR."""
        if severity < GENIE_LOG_DEBUG or severity > GENIE_LOG_ERROR:
            self._check(GENIE_SHARED_INVALID_PARAMETER)
            return
        code = self._lib.GenieSetLogSeverity(severity)
        self._check(code)

    def access_unit_count(self, input_file: PathLike) -> int:
        count = ctypes.c_uint64()
        code = self._lib.GenieGetAccessUnitCount(_path(input_file), ctypes.byref(count))
        self._check(code)
        return int(count.value)

    def decompress_access_unit_to_file(
        self,
        input_file: PathLike,
        access_unit_id: int,
        output_file: PathLike,
        reference_file: PathLike | None = None,
        working_dir: PathLike | None = None,
        threads: int = 1,
    ) -> None:
        code = self._lib.GenieDecompressAccessUnit(
            _path(input_file),
            access_unit_id,
            _path(output_file),
            _path(reference_file),
            _path(working_dir),
            threads,
        )
        self._check(code)

    def decompress_access_unit_to_bytes(
        self,
        input_file: PathLike,
        access_unit_id: int,
        reference_file: PathLike | None = None,
        working_dir: PathLike | None = None,
        threads: int = 1,
    ) -> bytes:
        data = ctypes.c_void_p()
        size = ctypes.c_uint64()
        code = self._lib.GenieDecompressAccessUnitToFastq(
            _path(input_file),
            access_unit_id,
            _path(reference_file),
            _path(working_dir),
            threads,
            ctypes.byref(data),
            ctypes.byref(size),
        )
        self._check(code)
        try:
            return ctypes.string_at(data, size.value)
        finally:
            self._lib.GenieFree(data)

    def decompress_access_unit_to_string(
        self,
        input_file: PathLike,
        access_unit_id: int,
        reference_file: PathLike | None = None,
        working_dir: PathLike | None = None,
        threads: int = 1,
        encoding: str = "utf-8",
    ) -> str:
        return self.decompress_access_unit_to_bytes(
            input_file, access_unit_id, reference_file, working_dir, threads
        ).decode(encoding)
