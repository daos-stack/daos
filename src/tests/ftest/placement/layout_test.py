"""
(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re
import shutil
import subprocess  # nosec

from avocado import Test

# Resolve the repo root at import time, while CWD is still the source tree.
# This file lives at  <repo>/src/tests/ftest/placement/layout_test.py
# so four levels up is the repo root.
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_THIS_DIR, "..", "..", "..", ".."))


class LayoutTest(Test):
    """Placement layout computation performance test.

    Standalone test using plain avocado.Test — no DAOS server/agent
    infrastructure required.  Runs the layout_test binary directly and
    parses per-phase timing output.

    Binary search order:
      1. DAOS_TEST_PREFIX environment variable  (e.g. /path/to/install)
      2. Sibling install/ directory relative to the repo root
      3. PATH

    :avocado: recursive
    """

    # ------------------------------------------------------------------
    # helpers
    # ------------------------------------------------------------------

    def _find_binary(self):
        """Locate the layout_test binary.

        Returns:
            str: absolute path to layout_test
        """
        candidates = []

        prefix = os.environ.get("DAOS_TEST_PREFIX")
        if prefix:
            candidates.append(os.path.join(prefix, "bin", "layout_test"))

        # repo-relative: resolved at import time before avocado changes CWD
        candidates.append(os.path.join(_REPO_ROOT, "install", "bin", "layout_test"))

        path_binary = shutil.which("layout_test")
        if path_binary:
            candidates.append(path_binary)

        for path in candidates:
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return path

        self.cancel(
            "layout_test binary not found. "
            "Set DAOS_TEST_PREFIX or add install/bin to PATH."
        )
        return None

    # pylint: disable=too-many-arguments, too-many-positional-arguments
    def _run_binary(
        self, binary, nodes, ranks, targets, object_class, obj_count, operation
    ):
        """Run layout_test and return stdout.

        Args:
            binary (str): path to layout_test
            nodes (int): number of nodes
            ranks (int): ranks per node
            targets (int): targets per rank
            object_class (str): DAOS object class name
            obj_count (int): number of OIDs
            operation (str): operation sequence string

        Returns:
            str: combined stdout from the process
        """
        cmd = [
            binary,
            "--nodes",
            str(nodes),
            "--ranks",
            str(ranks),
            "--targets",
            str(targets),
            "--class",
            object_class,
            "--obj-count",
            str(obj_count),
            "--operation",
            operation,
        ]

        self.log.info("Running: %s", " ".join(cmd))

        try:
            proc = subprocess.run(  # nosec
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=self.timeout,
                check=False,
                encoding="utf-8",
            )
        except subprocess.TimeoutExpired:
            self.fail(f"layout_test timed out after {self.timeout}s")

        output = proc.stdout or ""
        self.log.info(output)

        if proc.returncode != 0:
            self.fail(
                f"layout_test exited with code {proc.returncode}.\n"
                f"Output:\n{output}"
            )

        return output

    # pylint: disable=too-many-locals
    def _report_performance(
        self, output, nodes, ranks, targets, object_class, obj_count, operation
    ):
        """Parse layout_test stdout and log structured performance data.

        Lines of interest emitted by layout_test for each operation::

            Performance (pl_obj_place / pl_obj_find_rebuild only):
              pl_obj_place   (initial) :    45.123 ms
              pl_obj_find_rebuild      :   120.456 ms
              pl_obj_place   (post-op) :    44.789 ms
              Total                    :   210.368 ms

        Args:
            output (str): stdout text from layout_test
            nodes (int): node count
            ranks (int): ranks per node
            targets (int): targets per rank
            object_class (str): DAOS object class name
            obj_count (int): number of OIDs exercised
            operation (str): operation sequence string
        """
        self.log.info("=" * 70)
        self.log.info("LAYOUT PERFORMANCE SUMMARY")
        self.log.info(
            "  Topology : %d nodes x %d ranks x %d targets", nodes, ranks, targets
        )
        self.log.info("  Class    : %-12s  OIDs: %d", object_class, obj_count)
        self.log.info("  Ops      : %s", operation)
        self.log.info("-" * 70)

        current_op = None
        in_perf = False
        found_any = False

        phase_pattern = re.compile(
            r"^(pl_obj_place\s+\(initial\)|pl_obj_find_rebuild"
            r"|pl_obj_place\s+\(post-op\)|Total)"
            r"\s*:\s*([\d.]+)\s*ms$"
        )

        op_pattern = re.compile(
            r"^\[(\d+)/(\d+)\]\s+Running\s+(\S+)\s+(\S+)\s+operation$"
        )

        for line in output.splitlines():
            stripped = line.strip()

            op_m = op_pattern.match(stripped)
            if op_m:
                current_op = f"{op_m.group(3)} {op_m.group(4)}"
                in_perf = False
                continue

            if "Performance (pl_obj_place / pl_obj_find_rebuild only)" in stripped:
                in_perf = True
                continue

            if in_perf:
                m = phase_pattern.match(stripped)
                if m:
                    label = m.group(1)
                    ms = float(m.group(2))
                    self.log.info(
                        "  [%-22s] %-30s: %10.3f ms", current_op or "?", label, ms
                    )
                    found_any = True
                elif stripped:
                    in_perf = False

        self.log.info("=" * 70)

        if not found_any:
            self.fail(
                "No performance data found in layout_test output. "
                "Ensure the binary was built with timing support."
            )

    # ------------------------------------------------------------------
    # test method
    # ------------------------------------------------------------------

    def test_layout_performance(self):
        """Run layout_test and report per-phase placement timing.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=placement,layout
        :avocado: tags=LayoutTest,test_layout_performance
        """
        nodes = self.params.get("nodes", "/run/*", 32)
        ranks = self.params.get("ranks", "/run/*", 1)
        targets = self.params.get("targets", "/run/*", 8)
        obj_count = self.params.get("obj_count", "/run/*", 100000)
        object_class = self.params.get("object_class", "/run/object_class/*", "RP_3G1")
        operation = self.params.get(
            "operation", "/run/*", "exclude node=[0],reint node=[0]"
        )

        binary = self._find_binary()

        output = self._run_binary(
            binary, nodes, ranks, targets, object_class, obj_count, operation
        )

        self._report_performance(
            output, nodes, ranks, targets, object_class, obj_count, operation
        )
