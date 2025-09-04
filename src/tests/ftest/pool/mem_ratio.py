"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json

from apricot import TestWithServers
from general_utils import report_errors
from test_utils_pool import add_pools


class MemRatioTest(TestWithServers):
    """Test the dmg pool create --mem_ratio argument.

    :avocado: recursive
    """

    def test_mem_ratio(self):
        """Create multiple pools using different --mem_ratio arguments to define which fraction
        of meta blob size is used for the memory file size in each pool.

        Test steps:
        1. Define a list of mem ratio percentages to use to create pools
            a. For PMEM do not specify a --mem_ratio argument (not supported)
            b. For MD on SSD define 5 pools: 1-25%, 26-50%, 51-75%, 76-99%, and 100%
        2. Create a pool for each mem ratio percentage
            a. Verify the listed metadata storage and memory file sizes match the --mem_ratio
        3. Query the pools
            a. Verify the listed metadata storage and memory file sizes match the --mem_ratio

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=MemRatioTest,test_mem_ratio
        """
        dmg = self.get_dmg_command()
        kwargs_list = [{"test": self, "dmg": dmg.copy()}]
        if self.server_managers[0].manager.job.using_control_metadata:
            # Additional pools for MD on SSD
            kwargs_list[0]["mem_ratio"] = 100
            kwargs_list.append(
                {"test": self, "dmg": dmg.copy(), "mem_ratio": self.random.randint(76, 99)})
            kwargs_list.append(
                {"test": self, "dmg": dmg.copy(), "mem_ratio": self.random.randint(51, 75)})
            kwargs_list.append(
                {"test": self, "dmg": dmg.copy(), "mem_ratio": self.random.randint(26, 50)})
            kwargs_list.append(
                {"test": self, "dmg": dmg.copy(), "mem_ratio": self.random.randint(1, 25)})

        # Create pools with different --mem_ratio arguments
        self.log_step(f"Creating {len(kwargs_list)} pool(s)")
        pools = add_pools(kwargs_list)

        # Collect the pool create output values
        _format = "  %-60s  %-8s  %-34s  %-16s  %-13s  %-44s  %-16s  %s"
        _keys = ["Pool",
                 "mem-ratio",
                 "tier_bytes",
                 "mem_file_bytes",
                 "create_ratio"
                 "total_engines",
                 "tier_stats(total)",
                 "mem_file_bytes",
                 "query_ratio"]
        summary = []
        errors = []
        for pool in pools:
            summary.append({})
            summary[-1][_keys[0]] = str(pool)
            summary[-1][_keys[1]] = pool.mem_ratio.value
            _result = json.loads(pool.dmg.result.stdout)
            try:
                summary[-1][_keys[2]] = _result["response"]["tier_bytes"]
                summary[-1][_keys[3]] = _result["response"]["mem_file_bytes"]
                summary[-1][_keys[4]] = round(
                    int(summary[-1][_keys[3]]) / int(summary[-1][_keys[2]][0]) * 100)
                _difference = abs(summary[-1][_keys[1]] - summary[-1][_keys[4]])
                if summary[-1][_keys[1]] and _difference > 1:
                    errors.append(
                        f"{str(pool)} - Mem ratio ({summary[-1][_keys[1]]}) differs from pool "
                        f"create ({summary[-1][_keys[4]]}) by {_difference}")
            except (KeyError, IndexError):
                summary[-1][_keys[2]] = "<ERROR>"
                summary[-1][_keys[3]] = "<ERROR>"
                summary[-1][_keys[4]] = 0
                errors.append(f"{str(pool)} - Invalid dmg pool create response: {_result}")

        # Verify the pool blob and memory file sizes align with the requested mem ratio
        self.log_step(f"Query the {len(pools)} pool(s)")
        pool_queries = []
        for pool in pools:
            pool_queries.append(dmg.pool_query(pool.identifier))

        # Collect the pool query output values
        for index, pool_query in enumerate(pool_queries):
            try:
                summary[index][_keys[5]] = pool_query["response"]["total_engines"]
                summary[index][_keys[6]] = {}
                for data in pool_query["response"]["tier_stats"]:
                    summary[index][_keys[6]][data["media_type"]] = data["total"]
                summary[index][_keys[7]] = pool_query["response"]["mem_file_bytes"]
                # (mem_file_bytes / "tier_stats-scm-total) * 100
                summary[index][_keys[8]] = round(
                    int(summary[index][_keys[7]]) / int(summary[index][_keys[6]]["scm"]) * 100)
                _difference = abs(summary[index][_keys[1]] - summary[index][_keys[8]])
                if summary[-1][_keys[1]] and _difference > 1:
                    errors.append(
                        f"{str(pool)} - Mem ratio ({summary[-1][_keys[1]]}) differs from pool "
                        f"query ({summary[-1][_keys[4]]}) by {_difference}")
            except (KeyError, IndexError):
                summary[index][_keys[5]] = "<ERROR>"
                summary[index][_keys[6]] = "<ERROR>"
                summary[index][_keys[7]] = "<ERROR>"
                summary[index][_keys[8]] = 0
                errors.append(f"{str(pool)} - Invalid dmg pool query response: {pool_query}")

        # Report the test results
        self.log.debug(_format, *_keys)
        self.log.debug(
            _format, "-" * 60, "-" * 8, "-" * 34, "-" * 16, "-" * 13, "-" * 44, "-" * 16, "-" * 7)
        for data in summary:
            items = []
            for key in _keys:
                if isinstance(data[key], list):
                    items.append(", ".join(data[key]))
                elif isinstance(data[key], dict):
                    items.append(", ".join(f"{k}: {v}" for k, v in data[key].items()))
                else:
                    items.append(str(data[key]))
            self.log.debug(_format, *items)

        report_errors(self, errors)
        self.log.info("Test passed")
