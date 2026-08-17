# Rigel

**RIGEL** ( Rigid and Indexed Granular-data Engine Library )

A C++ library for fast reads/writes of fixed-size records keyed by a
sequential integer ID (0, 1, 2, ...).

Licensed under the [MIT License](LICENSE). Current version: `1.0.0`
(see `rigel::VERSION` in `rigel.h`, or run `rigel version`).

## What is this, and when would I use it?

Rigel is *not* a database engine - it has no SQL, no schema, no
secondary indexes, no transactions. It's a much smaller piece: a
slotted array of fixed-size records on disk, where "index N" always
maps to the same byte offset (`index * block_size`) via pure
arithmetic, `mmap`'d for speed. Think of it as the storage layer you'd
build a database *on top of*, not a replacement for one.

**Reach for Rigel when:**
- Records are addressed by a dense, sequential integer key - a tick
  count, a frame/sample number, a sensor reading index - not an
  arbitrary string or a value you'd look up by range/content.
- Records are fixed-size, or can be padded/truncated to one (Rigel
  stores raw bytes; you own the serialization format).
- You want O(1) offset computation with no B-tree/hash overhead, and
  you're fine handling durability (fsync), locking across processes,
  and schema evolution yourself (see "Limitations" below).

**Reach for SQLite (or Postgres, LevelDB, etc.) instead when** you need
variable-length records, lookup by arbitrary/string keys or content,
range queries beyond "everything scanned so far", joins, multi-record
ACID transactions, or a schema that can evolve without a data
migration.

A concrete fit: telemetry or sensor samples that arrive labeled with a
monotonically increasing sequence number, where each sample is the
same size and "give me sample N" (or "list every sample received so
far") is the only access pattern that matters. `freeze`/`unfreeze` (see
"Usage" below) exists for exactly this shape of data: mark a batch
read-only once it's finished, without giving up the ability to keep
reading/scanning it.

## Features

- **index = integer ID -> offset is pure arithmetic (`index * block_size`)**.
  No B-tree, no hash table. Lookup is O(1).
- Each index identifies exactly one record slot. Writing to an index
  that already has data overwrites it in place - there's no duplicate
  detection or append-only behavior, the same index always means the
  same slot.
- To keep any single file from growing without bound, records are split
  across physical files (shards) every `max_file_count` blocks. `index` is
  resolved into a `file_index`/`file_offset` pair to reach the right shard.
- A separate file (`.index`) holds one byte per index recording whether it
  has been written. Reading an index that was never written fails.
- Data and index files are `mmap`'d. Once opened they stay open; after that
  it's just pointer arithmetic + `memcpy`.
- One directory = one series (key). `rigel init` writes
  `key`/`block_size`/`max_file_count` into a metadata file under the
  directory, after which `Init(dirname)` alone is enough (see below).
- A single `Rigel` instance can be shared across threads, calling
  Write/Read/ScanInit/ScanNext concurrently (serialized internally by one
  mutex - see "Thread safety" below).

## Building

Requires C++11 or later. Quick start (CMake):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

See [docs/BUILDING.md](docs/BUILDING.md) for the plain-Makefile build,
installing (`make install`/`cmake --install`, pkg-config), and
generating API docs with Doxygen.

## Usage

First, initialize the directory with `rigel init` (once):

```sh
rigel init /path/to/data mykey 1024 131072 1000   # dirname key block_size max_file_count index_offset
```

This writes key/block_size/max_file_count/index_offset into
`/path/to/data/rigel.meta`. It refuses to run against a directory that
already has metadata (since that would corrupt existing data).
block_size/max_file_count default to `BLOCK_SIZE`=1024 and
`MAX_FILE_COUNT`=131072 if omitted; index_offset defaults to 0.

`index_offset` shifts the externally visible index space: with
`index_offset=1000`, index `1000` is the first record (internally
stored at position 0), and indices below `1000` are rejected. It's
stored in `rigel.meta` precisely so every tool/process touching the
same directory agrees on what index N means without each one
separately hard-coding or passing around the same shift.

Lines starting with `#` are treated as comments (ignored even if they
happen to contain e.g. `key=`), so `rigel.meta` can be hand-annotated.

The `rigel` command doubles as a quick way to poke at a directory from
the shell:

```sh
echo -n "hello" | rigel write /path/to/data 42
rigel read /path/to/data 42 | hexdump -C
rigel delete /path/to/data 42        # clears a record back to never-written
rigel scan /path/to/data          # lists every written index, one per line
rigel stat /path/to/data          # key, geometry, record count, disk usage
rigel freeze /path/to/data        # blocks further write/delete (read/scan still work)
rigel unfreeze /path/to/data      # allows write/delete again
```

`freeze` flips a `frozen` flag in `rigel.meta` in place, without touching
key/block_size/max_file_count/index_offset. Any handle that (re)reads
that metadata afterwards refuses `Write`/`Delete` (`LastError()` names
"frozen" as the reason) while `Read`/`Scan`/`stat` keep working - a guard
against accidentally writing into data you've already finished with. A
handle opened before the freeze keeps its prior in-memory state, same as
block_size/key/etc.: reopen the directory to pick up a freeze/unfreeze
made by another process or handle.

From code, the only thing needed is `Init(dirname)`:

```cpp
#include "rigel.h"

rigel::Rigel rigel;
if (!rigel.Init("/path/to/data")) {
  fprintf(stderr, "init failed: %s\n", rigel.LastError());  // before rigel_init, or corrupt metadata
}

unsigned char buf[1024] = { ... };
rigel.Write(index, buf, sizeof(buf));

unsigned char rbuf[1024];
ssize_t n = rigel.Read(index, rbuf, sizeof(rbuf));  // -1 if never written

rigel.ScanInit();
int idx;
while ((idx = rigel.ScanNext()) >= 0) {
  // a record exists at idx
}
```

If you want to specify block_size/max_file_count explicitly from code
(e.g. small shards for tests), `Init(dirname, key, block_size,
max_file_count)` is still available.

Besides `rigel.meta`, `dirname` ends up containing data files named
`<key>.<4-digit file_index>` and an index file named `<key>.index`.

### From C

`rigel.h` is a C++ class and can't be linked from a C translation unit
(no `extern "C"`, virtual methods, `std::mutex` members). `rigel_c.h`
wraps it behind an opaque handle for C callers; same library, same
`rigel.meta` format, interchangeable with the C++ API on the same
directory:

```c
#include "rigel_c.h"

RigelHandle* h = rigel_c_create();
if (!rigel_c_init_from_meta(h, "/path/to/data")) {
  fprintf(stderr, "init failed: %s\n", rigel_c_last_error(h));
}

unsigned char buf[1024] = { ... };
rigel_c_write(h, index, buf, sizeof(buf));

unsigned char rbuf[1024];
ssize_t n = rigel_c_read(h, index, rbuf, sizeof(rbuf));  // -1 if never written

rigel_c_destroy(h);
```

### From Python

`python/rigel.py` is a `ctypes` wrapper around `rigel_c.h` - no
compiled extension, just loads `librigel` at import time (via the
system loader after `make install`/`cmake --install`, or
`RIGEL_LIBRARY_PATH` pointing at a not-yet-installed build's `.so`):

```python
import rigel

r = rigel.Rigel("/path/to/data")               # reads existing rigel.meta
# or: r = rigel.Rigel("/path/to/data", key="mykey", block_size=1024, max_file_count=131072)

r.write(index, b"hello")
data = r.read(index)                            # None if never written
for idx in r.scan():
    ...                                          # a record exists at idx
r.stat()                                         # dict: key/geometry/usage info
rigel.Rigel.set_frozen("/path/to/data", True)    # blocks further write()/delete()
r.close()                                        # or use `with rigel.Rigel(...) as r:`
```

## Tests & CI

`ctest --test-dir build` (CMake) or `make check` (Makefile, in `src/`)
runs the full test suite (C++, C, and - after an install - Python).
Every push/PR also runs strict-warnings, ThreadSanitizer,
AddressSanitizer+UBSan, and cppcheck builds. See
[docs/TESTING.md](docs/TESTING.md) for what each test file covers and
the full CI job matrix.

## Thread safety

A single `Rigel` instance can be shared across threads: `Write`/`Read`/
`ScanInit`/`ScanNext` are safe to call concurrently, serialized
internally by one mutex (simple and clearly correct, not tuned for
concurrent throughput). Concurrent writes from multiple *processes* to
the same directory are not synchronized (no flock or other file
locking) - arrange that at the call site if needed.

## Error handling & logging conventions

The library doesn't print to stderr by default (the one exception is
`WriteMeta()`, which has no `Rigel` instance to attach an error to).
Real I/O errors are recorded into `LastError()` with the `errno`
reason; call it right after a failing `Write`/`Read`/`ScanInit`/
`Init(dirname)`. Normal control flow (e.g. reading an index that was
never written) does not set `LastError()` - only misuse or real errors
do. The `rigel` CLI prints `rigel <subcommand>: <LastError()>` on
failure.

## Security

`key` is restricted to `[A-Za-z0-9_.-]+` (non-empty, no `/`) by both
`WriteMeta()` and `Init(dirname)`'s `rigel.meta` parser, since it's
interpolated directly into filesystem paths. Without a path separator
allowed in `key`, it can never escape `dirname` via `../` traversal -
which matters because `rigel.meta` is a file inside the directory, so
its `key` could come from someone other than whoever is currently
running `rigel` against it. This restriction does not apply to `key`
when passed directly to `Init(dirname, key, block_size,
max_file_count)`, which is trusted the same as any other caller-supplied
value.

## Limitations

- Neither `Read` nor `Write` calls fsync/msync. Durability stops at the
  page cache (resilient to a process crash, not to an OS crash or power
  loss).
- The index file grows (1MiB at a time) to match the highest index
  written. Each data file has a fixed size of `block_size * max_file_count`.
- Open data shards are capped (least-recently-used ones are evicted -
  munmap'd and closed - once the cap is exceeded), so a long-lived
  process that touches many shards over its lifetime doesn't accumulate
  an unbounded number of open file descriptors or mmap'd regions. A
  shard evicted and later touched again is simply reopened.
