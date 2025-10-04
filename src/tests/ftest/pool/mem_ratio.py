"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json

from apricot import TestWithServers
from general_utils import bytes_to_human, list_to_str, report_errors
from test_utils_pool import add_pools, get_pool_create_percentages


class MemRatioTest(TestWithServers):
    """Test the dmg pool create --mem_ratio argument.

    :avocado: recursive
    """

    def check_insufficient_size(self, error):
        """Check for an insufficient pool size error during pool creation.

        Args:
            error (Exception): the error raised during pool creation
        """
        pattern = "(Insufficient scm size|No space on storage target)"
        self.log.debug("Verifying Pool creation failure: %s", error)
        result = self.server_managers[0].search_engine_logs(pattern)
        if not result.passed:
            raise error
        self.log.debug("Pool failure expected due to: '%s'", pattern)

    @staticmethod
    def readable_bytes(size):
        """Get a display string for a bytes value.

        Args:
            size (int): the size in bytes

        Returns:
            str: bytes displayed as human readable with the original value
        """
        return f"{bytes_to_human(int(size))} ({size})"

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
        _pool_size_percent = self.params.get("pool_size_percent", "/run/*")
        _sizes = get_pool_create_percentages(5, _pool_size_percent)

        dmg = self.get_dmg_command()
        kwargs_list = [{"test": self, "dmg": dmg.copy(), "size": _sizes[0]}]
        if self.server_managers[0].manager.job.using_control_metadata:
            # Additional pools for MD on SSD
            _ratios = [
                100,
                self.random.randint(76, 99),
                self.random.randint(51, 75),
                self.random.randint(26, 50),
                self.random.randint(10, 25)]     # Limit smallest mem-ratio sizes due to DAOS-18004
            kwargs_list[0]["mem_ratio"] = _ratios[0]
            for index in range(1, 5):
                kwargs_list.append({
                    "test": self,
                    "dmg": dmg.copy(),
                    "size": _sizes[index],
                    "mem_ratio": _ratios[index]})

        # Create pools with different --mem_ratio arguments
        self.log_step(f"Creating {len(kwargs_list)} pool(s)")
        pools = add_pools(dmg, kwargs_list, error_handler=self.check_insufficient_size)
        if len(kwargs_list) > 1 and len(pools) < 4:
            self.fail("Test failed to create a minimum of 4 pools with various mem-ratios")

        # Collect the pool create output values
        data = {}
        errors = []
        for pool in pools:
            name = str(pool)
            _result = json.loads(pool.dmg.result.stdout)
            try:
                data[name] = {
                    "mem-ratio": pool.mem_ratio.value,
                    "size": pool.size.value,
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
            except (IndexError, KeyError, TypeError, ValueError):
                # Unexpected response from pool create
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
                data[name]["tier_stats(query)"] = {}
                for item in query["response"]["tier_stats"]:
                    data[name]["tier_stats(query)"][item["media_type"]] = item["total"]
                data[name]["mem_file_bytes(query)"] = query["response"]["mem_file_bytes"]
                _ratio = (
                    int(data[name]["mem_file_bytes(query)"])
                    / int(data[name]["tier_stats(query)"]["scm"]))
                data[name]["query_ratio"] = round(_ratio * 100)
                _difference = abs(data[name]["mem-ratio"] - data[name]["query_ratio"])
                if data[name]["mem-ratio"] and _difference > 1:
                    errors.append(
                        f"{name} - Mem ratio ({data[name]['mem-ratio']}) differs from pool "
                        f"query ({data[name]['query_ratio']}) by {_difference}")
            except (IndexError, KeyError, TypeError, ValueError):
                data[name]["total_engines"] = "<ERROR>"
                data[name]["tier_stats(query)"] = "<ERROR>"
                data[name]["mem_file_bytes(query)"] = "<ERROR>"
                data[name]["query_ratio"] = 0
                errors.append(f"{name} - Invalid dmg pool query response: {query}")

        # Report the test results
        if not data:
            self.fail(f"Error collecting data from {len(pools)} pool(s)")
        _format = "%-54s  %-9s  %-40s  %-14s  %-12s  %-13s  %-50s  %-21s  %s"
        _keys = ["Pool",
                 "mem-ratio",
                 "tier_bytes",
                 "mem_file_bytes",
                 "create_ratio",
                 "total_engines",
                 "tier_stats(query)",
                 "mem_file_bytes(query)",
                 "query_ratio"]
        self.log.debug(_format, *_keys)
        self.log.debug(
            _format, "-" * 54, "-" * 9, "-" * 40, "-" * 14, "-" * 12, "-" * 13, "-" * 50, "-" * 21,
            "-" * 11)
        for name, info in data.items():
            items = [name]
            for key in _keys[1:]:
                if isinstance(info[key], list):
                    _display = [self.readable_bytes(value) for value in info[key]]
                    items.append(list_to_str(_display, ", "))
                elif isinstance(info[key], dict):
                    _display = [
                        f"{key}: {self.readable_bytes(value)}" for key, value in info[key].items()]
                    items.append(list_to_str(_display, ", "))
                elif "mem_file_bytes" in name:
                    items.append(self.readable_bytes(info[key]))
                else:
                    items.append(str(info[key]))
            self.log.debug(_format, *items)

        report_errors(self, errors)
        self.log.info("Test passed")
