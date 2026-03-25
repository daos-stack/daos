"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from scrubber_test_base import TestWithScrubber


class TestWithScrubberBasic(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks
    """Basic Scrubber Test

    :avocado: recursive
    """

    def run_scrubber_basic(self):
        """Runs the basic scrubber testing."""
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')
        self.ior_cmd.api.update(apis[0])
        self.ior_cmd.flags.update(flags[0], "ior.flags")
        self.ior_cmd.dfs_oclass.update(obj_class[0])
        self.ior_cmd.dfs_dir_oclass.update(obj_class[0])
        for test in transfer_block_size:
            self.ior_cmd.transfer_size.update(test[0])
            self.ior_cmd.block_size.update(test[1])
        status = self.run_ior_and_check_scrubber_status(pool=self.pool, cont=self.container)
        if status is False:
            self.log.info("-------Test Failed-------")
            self.log.info("---No metrics value change----")
            self.fail("------Test Failed-----")
        self.log.info("------Test passed------")

    def test_scrubber_disabled_during_pool_creation(self):
        """JIRA ID: DAOS-7371

            1. Create a test pool without scrubber properties
            2. Enable scrubber on a test pool and gather
               scrubber statistics
            2. Enable scrubber, run IOR and gather
               scrubber statistics
            3. Disable checksum on a container and run
               IOR. Gather scrubber statistics.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=TestWithScrubberBasic,test_scrubber_disabled_during_pool_creation

        """
        other_properties = self.params.get("other_properties", '/run/pool/*')

        self.add_pool()
        for prop_val in other_properties.split(","):
            if prop_val is not None:
                value = prop_val.split(":")
                self.pool.set_property(value[0], value[1])
        self.add_container(pool=self.pool)

        self.run_scrubber_basic()

    def test_scrubber_enabled_during_pool_creation(self):
        """JIRA ID: DAOS-7371

            1. Create a test pool with scrubber properties
            2. Enable scrubber on a test pool creation.
            2. Enable scrubber, run IOR and gather
               scrubber statistics
            3. Disable checksum on a container and run
               IOR. Gather scrubber statistics.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=TestWithScrubberBasic,test_scrubber_enabled_during_pool_creation

        """
        pool_properties = self.params.get("properties", '/run/pool/*')
        other_properties = self.params.get("other_properties", '/run/pool/*')

        self.add_pool(properties=f"{pool_properties},{other_properties}")
        self.add_container(pool=self.pool)

        self.run_scrubber_basic()
