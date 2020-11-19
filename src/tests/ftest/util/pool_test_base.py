#!/usr/bin/python
"""
(C) Copyright 2020 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
The Government's rights to use, modify, reproduce, release, perform, display,
or disclose this software are subject to the terms of the Apache License as
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""

from apricot import TestWithServers
from general_utils import bytes_to_human, human_to_bytes
from server_utils import ServerFailed


class PoolTestBase(TestWithServers):
    """Base pool test class.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()

        super(PoolTestBase, self).setUp()
        self.dmg = self.get_dmg_command()

    def get_max_pool_sizes(self, scm_ratio=0.9, nvme_ratio=0.9):
        """Get the maximum pool sizes for the current server configuration.

        Args:
            scm_ratio (float, optional): percentage of the maximum SCM
                capacity to use for the pool sizes. Defaults to 0.9 (90%).
            nvme_ratio (float, optional): percentage of the maximum NVMe
                capacity to use for the pool sizes. Defaults to 0.9 (90%).

        Returns:
            list: a list of bytes representing the maximum pool creation
                SCM size and NVMe size

        """
        try:
            sizes = self.server_managers[0].get_available_storage()
        except ServerFailed as error:
            self.fail(error)

        ratios = (scm_ratio, nvme_ratio)
        for index, size in enumerate(sizes):
            if size and ratios[index] < 1:
                # Reduce the size by the specified percentage
                sizes[index] *= ratios[index]
                self.log.info(
                    "Adjusted %s size by %.2f%%: %s (%s)",
                    "SCM" if index == 0 else "NVMe", 100 * ratios[index],
                    str(sizes[index]), bytes_to_human(sizes[index]))
        return sizes

    def get_pool_list(self, quantity, scm_ratio, nvme_ratio):
        """Get a list of TestPool objects.

        Set each TestPool's scm_size and nvme_size attributes using the
        specified ratios and the largest SCM or NVMe size common to all the
        configured servers.

        Args:
            quantity (int): number of TestPool objects to create
            scm_ratio (float): percentage of the maximum SCM capacity to use
                for the pool sizes, e.g. 0.9 for 90%
            nvme_ratio (float): percentage of the maximum NVMe capacity to use
                for the pool sizes, e.g. 0.9 for 90%. Specifying None will
                setup each pool without NVMe.

        Returns:
            list: a list of TestPool objects equal in length to the quantity
                specified, each configured with the same SCM and NVMe sizes.

        """
        sizes = self.get_max_pool_sizes(
            scm_ratio, 1 if nvme_ratio is None else nvme_ratio)
        pool_list = [
            self.get_pool(create=False, connect=False) for _ in range(quantity)]
        for pool in pool_list:
            pool.scm_size.update(bytes_to_human(sizes[0]), "scm_size")
            if nvme_ratio is not None:
                if sizes[1] is None:
                    self.fail(
                        "Unable to assign a max pool NVMe size; NVMe not "
                        "configured!")

                # The I/O server allocates NVMe storage on targets in multiples
                # of 1GiB per target.  A server with 8 targets will have a
                # minimum NVMe size of 8 GiB.  Specify the largest NVMe size in
                # GiB that can be used with the configured number of targets and
                # specified capacity in GiB.
                targets = self.server_managers[0].get_config_value("targets")
                increment = human_to_bytes("{}GiB".format(targets))
                nvme_multiple = increment
                while nvme_multiple + increment <= sizes[1]:
                    nvme_multiple += increment
                self.log.info(
                    "Largest NVMe multiple based on %s targets in %s: %s (%s)",
                    targets, str(sizes[1]), str(nvme_multiple),
                    bytes_to_human(nvme_multiple))
                pool.nvme_size.update(
                    bytes_to_human(nvme_multiple), "nvme_size")

        return pool_list
