"""Smoke test for the ctypes wrapper (python/rigel.py), exercising it
against the same rigel.meta format the C++/C tests use."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
import rigel  # noqa: E402

fail = 0


def check(cond, label):
    global fail
    if cond:
        print("PASS: %s" % label)
    else:
        print("FAIL: %s" % label)
        fail += 1


def main():
    off_dir = "/tmp/rigel_test_python"
    os.makedirs(off_dir, exist_ok=True)

    check(rigel.Rigel.write_meta(off_dir, "py", 8, 4, 1000),
          "Rigel.write_meta succeeds with a non-zero index_offset")

    r = rigel.Rigel(off_dir)
    check(r.index_offset == 1000, "index_offset reads back from metadata")
    check(r.key == "py", "key reads back from metadata")
    check(r.block_size == 8, "block_size reads back from metadata")

    check(r.write(1000, b"PYPYPYPY") == 8, "write at the offset index succeeds")
    check(r.read(1000) == b"PYPYPYPY", "read at the offset index matches")
    check(r.write(999, b"x" * 8) == -1, "write below index_offset fails")
    check(r.read(999) is None, "read below index_offset returns None")

    r.write(1001, b"Y" * 8)
    check(list(r.scan()) == [1000, 1001], "scan returns offset-shifted indices in order")

    st = r.stat()
    check(st["key"] == "py", "stat() reports key")
    check(st["block_size"] == 8, "stat() reports block_size")
    check(st["record_count"] == 2, "stat() reports record_count")
    check((st["min_index"], st["max_index"]) == (1000, 1001), "stat() reports index range")
    check(st["shard_count"] >= 1, "stat() reports at least one shard file")
    check(st["index_bytes"] > 0, "stat() reports a non-zero index file size")

    check(not r.frozen, "frozen is False before freezing")
    check(rigel.Rigel.set_frozen(off_dir, True), "set_frozen(True) succeeds")
    with rigel.Rigel(off_dir) as rf:
        check(rf.frozen, "frozen reads back True after set_frozen")
        check(rf.write(1001, b"Z" * 8) == -1, "write fails on a frozen directory")
        check(rf.delete(1001) is False, "delete fails on a frozen directory")
        check(rf.read(1001) == b"Y" * 8, "read still works on a frozen directory")
        check(list(rf.scan()) == [1000, 1001], "scan still works on a frozen directory")
        check(rf.stat()["frozen"] is True, "stat() reports frozen")
    check(rigel.Rigel.set_frozen(off_dir, False), "set_frozen(False) succeeds")
    with rigel.Rigel(off_dir) as ru:
        check(not ru.frozen, "frozen reads back False after unfreezing")
        check(ru.write(1001, b"Y" * 8) == 8, "write succeeds again after unfreezing")

    check(r.delete(1000), "delete succeeds")
    check(r.read(1000) is None, "read returns None after delete")
    check(list(r.scan()) == [1001], "scan skips the deleted index")

    r.close()

    with rigel.Rigel(off_dir) as r2:
        check(r2.read(1001) == b"Y" * 8, "reopening the directory reads back prior writes")

    # read_only, and auto-detected read-only: chmod the shard/index files
    # themselves (not just the directory - opening an already-existing
    # file only needs permission on the file, not on its containing
    # directory) to prove this - and the automatic EACCES/EROFS fallback
    # that makes it unnecessary for plain reads - actually need no write
    # access, e.g. against a read-only NFS export.
    shard_path = os.path.join(off_dir, "py.0000")
    index_path = os.path.join(off_dir, "py.index")
    os.chmod(shard_path, 0o444)
    os.chmod(index_path, 0o444)
    try:
        with rigel.Rigel(off_dir) as r3:
            check(not r3.read_only, "read_only is False before the first read attempt")
            check(r3.read(1001) == b"Y" * 8,
                  "read without read_only still succeeds (auto-detected fallback)")
            check(r3.read_only, "read_only is True after the fallback triggered")
            check(r3.write(1001, b"Z" * 8) == -1, "write fails after auto-detected read_only")

        # write() called first (no prior read) must not trigger the
        # fallback - it should fail with a plain permission error instead
        # of silently switching modes and then crashing on a PROT_READ
        # mapping.
        with rigel.Rigel(off_dir) as r4:
            check(r4.write(1001, b"Z" * 8) == -1,
                  "write called first (no prior read) still fails, not falls back")
            check(not r4.read_only, "read_only stays False - write never auto-detects")

        with rigel.Rigel(off_dir, read_only=True) as rro:
            check(rro.read_only, "read_only reads back True")
            check(rro.read(1001) == b"Y" * 8,
                  "read_only=True succeeds against files with no write permission")
            check(rro.write(1001, b"Z" * 8) == -1, "write fails on a read_only instance")
            check(rro.delete(1001) is False, "delete fails on a read_only instance")
            check(list(rro.scan()) == [1001], "scan works on a read_only instance")
    finally:
        os.chmod(shard_path, 0o644)
        os.chmod(index_path, 0o644)

    try:
        rigel.Rigel("/tmp/rigel_test_python_nonexistent")
        check(False, "Rigel(dirname) raises RigelError for a missing rigel.meta")
    except rigel.RigelError:
        check(True, "Rigel(dirname) raises RigelError for a missing rigel.meta")

    if fail == 0:
        print("All tests passed")
    else:
        print("%d test(s) failed" % fail)
    sys.exit(0 if fail == 0 else 1)


if __name__ == "__main__":
    main()
