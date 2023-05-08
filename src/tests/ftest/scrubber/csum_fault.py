"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from scrubber_test_base import TestWithScrubber


class TestWithScrubberFault(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks
    """Inject Checksum Fault with scrubber enabled

    :avocado: recursive
    """
    def test_scrubber_csum_fault(self):
        """JIRA ID: DAOS-7333

            1. Create checksum faults and see
            whether scrubber finds them.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=TestWithScrubberFault,test_scrubber_csum_fault

        """
        flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        transfer_block_size = self.params.get("transfer_block_size",
                                              '/run/ior/iorflags/*')
        obj_class = self.params.get("obj_class", '/run/ior/iorflags/*')
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        self.ior_cmd.api.update(apis[0])
        self.ior_cmd.flags.update(flags[0], "ior.flags")
        self.ior_cmd.dfs_oclass.update(obj_class[0])
        self.ior_cmd.dfs_dir_oclass.update(obj_class[0])
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        for test in transfer_block_size:
            self.ior_cmd.transfer_size.update(test[0])
            self.ior_cmd.block_size.update(test[1])
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        start_time = 0
        finish_time = 0
        poll_status = False
        start_time = time.time()
        while int(finish_time - start_time) < 60 and poll_status is False:
            final_metrics = self.scrubber.get_scrub_corrupt_metrics()
            status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
            if status is False:
                self.log.info("------Test Failed-----")
                self.log.info("---No metrics value change----")
            else:
                poll_status = True
            finish_time = time.time()
        if poll_status is True:
            self.log.info("------Test passed------")
        else:
            self.fail("------Test Failed-----")
