# Building, installing, and API docs

Requires C++11 or later (uses `std::mutex`, `std::unordered_map`,
`std::thread`).

## Building

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
| headers | `rigel.h` (C++), `rigel_c.h` (C) |
| CLI tool | `rigel` (subcommands: `init`, `read`, `write`, `delete`, `scan`, `stat`, `freeze`, `unfreeze`, `version`) |
| test suites | `test`/`test_c` (Makefile) / `rigel_test`/`rigel_test_c` (CMake) |

## Installing

**Makefile:**

```sh
make install PREFIX=/usr/local     # PREFIX defaults to /usr/local
make uninstall PREFIX=/usr/local
```

`DESTDIR` is also honored for staged installs.

**CMake** (also generates a pkg-config file; set `CMAKE_INSTALL_PREFIX`
at configure time so it's reflected correctly in the generated
`rigel.pc`):

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
cmake --install build
```

Once installed, a consumer can pick up the right flags with:

```sh
pkg-config --cflags --libs rigel
```

## API docs

Generate Doxygen HTML/LaTeX docs from `rigel.h`/`rigel.cc` (run from the
repo root, needs `doxygen`):

```sh
doxygen docs/doxygen.conf   # writes docs/html and docs/latex
```
