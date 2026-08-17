# Rigel

**RIGEL** ( Rigid and Indexed Granular-data Engine Library )

A C++ library for fast reads/writes of fixed-size records keyed by a
sequential integer ID (0, 1, 2, ...). Built for cases like satellite
telemetry, where large volumes of fixed-size packets need to be stored
and looked up by time.

Licensed under the [MIT License](LICENSE). Current version: `1.0.0`
(see `rigel::VERSION` in `rigel.h`, or run `rigel version`).

## Features

- **index = integer ID -> offset is pure arithmetic (`index * block_size`)**.
  No B-tree, no hash table. Lookup is O(1).
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

Requires C++11 or later (uses `std::mutex`, `std::unordered_map`,
`std::thread`).

**Plain Makefile** (`src/Makefile` - what CI's main build/test/sanitizer/
cppcheck jobs use):

```sh
cd src
make               # builds the library, the rigel CLI, and the test suite
make check         # runs the test suite
```

**CMake** (`CMakeLists.txt` at the repo root - also what CI verifies, and
the path that produces a pkg-config file and a properly SONAME'd shared
library):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Build outputs (same for either build system):

| kind | name |
|---|---|
| shared library | `librigel.so` |
| static library | `librigel.a` |
| header | `rigel.h` |
| CLI tool | `rigel` (subcommands: `init`, `read`, `write`, `scan`, `stat`, `version`) |
| test suite | `test` (Makefile) / `rigel_test` (CMake) |

## Installing

**Makefile:**

```sh
make install PREFIX=/usr/local     # PREFIX defaults to /usr/local
make uninstall PREFIX=/usr/local
```

`DESTDIR` is also honored for staged installs (`make install DESTDIR=/stage
PREFIX=/usr/local`).

**CMake** (also generates a pkg-config file):

```sh
# Set CMAKE_INSTALL_PREFIX at configure time, not just at install time -
# it's baked into the generated rigel.pc.
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
cmake --install build
```

Once installed, a consumer can pick up the right flags with:

```sh
pkg-config --cflags --libs rigel
```

## Usage

First, initialize the directory with `rigel init` (once):

```sh
./rigel init /path/to/data mykey 1024 131072   # dirname key block_size max_file_count
```

This writes key/block_size/max_file_count into
`/path/to/data/rigel.meta`. It refuses to run against a directory that
already has metadata (since that would corrupt existing data).
block_size/max_file_count default to `BLOCK_SIZE`=1024 and
`MAX_FILE_COUNT`=131072 if omitted.

The `rigel` command doubles as a quick way to poke at a directory from
the shell:

```sh
echo -n "hello" | ./rigel write /path/to/data 42
./rigel read /path/to/data 42 | hexdump -C
./rigel scan /path/to/data          # lists every written index, one per line
./rigel stat /path/to/data          # key, geometry, record count, disk usage
```

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

## Tests

- `src/test.cc`: a functional test covering Write/Read consistency,
  splitting across multiple files, Scan enumeration, safe failure on
  out-of-range indices, `Init(dirname)` via metadata, and concurrent
  Write/Read from multiple threads (run via `make check`).
- Thread safety is also verified with ThreadSanitizer:
  `g++ -std=c++11 -fsanitize=thread -pthread rigel.cc test.cc -o test_tsan`
  (plain `make check` only checks functional correctness - only
  ThreadSanitizer can actually tell whether a race exists).
- Likewise, memory safety/UB is checked with AddressSanitizer +
  UndefinedBehaviorSanitizer:
  `g++ -std=c++11 -fsanitize=address,undefined -pthread rigel.cc test.cc -o test_asan`

## CI

`.github/workflows/ci.yml` runs all of the following on every push/PR:

| job | what it does |
|---|---|
| `build-and-test` | normal build + `make check` |
| `strict-warnings` | rebuilds with `-Wall -Wextra -Werror` (warnings fail the build) |
| `thread-sanitizer` | catches races with ThreadSanitizer |
| `address-ub-sanitizer` | catches memory-safety/UB issues with AddressSanitizer+UBSan |
| `cmake-build` | configure/build/test/install via CMake, then sanity-checks the installed pkg-config file and binary |
| `cppcheck` | static analysis (`warning`/`performance`/`portability` categories; fails on any finding) |

## Thread safety

- A single `Rigel` instance is safe to call `Write`/`Read`/`ScanInit`/
  `ScanNext` on concurrently from multiple threads. Its shared state
  (mmap'd region bookkeeping, scan position) is protected by one
  `std::mutex`.
- The implementation is coarse-grained: one mutex serializes every call,
  so even access to unrelated shards never actually runs in parallel
  (prioritizing simple, clearly-correct thread safety over concurrent
  throughput). Per-shard locking would be the next step if higher
  concurrent throughput is ever needed.
- Out of scope: no exclusion is done for concurrent writes from multiple
  *processes* to the same directory (no flock or other file locking). If
  that's needed, arrange it at the call site.

## Error handling & logging conventions

- The library itself prints nothing to stderr by default. Real I/O errors
  (open/fstat/ftruncate/mmap failures) are recorded into `LastError()`
  with the reason from `errno`; whether and where to print that is left
  to the caller. The one exception is `WriteMeta()` (static, so there's
  no instance to attach a `LastError()` to - it prints directly to stderr
  on failure).
- Call `LastError()` right after `Write`/`Read`/`ScanInit`/`Init(dirname)`
  returns a failure (`-1`/`false`) to get the reason. Read it before
  another call is made on this instance (one buffer is reused per
  instance).
- There are two kinds of failure. **Normal control flow** (e.g. `Read`ing
  an index that was never written) does not set `LastError()` - it isn't
  a bug. **Misuse / real errors** (an out-of-range index, a write
  exceeding a shard's bounds, an open/mmap failure, etc.) do set a
  specific reason.
- The `rigel` CLI prints `rigel <subcommand>: <LastError()>` to stderr
  when a call fails - see `rigel_cli.cc` for a usage example.

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
