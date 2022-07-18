#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from scrubber_test_base import TestWithScrubber
from apricot import skipForTicket


class TestWithScrubberBasic(TestWithScrubber):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Basic Scrubber Test

    :avocado: recursive
    """

    def test_scrubber_basic(self, pool_prop=None, cont_prop=None):
        """JIRA ID: DAOS-7371
        Scrubber basic main method which runs the basic testing.

        Args:
           pool_prop(str) : Test pool properties string.
           cont_prop(str) : Test container properties string
        """
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')
        self.ior_cmd.api.update(apis[0])
        self.ior_cmd.flags.update(flags[0], "ior.flags")
        self.ior_cmd.dfs_oclass.update(obj_class[0])
        self.ior_cmd.dfs_dir_oclass.update(obj_class[0])
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        for test in transfer_block_size:
            self.ior_cmd.transfer_size.update(test[0])
            self.ior_cmd.block_size.update(test[1])
        status = self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        if status is False:
            self.log.info("-------Test Failed-------")
            self.log.info("---No metrics value change----")
            self.fail("------Test Failed-----")
        self.log.info("------Test passed------")

    @skipForTicket("DAOS-8906")
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
        :avocado: tags=hw,medium,ib2
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_disabled_during_pool_creation

        """
        self.test_scrubber_basic(None, None)

    def test_scrubber_enabled_during_pool_creation(self):
        """JIRA ID: DAOS-7371

            1. Create a test pool with scrubber properties
            2. Enable scrubber on a test pool creation.
            2. Enable scrubber, run IOR and gather
               scrubber statistics
            3. Disable checksum on a container and run
               IOR. Gather scrubber statistics.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_enabled_during_pool_creation

        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        self.test_scrubber_basic(pool_prop, cont_prop)
