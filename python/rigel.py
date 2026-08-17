"""ctypes wrapper around librigel's C API (see ../src/rigel_c.h).

Loads librigel via the system loader (after `make install`/`ldconfig`,
or CMake's install) or RIGEL_LIBRARY_PATH for a not-yet-installed build.
"""
import ctypes
import ctypes.util
import os

DEFAULT_BLOCK_SIZE = 1024
DEFAULT_MAX_FILE_COUNT = 131072


class RigelError(Exception):
    pass


def _load_library():
    candidates = []
    env_path = os.environ.get("RIGEL_LIBRARY_PATH")
    if env_path:
        candidates.append(env_path)
    found = ctypes.util.find_library("rigel")
    if found:
        candidates.append(found)
    candidates += ["librigel.so", "librigel.dylib"]

    last_error = None
    for name in candidates:
        try:
            return ctypes.CDLL(name)
        except OSError as e:
            last_error = e
    raise OSError(
        "could not load librigel (tried: %s); install it (see README) or "
        "set RIGEL_LIBRARY_PATH to the .so/.dylib path" % ", ".join(candidates)
    ) from last_error


_lib = _load_library()

_lib.rigel_c_create.restype = ctypes.c_void_p
_lib.rigel_c_create.argtypes = []

_lib.rigel_c_destroy.restype = None
_lib.rigel_c_destroy.argtypes = [ctypes.c_void_p]

_lib.rigel_c_init.restype = None
_lib.rigel_c_init.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_int, ctypes.c_int, ctypes.c_int,
]

_lib.rigel_c_init_from_meta.restype = ctypes.c_int
_lib.rigel_c_init_from_meta.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.rigel_c_write_meta.restype = ctypes.c_int
_lib.rigel_c_write_meta.argtypes = [
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
]

_lib.rigel_c_write.restype = ctypes.c_ssize_t
_lib.rigel_c_write.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]

_lib.rigel_c_read.restype = ctypes.c_ssize_t
_lib.rigel_c_read.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]

_lib.rigel_c_delete.restype = ctypes.c_int
_lib.rigel_c_delete.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.rigel_c_scan_init.restype = ctypes.c_int
_lib.rigel_c_scan_init.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.rigel_c_scan_next.restype = ctypes.c_int
_lib.rigel_c_scan_next.argtypes = [ctypes.c_void_p]

_lib.rigel_c_last_error.restype = ctypes.c_char_p
_lib.rigel_c_last_error.argtypes = [ctypes.c_void_p]

_lib.rigel_c_block_size.restype = ctypes.c_int
_lib.rigel_c_block_size.argtypes = [ctypes.c_void_p]

_lib.rigel_c_key.restype = ctypes.c_char_p
_lib.rigel_c_key.argtypes = [ctypes.c_void_p]

_lib.rigel_c_max_file_count.restype = ctypes.c_int
_lib.rigel_c_max_file_count.argtypes = [ctypes.c_void_p]

_lib.rigel_c_index_offset.restype = ctypes.c_int
_lib.rigel_c_index_offset.argtypes = [ctypes.c_void_p]


class Rigel:
    """A Rigel data directory. Pass only `dirname` to read its existing
    rigel.meta; pass `key` too to initialize directly (mirrors
    rigel::Rigel::Init's two overloads)."""

    def __init__(self, dirname, key=None, block_size=None,
                 max_file_count=None, index_offset=0):
        self._dirname = dirname
        self._handle = _lib.rigel_c_create()
        if key is None:
            if not _lib.rigel_c_init_from_meta(self._handle, dirname.encode()):
                err = self.last_error()
                _lib.rigel_c_destroy(self._handle)
                self._handle = None
                raise RigelError(err or "Init(dirname) failed")
        else:
            _lib.rigel_c_init(
                self._handle, dirname.encode(), key.encode(),
                block_size if block_size is not None else DEFAULT_BLOCK_SIZE,
                max_file_count if max_file_count is not None else DEFAULT_MAX_FILE_COUNT,
                index_offset,
            )

    @staticmethod
    def write_meta(dirname, key, block_size=DEFAULT_BLOCK_SIZE,
                    max_file_count=DEFAULT_MAX_FILE_COUNT, index_offset=0):
        return bool(_lib.rigel_c_write_meta(
            dirname.encode(), key.encode(), block_size, max_file_count, index_offset))

    def write(self, index, data):
        return _lib.rigel_c_write(self._handle, index, data, len(data))

    def read(self, index, size=None):
        size = size if size is not None else self.block_size
        buf = ctypes.create_string_buffer(size)
        n = _lib.rigel_c_read(self._handle, index, buf, size)
        return buf.raw[:n] if n >= 0 else None

    def delete(self, index):
        return bool(_lib.rigel_c_delete(self._handle, index))

    def scan(self, start=0):
        if not _lib.rigel_c_scan_init(self._handle, start):
            raise RigelError(self.last_error() or "ScanInit failed")
        while True:
            idx = _lib.rigel_c_scan_next(self._handle)
            if idx < 0:
                return
            yield idx

    def stat(self):
        """Mirrors `rigel stat <dir>`: key/geometry/usage info as a dict."""
        block_size = self.block_size
        max_file_count = self.max_file_count
        record_count = 0
        min_index = max_index = None
        for idx in self.scan():
            record_count += 1
            if min_index is None:
                min_index = idx
            max_index = idx

        key = self.key
        key_prefix = key + "."
        shard_count = 0
        shard_bytes = 0
        for name in os.listdir(self._dirname):
            suffix = name[len(key_prefix):]
            if (len(name) == len(key_prefix) + 4 and
                    name.startswith(key_prefix) and suffix.isdigit()):
                shard_count += 1
                shard_bytes += os.stat(os.path.join(self._dirname, name)).st_size

        index_path = os.path.join(self._dirname, key + ".index")
        index_bytes = os.stat(index_path).st_size if os.path.exists(index_path) else 0

        return {
            "key": key,
            "block_size": block_size,
            "max_file_count": max_file_count,
            "max_file_size": block_size * max_file_count,
            "index_offset": self.index_offset,
            "record_count": record_count,
            "min_index": min_index,
            "max_index": max_index,
            "shard_count": shard_count,
            "shard_bytes": shard_bytes,
            "index_bytes": index_bytes,
        }

    def last_error(self):
        err = _lib.rigel_c_last_error(self._handle)
        return err.decode() if err else ""

    @property
    def block_size(self):
        return _lib.rigel_c_block_size(self._handle)

    @property
    def key(self):
        return _lib.rigel_c_key(self._handle).decode()

    @property
    def max_file_count(self):
        return _lib.rigel_c_max_file_count(self._handle)

    @property
    def index_offset(self):
        return _lib.rigel_c_index_offset(self._handle)

    def close(self):
        if self._handle is not None:
            _lib.rigel_c_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def __del__(self):
        self.close()
