"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json

from apricot import TestWithServers
from general_utils import dict_to_str, list_to_str, report_errors
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
        data = {}
        errors = []
        for pool in pools:
            name = str(pool)
            _result = json.loads(pool.dmg.result.stdout)
            try:
                data[name] = {
                    "mem-ratio": pool.mem_ratio.value,
                    "tier_bytes": _result["response"]["tier_bytes"],
                    "mem_file_bytes": _result["response"]["mem_file_bytes"],
                }
                data[name]["create_ratio"] = round(
                    int(data[name]["mem_file_bytes"]) / int(data[name]["tier_bytes"][0]) * 100)
                _difference = abs(data[name]["mem-ratio"] - data[name]["create_ratio"])
                if data[name]["mem-ratio"] and _difference > 1:
                    errors.append(
                        f"{name} - Mem ratio ({data[name]['mem-ratio']}) differs from pool "
                        f"create ({data[name]['create_ratio']}) by {_difference}")
            except (KeyError, IndexError):
                data[name] = {
                    "mem-ratio": pool.mem_ratio.value,
                    "tier_bytes": "<ERROR>",
                    "mem_file_bytes": "<ERROR>",
                    "create_ratio": 0
                }
                errors.append(f"{name} - Invalid dmg pool create response: {_result}")

        # Verify the pool blob and memory file sizes align with the requested mem ratio
        self.log_step(f"Query the {len(pools)} pool(s)")
        pool_queries = {}
        for pool in pools:
            pool_queries[str(pool)] = dmg.pool_query(pool.identifier)

        # Collect the pool query output values
        for name, query in pool_queries.items():
            try:
                data[name]["total_engines"] = query["response"]["total_engines"]
                data[name]["tier_stats"] = {}
                for item in query["response"]["tier_stats"]:
                    data[name]["tier_stats"][item["media_type"]] = item["total"]
                data[name]["mem_file_bytes"] = query["response"]["mem_file_bytes"]
                data[name]["query_ratio"] = round(
                    int(data[name]["mem_file_bytes"]) / int(data[name]["tier_stats"]["scm"]) * 100)
                _difference = abs(data[name]["mem-ratio"] - data[name]["query_ratio"])
                if data[name]["mem-ratio"] and _difference > 1:
                    errors.append(
                        f"{name} - Mem ratio ({data[name]['mem-ratio']}) differs from pool "
                        f"query ({data[name]['query_ratio']}) by {_difference}")
            except (KeyError, IndexError):
                data[name]["total_engines"] = "<ERROR>"
                data[name]["tier_stats"] = "<ERROR>"
                data[name]["mem_file_bytes"] = "<ERROR>"
                data[name]["query_ratio"] = 0
                errors.append(f"{name} - Invalid dmg pool query response: {query}")

        # Report the test results
        _format = "  %-60s  %-8s  %-34s  %-16s  %-13s  %-44s  %-16s  %s"
        _keys = ["Pool",
                 "mem-ratio",
                 "tier_bytes",
                 "mem_file_bytes",
                 "create_ratio",
                 "total_engines",
                 "tier_stats(total)",
                 "mem_file_bytes",
                 "query_ratio"]
        self.log.debug(_format, *_keys)
        self.log.debug(
            _format, "-" * 60, "-" * 8, "-" * 34, "-" * 16, "-" * 13, "-" * 44, "-" * 16, "-" * 7)
        for name, info in data.items():
            items = [name]
            for key in _keys[1:]:
                if isinstance(info[key], list):
                    items.append(list_to_str(info[key], ", "))
                elif isinstance(info[key], dict):
                    items.append(dict_to_str(info[key], ", ", ": "))
                else:
                    items.append(str(info[key]))
            self.log.debug(_format, *items)

        report_errors(self, errors)
        self.log.info("Test passed")
