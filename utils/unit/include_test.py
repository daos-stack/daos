#!/usr/bin/env python3
"""Check that all daos headers can be included stand-alone"""

import json
import os
import subprocess  # nosec
import tempfile


def check_dir(include_dir, sub_dir):
    """Check all files in one directory"""

    h_dir = include_dir

    if sub_dir:
        h_dir = os.path.join(include_dir, sub_dir)
    for entry in sorted(os.listdir(h_dir)):
        if os.path.isdir(os.path.join(h_dir, entry)):
            check_dir(include_dir, entry)
            continue
        with tempfile.NamedTemporaryFile(suffix=".c", mode="w+t") as tf:
            header = entry
            if sub_dir:
                header = os.path.join(sub_dir, entry)
            tf.write(f"#include <{header}>\n")
            tf.write("int main() {return 0;}")
            tf.flush()
            print(f"Checking {header}")
            subprocess.run(["gcc", "-I", include_dir, tf.name], check=True)
            print(f"Header file {header} is OK.")


def main():
    """Check the whole tree"""

    with open(".build_vars.json", "r") as ofh:
        bv = json.load(ofh)

    include_dir = os.path.join(bv["PREFIX"], "include")

    check_dir(include_dir, None)
    os.unlink("a.out")


if __name__ == "__main__":
    main()
