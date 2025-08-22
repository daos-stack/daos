"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json

from apricot import TestWithServers
from general_utils import report_errors


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
        kwargs_list = [{}]
        if self.server_managers[0].manager.job.using_control_metadata:
            # Additional pools for MD on SSD
            kwargs_list = [{"mem_ratio": 100}]
            kwargs_list.append({"mem_ratio": self.random.randint(76, 99)})
            kwargs_list.append({"mem_ratio": self.random.randint(51, 75)})
            kwargs_list.append({"mem_ratio": self.random.randint(26, 50)})
            kwargs_list.append({"mem_ratio": self.random.randint(1, 25)})

        # Create pools with different --mem_ratio arguments
        self.log_step(f"Creating {len(kwargs_list)} pool(s)")
        pools = []
        for kwargs in kwargs_list:
            pools.append(self.get_pool(dmg=dmg.copy(), **kwargs))

        # Verify pool create output
        self.log_step(f"Verifying {len(pools)} pool create responses")
        _format = "  %-60s  %-8s  %-16s  %-16s  %s"
        self.log.debug(_format, "Pool", "MemRatio", "Metadata Storage", "Memory File Size", "Ratio")
        self.log.debug(_format, "-" * 60, "-" * 8, "-" * 16, "-" * 16, "-" * 7)
        errors = []
        for pool in pools:
            mem_ratio = pool.mem_ratio.value
            _metadata = _memfile = "<ERROR>"
            result = json.loads(pool.dmg.result.stdout)
            try:
                _metadata = result["response"]["tier_bytes"][0]
                _memfile = result["response"]["mem_file_bytes"]
            except (KeyError, IndexError) as error:
                self.log.debug(_format, str(pool), mem_ratio, _metadata, _memfile, error)
                errors.append(f"{str(pool)} - Invalid dmg pool create response")
                continue
            actual = round(int(_memfile) / int(_metadata) * 100)
            self.log.debug(_format, str(pool), mem_ratio, _metadata, _memfile, actual)
            if mem_ratio and actual != mem_ratio:
                errors.append(
                    f"{str(pool)} - Actual mem ratio ({actual}) does not match specified ratio "
                    f"({mem_ratio}) in pool create output")

        # Verify the pool blob and memory file sizes align with the requested mem ratio
        self.log_step(f"Query the {len(pools)} pool(s)")
        pool_queries = []
        for pool in pools:
            pool_queries.append(dmg.pool_query(pool.identifier))

        self.log_step(f"Verify the {len(pool_queries)} pool query response(s)")
        _format = "  %-60s  %-7s  %-16s  %-16s  %s"
        self.log.debug(_format, "Pool", "Engines", "Metadata Storage", "Memory File Size", "Ratio")
        self.log.debug(_format, "-" * 60, "-" * 7, "-" * 16, "-" * 16, "-" * 7)
        for index, pool_query in enumerate(pool_queries):
            _engines = _memfile = _metadata = "<ERROR>"
            try:
                _engines = pool_query["response"]["total_engines"]
                _memfile = pool_query["response"]["mem_file_bytes"]
                _metadata = pool_query["response"]["tier_stats"][0]["total"]
            except (KeyError, IndexError) as error:
                self.log.debug(_format, str(pool), _engines, _memfile, _metadata, error)
                errors.append(f"{str(pools[index])} - Invalid dmg pool query response")
                continue
            actual = round(int(_memfile) / int(_metadata) * 100)
            self.log.debug(_format, str(pools[index]), _engines, _memfile, _metadata, actual)
            if "mem_ratio" in kwargs_list[index] and actual != kwargs_list[index]["mem_ratio"]:
                errors.append(
                    f"{str(pools[index])} - Actual mem ratio ({actual}) does not match specified "
                    f"ratio ({kwargs_list[index]['mem_ratio']}) in pool query output")

        report_errors(self, errors)
        self.log.info("Test passed")
