#!/usr/bin/env python3
"""Check that all daos headers can be included stand-alone"""

import argparse
import json
import os
import subprocess  # nosec
import tempfile

# pylint: disable-wrong-spelling-in-comment


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


E_SYSTEM_HEADERS = (
    "float.h",
    "stdarg.h",
    "stdbool.h",
    "stddef.h",
    "stdatomic.h",
    "math.h",
)
E_LOCAL_HEADERS = (
    "cmocka.h",
    "mpi.h",
    "lustre/lustreapi.h",
    "linux/lustre/lustre_idl.h",
    "qat.h",
    "Python.h",
    "jni.h",
    "nvme_internal.h",
    "nvme_control_common.h",
    "nvme_control.h",
    "cpa.h",
    "cpa_dc.h",
    "icp_sal_user.h",
    "icp_sal_poll.h",
    "qae_mem.h",
)
E_INTERNAL_HEADERS = (
    "daos_test.h",
    "crt_utils.h",
    "crt_internal.h",
    "object/rpc_csum.h",
    "daos_iotest.h",
    "dfs_internal.h",
    "daos_jni_common.h",
    "io_daos_DaosClient.h",
    "io_daos_dfs_DaosFsClient.h",
    "dfuse_common.h",
    "dfuse.h",
    "dfuse_log.h",
    "dfuse_vector.h",
    "vos_internal.h",
    "raft.h",
    "utest_common.h",
    "dfuse_vector.h",
    "vos_internal.h",
    "object/rpc_csum.h",
    "daos_hdlr.h",
    "evt_priv.h",
    "vos_layout.h",
    "vos_ts.h",
    "vos_obj.h",
)


class OrderChecker:
    """Check the ordering of header files in the source tree"""

    def __init__(self, build_vars):
        self.fix = False

        self._system_headers = []
        output = subprocess.run(
            ["rpm", "-ql", "glibc-headers"], capture_output=True, check=True
        )
        self._system_headers = []
        for sh in output.stdout.decode().splitlines():
            self._system_headers.append(sh[13:])
        self._component_dirs = []
        for component, val in build_vars.items():
            if not component.endswith("_PREFIX"):
                continue
            if val == "/usr":
                continue
            c_dir = os.path.join(val, "include")
            if os.path.isdir(c_dir):
                self._component_dirs.append(c_dir)

    def check_paths(self, src_dir, src_file):
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

        # Headers which come from glibc-headers
        h_core_sys = set()
        # Headers which come from /usr/include
        h_sys = set()
        # Headers which come from 3rd party libraries
        h_local = set()
        # Public daos headers
        h_daos = set()
        # Internal daos headers
        h_internal = set()
        # Internal local headers (from the same directory)
        h_dir = set()

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
                    if not have_headers:
                        last_pre_blank = idx
                    continue

                if not line.startswith("#include"):
                    if have_headers:
                        if first_post_line is None:
                            first_post_line = idx - 1
                        finished_headers = True
                    else:
                        last_pre_blank = idx + 1
                    continue

                have_headers = True
                if last_pre_blank is None:
                    last_pre_blank = idx

                if finished_headers:
                    can_fixup = False

                _, header = line.split(" ", maxsplit=1)
                fname = header[1:-1]
                brace_include = header[0] == "<"
                if fname in E_SYSTEM_HEADERS:
                    h_core_sys.add(fname)
                    continue
                if fname in self._system_headers:
                    h_core_sys.add(fname)
                    continue
                if fname in E_LOCAL_HEADERS:
                    h_local.add(fname)
                    continue

                if brace_include:
                    if os.path.exists(f"/usr/include/{fname}"):
                        if fname.startswith("linux"):
                            h_core_sys.add(fname)
                        else:
                            h_sys.add(fname)
                        continue
                    found = False
                    for dep in self._component_dirs:
                        if os.path.exists(os.path.join(dep, fname)):
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

                if fname in E_INTERNAL_HEADERS:
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

        # Hack for debug headers, if required this needs to come before other headers.
        # Needed for RPC definitions
        for head in ("daos/debug", "crt_utils", "crt_internal"):
            if f"{head}.h" in h_internal:
                hblobs.append(f'#include "{head}.h"')
                h_internal.remove(f"{head}.h")

        if h_dir:
            hblobs.append(set_to_txt(h_dir))

        if h_internal:
            hblobs.append(set_to_txt(h_internal))

        if h_core_sys:
            hblobs.append(set_to_txt(h_core_sys, public=True))

        if h_sys:
            hblobs.append(set_to_txt(h_sys, public=True))

        if h_local:
            hblobs.append(set_to_txt(h_local, public=True))

        if h_daos:
            hblobs.append(set_to_txt(h_daos, public=True))

        header_text = "\n\n".join(hblobs)

        if not can_fixup:
            print(f"File {src_dir}/{src_file} cannot be fixed")
            # print("Header text would be")
            # print(header_text)
            return

        if not hblobs:
            return

        # print(f"Re-writing {src_file} from {last_pre_blank} to {first_post_line}")

        if not self.fix:
            return

        # print(f"Re-writing {src_file}")
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

    def check_paths_dir(self, src_dir):
        """Check all headers in a dir"""

        if src_dir == "src/rdb/raft":
            return
        for entry in os.listdir(src_dir):
            if os.path.isdir(os.path.join(src_dir, entry)):
                self.check_paths_dir(os.path.join(src_dir, entry))
                continue
            if not entry.endswith(".c") and not entry.endswith(".h"):
                continue
            self.check_paths(src_dir, entry)


# Commits can then be patched with
# git diff master... --name-only | xargs ./utils/include_test.py --fix --check-file


def main():
    """Check the whole tree"""

    with open(".build_vars.json", "r") as ofh:
        bv = json.load(ofh)

    parser = argparse.ArgumentParser()
    parser.add_argument("--check-include-order", action="store_true")
    parser.add_argument("--check-file", nargs="*")
    parser.add_argument("--fix", action="store_true")
    args = parser.parse_args()

    if args.check_file:
        oc = OrderChecker(bv)
        if args.fix:
            oc.fix = True
        for fname in args.check_file:
            oc.check_paths(os.path.dirname(fname), os.path.basename(fname))
        return

    if args.check_include_order:
        oc = OrderChecker(bv)
        if args.fix:
            oc.fix = True
        oc.check_paths_dir("src")

    include_dir = os.path.join(bv["PREFIX"], "include")

    check_dir(include_dir, None)
    os.unlink("a.out")


if __name__ == "__main__":
    main()
