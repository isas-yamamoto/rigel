# Tests and CI

## Tests

- `tests/test.cc`: a functional test covering Write/Read consistency,
  splitting across multiple files, Scan enumeration, safe failure on
  out-of-range indices, `Init(dirname)` via metadata, and concurrent
  Write/Read from multiple threads (run via `make check`).
- `tests/test_c.c`: compiled as plain C, exercises `rigel_c.h` to prove
  it's actually callable from C (also run via `make check`).
- `tests/test_python.py`: exercises `python/rigel.py` against a real
  built `librigel` (`RIGEL_LIBRARY_PATH=/path/to/librigel.so python3
  tests/test_python.py`); run by the `cmake-build` CI job after install.
- Also verified with ThreadSanitizer and AddressSanitizer+UBSan (see the
  CI jobs below for the exact build commands) - functional tests alone
  can't tell whether a race or memory-safety issue exists.

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
