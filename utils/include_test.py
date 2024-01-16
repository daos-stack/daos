#!/usr/bin/env python
"""Check that all daos headers can be included stand-alone"""

import os
import subprocess
import tempfile

INCLUDE_DIR = "install/include"


def check_dir(sub_dir):
    """Check all files in one directory"""

    h_dir = INCLUDE_DIR

    if sub_dir:
        h_dir = os.path.join(INCLUDE_DIR, sub_dir)
    for entry in sorted(os.listdir(h_dir)):
        if not os.path.isfile(os.path.join(h_dir, entry)):
            check_dir(entry)
            continue
        with tempfile.NamedTemporaryFile(suffix=".c", mode="w+t") as tf:
            header = entry
            if sub_dir:
                header = os.path.join(sub_dir, entry)
            tf.write(f"#include <{header}>\n")
            tf.write("int main() {return 0;}")
            tf.flush()
            print(f"Checking {header}")
            subprocess.run(["gcc", "-I", INCLUDE_DIR, tf.name], check=True)
            print(f"Header file {header} is OK.")


def main():
    """Check the whole tree"""
    check_dir(None)
    os.unlink("a.out")


if __name__ == "__main__":
    main()
