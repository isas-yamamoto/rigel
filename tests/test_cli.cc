/**
 * Functional test for the `rigel` CLI binary (src/rigel_cli.cc), invoked as
 * a subprocess exactly as a real user would run it. Covers logic that
 * lives only in the CLI and isn't exercised by tests/test.cc (library-
 * level) or tests/test_c.c (C bindings): `dump`'s hex-encoding/start-end
 * filtering/--raw, and --read-only actually needing no write access to
 * the underlying files (not just accepting the flag).
 *
 * RIGEL_CLI_PATH is the built rigel_cli binary's absolute path, injected by
 * CMakeLists.txt via $<TARGET_FILE:rigel_cli> so this test doesn't depend
 * on the working directory ctest happens to run it from.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_fail = 0;

void check(bool cond, const char* label) {
  if (cond) {
    std::printf("PASS: %s\n", label);
  } else {
    std::printf("FAIL: %s\n", label);
    g_fail++;
  }
}

// Runs cmd via the shell and returns everything it wrote to stdout.
std::string RunCapture(const std::string& cmd) {
  FILE* p = ::popen(cmd.c_str(), "r");
  if (p == NULL) {
    return "";
  }
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) {
    out.append(buf, n);
  }
  ::pclose(p);
  return out;
}

// Runs cmd via the shell, feeding it `input` on stdin. Returns true if the
// subprocess exited successfully.
// std::system() is declared warn_unused_result on glibc; these calls are
// best-effort cleanup where a failure (e.g. dir didn't exist yet) doesn't
// matter, so discard the result explicitly rather than let -Werror flag it.
void RunShellBestEffort(const std::string& cmd) {
  int rc = std::system(cmd.c_str());
  (void)rc;
}

bool RunWithStdin(const std::string& cmd, const std::string& input) {
  FILE* p = ::popen(cmd.c_str(), "w");
  if (p == NULL) {
    return false;
  }
  std::fwrite(input.data(), 1, input.size(), p);
  return ::pclose(p) == 0;
}

} // namespace

int main() {
  const std::string cli = RIGEL_CLI_PATH;
  const std::string dir = "/tmp/rigel_test_cli";

  // Start from a clean slate: `rigel init` refuses to run against a
  // directory that already has metadata from a previous test run.
  RunShellBestEffort("rm -rf " + dir);
  ::mkdir(dir.c_str(), 0755);

  check(RunCapture(cli + " init " + dir + " testkey 4 100").find("initialized") == 0,
        "init succeeds");

  check(RunWithStdin(cli + " write " + dir + " 5", "ab"), "write index 5 succeeds");
  check(RunWithStdin(cli + " write " + dir + " 7", "cd"), "write index 7 succeeds");
  check(RunWithStdin(cli + " write " + dir + " 10", "ef"), "write index 10 succeeds");

  // Default (hex) output, no range: every written index, in order.
  {
    std::string out = RunCapture(cli + " dump " + dir);
    check(out == "5: 61620000\n7: 63640000\n10: 65660000\n",
          "dump with no range prints every written record as \"index: hex\"");
  }

  // start filters out indices before it.
  {
    std::string out = RunCapture(cli + " dump " + dir + " 6");
    check(out == "7: 63640000\n10: 65660000\n", "dump start filters earlier indices");
  }

  // end filters out indices after it (inclusive of end itself).
  {
    std::string out = RunCapture(cli + " dump " + dir + " 0 7");
    check(out == "5: 61620000\n7: 63640000\n", "dump end is inclusive and filters later indices");
  }

  // --raw concatenates the raw block bytes with no index labels.
  {
    std::string out = RunCapture(cli + " dump " + dir + " --raw");
    // Built byte-by-byte (not a "ab\0\0cd..." string literal) since a
    // literal's embedded NULs would need explicit sizing anyway to survive
    // std::string's usual strlen-based construction.
    unsigned char raw_expected[12] = {'a', 'b', 0, 0, 'c', 'd', 0, 0, 'e', 'f', 0, 0};
    std::string raw_expected_str(reinterpret_cast<char*>(raw_expected), sizeof(raw_expected));
    check(out == raw_expected_str, "dump --raw emits concatenated raw block bytes");
  }

  // --read-only: chmod the shard/index files themselves (not just the
  // directory - opening an already-existing file only needs permission on
  // the file, not on the directory it lives in) to prove this flag lets
  // read/scan/dump/stat work with genuinely no write access, e.g. a
  // read-only NFS export.
  {
    RunShellBestEffort("chmod 0444 " + dir + "/testkey.0000 " + dir + "/testkey.index");

    std::string without_flag = RunCapture(cli + " read " + dir + " 5 2>&1");
    check(without_flag.find("ab") == std::string::npos,
          "read without --read-only fails against files with no write permission");

    std::string with_flag = RunCapture(cli + " read " + dir + " 5 --read-only");
    unsigned char expected[4] = {'a', 'b', 0, 0}; // Read returns the full zero-padded block
    check(with_flag == std::string(reinterpret_cast<char*>(expected), sizeof(expected)),
          "read --read-only succeeds against files with no write permission");

    check(RunCapture(cli + " scan " + dir + " --read-only") == "5\n7\n10\n",
          "scan --read-only succeeds against files with no write permission");

    check(RunCapture(cli + " dump " + dir + " --read-only") ==
              "5: 61620000\n7: 63640000\n10: 65660000\n",
          "dump --read-only succeeds against files with no write permission");

    check(RunCapture(cli + " stat " + dir + " --read-only").find("records:         3") !=
              std::string::npos,
          "stat --read-only succeeds against files with no write permission");

    RunShellBestEffort("chmod 0644 " + dir + "/testkey.0000 " + dir + "/testkey.index");
  }

  RunShellBestEffort("rm -rf " + dir);

  if (g_fail == 0) {
    std::printf("All tests passed\n");
  } else {
    std::printf("%d test(s) failed\n", g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
