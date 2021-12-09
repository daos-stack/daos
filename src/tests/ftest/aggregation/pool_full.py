#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from time import sleep, time
from ior_test_base import IorTestBase
from general_utils import DaosTestError

class DaosAggregationFull(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:

       Run IOR with same file to fullfill the pool and verify the
       aggregation happens in the background. Check for the
       space usage before and after the aggregation to verify
       the overwritten space is reclaimed, and run IOR when aggregation
       turned on and verify the space.

    :avocado: recursive
    """

    def get_free_space(self, storage_index=0):
        """Get the pool information free space from the specified storage index

        Args:
            optional storage_index (int): index of the pool free space to
                obtain. 0(default): scm,  1: nvme
        Returns:
            int: pool free space for the specified storage index
        """
        self.pool.get_info()
        return self.pool.info.pi_space.ps_space.s_free[storage_index]

    def convert_str_to_number(self, string):
        """Convert the input string of K, M and B into number

        Arg:
            string (str): string of K, M, B to be converted
        Return:
            number (int): converted number
        """

        number = 0
        num_dic = {'K':1000, 'M':1000000, 'B':1000000000}
        if string.isdigit():
            number = int(string)
        else:
            if len(string) > 1:
                number = float(string[:-1]) * num_dic.get(
                    string[-1].upper(), 1)
        return int(number)

    def test_aggregation_poolfull(self):
        """Jira ID: DAOS-4870

        Test Description:
            Purpose of this test is to run ior using DFS api to write to
            a DAOS pool until out-of-space and enable the aggregation and
            verify the aggregation reclaims the overwritten space, then rerun
            ior on aggregation mode, to check space usage.

        Steps:
            (0)Create pool and container.
            (1)Run ior with -k option to retain the file and not delete it
               until out of space detected.
            (2)Verify pool free space and usage before aggregation.
            (3)Enable the aggregation after pool is out of space, wait for
               aggregation and reclaim space completed.
            (4)Run ior loop after aggregation enabled on the same pool.
            (5)Verify pool free space and usage after aggregation.
        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,large
        :avocado: tags=aggregate,daosio,ior
        :avocado: tags=aggregate_full
        """

        # Get the test params
        aggr_delay = self.params.get("aggregation_delay", "/run/pool/*")
        test_loop = self.params.get("test_loop", "/run/ior/*")
        ior_test_timeout = self.params.get("ior_test_timeout", "/run/ior/*")
        block_size = self.params.get("block_size", "/run/ior/*")
        ior_np = self.params.get("np", "/run/ior/client_processes/*")
        expect_ior_usage = self.convert_str_to_number(block_size) * ior_np

        #(0)Create pool and container
        self.update_ior_cmd_with_pool()
        free_space_init = self.get_free_space()
        free_space = [free_space_init]
        used_by_ior = [0]
        self.log.info("(0)===>initial_free_space= %s", free_space_init)
        #Disable the aggregation
        self.pool.set_property("reclaim", "disabled")

        #(1)Run ior with -k option to retain the file and not delete it
        #    until out of space detected.
        ind = 1
        out_of_space = False
        start = time()
        while time() - start < ior_test_timeout:
            try:
                output=self.run_ior_with_pool(create_pool=False,
                                              fail_on_warning=False)
                free_space.append(self.get_free_space())
                used_by_ior.append(free_space[ind-1] - free_space[ind])
                self.assertTrue(free_space[ind] <= free_space[ind-1],
                                "IOR run was not successful.")
                ind += 1
            except Exception as excep:
                out_of_space = True
                self.log.info("=Error on run_ior_with_pool, out of space.. %s",
                              repr(excep))
                break
        if not out_of_space:
            self.fail("#IOR test timeout occurred before pool out-of-space.")

        #(2)Verify pool free space and usage before aggregation
        for ind3 in range(1, ind):
            self.log.info("(%s)===>free_space_after ior#%s= %s, usage= %s",
                          ind3, ind3, free_space[ind3], used_by_ior[ind3])
            if used_by_ior[ind3] < expect_ior_usage:
                self.fail("#IOR usage smaller than expected, ind3={}".format(
                    ind3))
            else:
                self.log.info("==pass ind3=%s", ind3)

        #(3)Enable the aggregation after pool is out of space
        self.pool.set_property("reclaim", "time")
        # wait for files to get old enough for aggregation +
        # for aggregation to start and finish
        self.log.info("===>Waiting for %s seconds for aggregation to start \
            and finish", aggr_delay)
        sleep(aggr_delay)
        free_space.append(self.get_free_space())
        used_by_ior.append(0)
        self.log.info("(%s)===>free_space after aggregation = %s",
            ind+1, free_space[-1])

        #(4)Run ior after aggregation enabled
        for ind2 in range(ind+1, test_loop):
            self.run_ior_with_pool(create_pool=False, fail_on_warning=False)
            free_space.append(self.get_free_space())
            used_by_ior.append(free_space[ind2-1] - free_space[ind2])
            self.log.info("(%s)===>free_space_after ior#%s= %s",
                          ind2, ind2, free_space[ind2])
            self.log.info("    ==>Space used by ior #%s= %s",
                          ind2, used_by_ior[ind2])
            self.assertTrue(free_space[ind2] <= free_space[ind2-1],
                            "##IOR run was not successful.")

        #(5)Verify pool free space and usage after aggretation
        for ind3 in range(ind+2, test_loop):
            self.log.info("(%s)===>free_space_after ior#%s= %s, usage= %s",
                          ind3, ind3, free_space[ind3], used_by_ior[ind3])
            if used_by_ior[ind3] > expect_ior_usage:
                self.fail(
                    "##IOR usage greater than expected, ind3={}".format(ind3))
            else:
                self.log.info("==pass ind3=%s", ind3)
