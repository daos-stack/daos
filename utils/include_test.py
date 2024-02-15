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


E_SYSTEM_HEADERS = ("float.h", "stdarg.h", "stdbool.h", "stddef.h")


def check_paths(src_dir, src_file):
    """Checks a single source file for the order in which it includes headers

    This is loosely based on the below.  The logic is to first include any headers in blocks
    ordered by where the header is located:

    * Headers in the same directory
    * Internal headers from elsewhere in the source
    * System/installed library headers.
    * Headers from non-os packages that DAOS uses
    * External daos headers

    https://google.github.io/styleguide/cppguide.html#Names_and_Order_of_Includes
    """

    # pylint: disable=too-many-branches

    # Headers which come from /usr/include
    h_sys = set()
    # Headers which come from 3rd party libraries
    h_local = set()
    # Public daos headers
    h_daos = set()
    # Internal daos headers
    h_internal = set()
    # Internal local headers
    h_dir = set()

    fixup_src = True
    last_pre_blank = None
    first_post_line = None
    have_headers = False
    finished_headers = False
    can_fixup = True

    with open(os.path.join(src_dir, src_file)) as fd:
        idx = 0
        for linef in fd.readlines():
            idx += 1

            line = linef.rstrip()

            if line == "":
                # There is a bug here where headers do not have a blank line before them.
                if not have_headers:
                    last_pre_blank = idx
                continue

            if not line.startswith("#include"):
                if have_headers:
                    if first_post_line is None:
                        first_post_line = idx - 1
                    finished_headers = True
                continue

            have_headers = True
            if last_pre_blank is None:
                last_pre_blank = idx

            if finished_headers:
                can_fixup = False

            _, header = line.split(" ", maxsplit=1)
            fname = header[1:-1]
            brace_include = header[0] == "<"
            if brace_include:
                if fname in E_SYSTEM_HEADERS:
                    h_sys.add(fname)
                    continue
                if os.path.exists(f"/usr/include/{fname}"):
                    h_sys.add(fname)
                    continue
                found = False
                for dep in os.listdir("install/prereq/release/"):
                    if os.path.exists(f"install/prereq/release/{dep}/include/{fname}"):
                        h_local.add(fname)
                        found = True
                        break
                if found:
                    continue
            else:
                if os.path.exists(f"{src_dir}/{fname}"):
                    if "/" in fname:
                        h_internal.add(fname)
                    else:
                        h_dir.add(fname)
                    continue
            if os.path.exists(f"src/include/{fname}"):
                if (
                    "/" not in fname
                    or fname == "daos/tse.h"
                    or fname.startswith("gurt/")
                    or fname.startswith("cart")
                ):
                    h_daos.add(fname)
                else:
                    h_internal.add(fname)
                continue

            # This is a hack but needed for now at least.
            if fname.endswith("pb-c.h"):
                h_internal.add(fname)
                continue

            print(f"Unable to find location of {fname}")
            can_fixup = False

    def set_to_txt(hlist, public=False):
        """Return the text to include a set of headers, or None"""

        lines = []
        for header in sorted(hlist):
            if public:
                lines.append(f"#include <{header}>")
            else:
                lines.append(f'#include "{header}"')
        return "\n".join(lines)

    hblobs = []
    if h_dir:
        hblobs.append(set_to_txt(h_dir))

    if h_internal:
        hblobs.append(set_to_txt(h_internal))

    if h_sys:
        hblobs.append(set_to_txt(h_sys, public=True))

    if h_local:
        hblobs.append(set_to_txt(h_local, public=True))

    if h_daos:
        hblobs.append(set_to_txt(h_daos, public=True))

    if not hblobs:
        return

    header_text = "\n\n".join(hblobs)

    if not can_fixup:
        print(f"File {src_file} cannot be fixed")
        return

    if not fixup_src:
        return

    print(f"Re-writing {src_file}")
    tname = None
    with open(os.path.join(src_dir, src_file)) as fd:
        with tempfile.NamedTemporaryFile(
            dir=src_dir, prefix=src_file, delete=False
        ) as tmp:
            tname = tmp.name
            idx = 0
            for line in fd.readlines():
                idx += 1
                if idx == last_pre_blank:
                    tmp.write("\n".encode())
                    tmp.write(header_text.encode())
                    tmp.write("\n".encode())
                    continue
                if last_pre_blank < idx < first_post_line:
                    continue
                tmp.write(line.encode())
    os.rename(tname, os.path.join(src_dir, src_file))


def check_paths_dir(src_dir):
    """Check all headers in a dir"""
    for fname in os.listdir(src_dir):
        if not fname.endswith(".c") and not fname.endswith(".h"):
            continue
        check_paths(src_dir, fname)


def main():
    """Check the whole tree"""

    check_paths_dir("src/cart")
    check_paths_dir("src/gurt")
    check_paths_dir("src/include/daos")

    with open(".build_vars.json", "r") as ofh:
        bv = json.load(ofh)

    include_dir = os.path.join(bv["PREFIX"], "include")

    check_dir(include_dir, None)
    os.unlink("a.out")


if __name__ == "__main__":
    main()
