#!/usr/bin/env python3
"""Copy a Spriggit YAML tree so that directory enumeration returns entries in
case-insensitive alphabetical order (what NTFS gives Spriggit on Windows).

tmpfs (e.g. /dev/shm) lists directory entries newest-first, so entries are created
in reverse sorted order. Used by spriggit.sh for a byte-exact round-trip check."""
import os
import shutil
import sys


def key(name: str) -> str:
    return name.upper()


def copy(src: str, dst: str) -> None:
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src), key=key, reverse=True):
        s = os.path.join(src, name)
        d = os.path.join(dst, name)
        if os.path.isdir(s):
            copy(s, d)
        else:
            shutil.copyfile(s, d)


def ordered(path: str) -> bool:
    for root, dirs, files in os.walk(path):
        names = os.listdir(root)
        if names != sorted(names, key=key):
            return False
    return True


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    copy(src, dst)
    sys.exit(0 if ordered(dst) else 3)
