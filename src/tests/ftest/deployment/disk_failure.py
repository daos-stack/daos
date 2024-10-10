"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
import threading
import time

from avocado import fail_on
from dmg_utils import get_dmg_response, get_storage_query_device_info
from exception_utils import CommandFailure
from general_utils import list_to_str
from osa_utils import OSAUtils
from test_utils_pool import add_pool


class DiskFailureTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify disk failure is properly handled.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.targets = self.params.get("targets", "/run/server_config/servers/0/*")
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/*')

    @fail_on(CommandFailure)
    def verify_disk_failure(self, num_pool):
        """Run IOR and create disk failures while IO is happening.

        Args:
            num_pool (int) : Total number of pools to run the testing.
        """
        pool = {}

        # Get the device information.
        device_info = get_storage_query_device_info(self.dmg_command)

        self.log.info("Device information")
        self.log.info("==================")
        for index, entry in enumerate(device_info):
            self.log.info("Device %s:", index)
            for key in sorted(entry):
                self.log.info("  %s: %s", key, entry[key])

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            threads = []
            self.pool = pool[val]
            # The following thread runs while raising disk faults
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": self.ior_cmd.dfs_oclass.value,
                                                    "test": self.ior_test_sequence[0],
                                                    "fail_on_warning": False}))
            # Launch the IOR threads
            for thread in threads:
                self.log.info("Thread : %s", thread)
                thread.start()
                time.sleep(5)

            # Evict a random target from the system
            evict_device = random.choice(device_info)  # nosec
            self.log.info("Evicting random target: %s", evict_device["uuid"])
            try:
                get_dmg_response(self.dmg_command.storage_set_faulty,
                                 host=evict_device["hosts"].split(":")[0],
                                 uuid=evict_device["uuid"])
            except CommandFailure:
                self.fail("Error evicting target {}".format(evict_device["uuid"]))
            done = "Completed setting all devices to fault"
            self.print_and_assert_on_rebuild_failure(done)
            for thread in threads:
                thread.join()

            # Now replace the faulty NVME device.
            self.log.info("Replacing evicted target: %s", evict_device["uuid"])
            try:
                get_dmg_response(self.dmg_command.storage_replace_nvme,
                                 host=evict_device["hosts"].split(":")[0],
                                 old_uuid=evict_device["uuid"],
                                 new_uuid=evict_device["uuid"])
            except CommandFailure as error:
                self.fail(str(error))
            time.sleep(10)
            self.log.info(
                "Reintegrating evicted target: uuid=%s, rank=%s, targets=%s",
                evict_device["uuid"], evict_device["rank"], evict_device["tgt_ids"])
            output = self.pool.reintegrate(
                evict_device["rank"], list_to_str(evict_device["tgt_ids"]))
            time.sleep(15)
            done = "Faulty NVMEs replaced"
            self.print_and_assert_on_rebuild_failure(output)
            self.log.info(done)

        # After completing the test, check for container integrity
        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            self.run_ior_thread("Read", oclass=self.ior_cmd.dfs_oclass.value,
                                test=self.ior_test_sequence[0])
            self.container = self.pool_cont_dict[self.pool][0]
            self.container.check()

    def test_disk_failure_w_rf(self):
        """Jira ID: DAOS-11284.

        Test disk failures during the IO operation.

        :avocado: tags=all,manual
        :avocado: tags=deployment,disk_failure
        :avocado: tags=DiskFailureTest,test_disk_failure_w_rf
        """
        self.verify_disk_failure(1)

    @fail_on(CommandFailure)
    def test_disk_fault_to_normal(self):
        """Jira ID: DAOS-11284
        Test a disk inducing faults and resetting is back to normal state.

        :avocado: tags=all,manual
        :avocado: tags=deployment,disk_failure
        :avocado: tags=DiskFailureTest,test_disk_fault_to_normal
        """
        device_info = get_storage_query_device_info(self.dmg_command)
        for index, device in enumerate(device_info):
            host = device["hosts"].split(":")[0]
            self.log.info("Device %s on host %s:", index, host)
            for key in sorted(device):
                self.log.info("  %s: %s", key, device[key])
            try:
                # Set the device as faulty
                get_dmg_response(self.dmg_command.storage_set_faulty, host=host,
                                 uuid=device["uuid"])
                # Replace the device with same uuid.
                passed = False
                for _ in range(10):
                    data = self.dmg_command.storage_replace_nvme(host=host,
                                                                 old_uuid=device["uuid"],
                                                                 new_uuid=device["uuid"])
                    if not data['error'] and len(data['response']['host_errors']) == 0:
                        passed = True
                        break
                    time.sleep(5)
                if not passed:
                    self.fail('Replacing faulty device did not pass after 10 retries')
            except CommandFailure as error:
                self.fail(str(error))
