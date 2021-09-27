#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from scrubber_test_base import TestWithScrubber


class TestWithScrubberFault(TestWithScrubber):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Inject Checksum Fault with scrubber enabled

    :avocado: recursive
    """
    def test_scrubber_csum_fault(self):
        """JIRA ID: DAOS-7333

            1. Create checksum faults and see
            whether scrubber finds them.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_csum_fault

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
        self.create_pool_cont_with_scrubber()
        self.scrubber.get_scrub_corrupt_metrics()
        for test in transfer_block_size:
            self.ior_cmd.transfer_size.update(test[0])
            self.ior_cmd.block_size.update(test[1])
        self.run_ior_and_check_scruber_status(pool=self.sc_pool, cont=self.sc_container)
        self.scrubber.get_scrub_corrupt_metrics()
        self.log.info("------Test passed------")
