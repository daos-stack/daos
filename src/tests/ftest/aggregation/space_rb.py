"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils_base import CommandFailure
from general_utils import report_errors
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager


class SpaceRb(IorTestBase):
    """Verify space_rb property of the pool.

    :avocado: recursive
    """

    def verify_space_rb_property(self, pool, expected_space_rb):
        """Verify the space_rb property of the given pool is the expected value.

        Args:
            pool (TestPool): Pool to verify the space_rb property.
            expected_space_rb (int): Expected space_rb value. 50 in this test.
        """
        space_rb = pool.get_property(prop_name="space_rb")
        if space_rb != expected_space_rb:
            msg = (f"Unexpected space_rb is set in {pool.identifier}! "
                   f"Expected = {expected_space_rb}; Actual = {space_rb}")
            self.fail(msg)

    def run_ior_verify_error(self, namespace, pool, container, job_num, errors):
        """Run IOR and verify the error message contains the expected message.

        Args:
            namespace (str): Namespace that defines block_size and transfer_size.
            pool (TestPool): Pool to use with IOR.
            container (TestContainer): Container to use with IOR.
            job_num (int): Indicator to be used in the message.
            block_size (str): Block size parameter for the IOR.
            transfer_size (str): Transfer size parameter for the IOR.
            errors (list): List to collect the errors occurred during the test.
        """
        ior_cmd = IorCommand(namespace=namespace)
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(pool, container.identifier)
        testfile = os.path.join(os.sep, f"test_file_{job_num}")
        ior_cmd.test_file.update(testfile)
        manager = get_job_manager(test=self, job=ior_cmd, subprocess=self.subprocess)
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", namespace)
        manager.assign_processes(ppn=ppn)
        error_msg = None
        exception_detected = False

        try:
            manager.run()
            self.fail(f"IOR {job_num} didn't fail as expected!")
        except CommandFailure as error:
            exception_detected = True
            # Convert it to string to obtain the error message.
            error_msg = str(error)
        if not exception_detected:
            errors.append(f"IOR {job_num} didn't cause an error!")
        exp_msg = "No space left on device"
        if exp_msg not in error_msg:
            errors.append(f"'{exp_msg}' is not in the error message of IOR {job_num}!")

    def test_space_rb(self):
        """Jira ID: DAOS-15198

        1. Create pool with space_rb set to 50 and aggregation disabled.
        2. Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.
        3. Run IOR to fill 50% of SCM. Use small transfer size so that data are written to SCM.
        IOR should fail with DER_NOSPACE(-1007): 'No space left on device'
        4. Create a new pool with space_rb set to 50 and aggregation enabled this time.
        5. Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.
        6. Run IOR to fill 50% of NVMe. Use large transfer size so that data are written to NVMe.
        IOR should fail with DER_NOSPACE(-1007): 'No space left on device'

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=aggregation
        :avocado: tags=SpaceRb,test_space_rb
        """
        # 1. Create pool with space_rb set to 50 and aggregation disabled.
        self.log_step("Create pool with space_rb set to 50 and aggregation disabled.")
        pool_1 = self.get_pool(create=False)
        pool_1.create()
        pool_1.set_prop(properties="reclaim:disabled")
        container_1 = self.get_container(pool=pool_1)

        # 2. Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.
        self.log_step(
            "Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.")
        expected_space_rb = int(self.params.get("properties", '/run/pool/*').split(":")[1])
        self.verify_space_rb_property(pool=pool_1, expected_space_rb=expected_space_rb)

        # 3. Run IOR to fill 50% of SCM.
        self.log_step("Run IOR to fill 50% of SCM.")
        errors = []
        self.run_ior_verify_error(
            namespace="/run/ior_small_transfer/*", pool=pool_1, container=container_1, job_num=1,
            errors=errors)

        # 4. Create a new pool with space_rb set to 50 and aggregation enabled this time.
        self.log_step(
            "Create a new pool with space_rb set to 50 and aggregation enabled this time.")
        pool_2 = self.get_pool()
        container_2 = self.get_container(pool=pool_2)

        # 5. Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.
        self.log_step(
            "Call dmg pool get-prop and verify that Rebuild space ratio (space_rb) is 50%.")
        self.verify_space_rb_property(pool=pool_2, expected_space_rb=expected_space_rb)

        # 6. Run IOR to fill 50% of NVMe.
        self.log_step("Run IOR to fill 50% of NVMe.")
        self.run_ior_verify_error(
            namespace="/run/ior_large_transfer/*", pool=pool_2, container=container_2, job_num=2,
            errors=errors)

        report_errors(test=self, errors=errors)
