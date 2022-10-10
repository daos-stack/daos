"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import json
import threading
from test_utils_pool import add_pool
from osa_utils import OSAUtils
from avocado.utils import process


class DiskFailureTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify network failure is properly handled and recovered.

    :avocado: recursive
    """
    def setUp(self):
        super().setUp()
        self.targets = self.params.get("targets", "/run/server_config/servers/0/*")
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.daos_command = self.get_daos_command()

    def get_nvme_device_info(self):
        """Get the list of nvme device-ids.
        Returns: List of uuid and ranks
        """
        self.dmg_command.json.value = True
        try:
            result = self.dmg_command.storage_query_list_devices()
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))
        finally:
            self.dmg_command.json.value = False
        data = json.loads(result.stdout_text)
        resp = data['response']
        if data['error'] or len(resp['host_errors']) > 0:
            if data['error']:
                self.fail("dmg command failed: {}".format(data['error']))
            else:
                self.fail("dmg command failed: {}".format(resp['host_errors']))
        # uuid = []
        # ranks = []
        device_info = {}
        count = 0
        for value in list(resp['host_storage_map'].values()):
            if value['storage']['smd_info']['devices']:
                for target in range(self.targets):
                    device_info[count] = {}
                    uuid_value = value['storage']['smd_info']['devices'][target]['uuid']
                    device_info[count]["uuid"] = uuid_value
                    rank = value['storage']['smd_info']['devices'][target]['rank']
                    device_info[count]["rank"] = rank
                    tgt_ids = value['storage']['smd_info']['devices'][target]['tgt_ids']
                    device_info[count]["tgts"] = tgt_ids
        return device_info

    def set_nvme_faulty(self, nvme_id=None):
        """Get a device to faulty state.

        Args:
            device_id (str): Device UUID
        Returns:
            dmg device faulty information.
        """
        data = {}
        if nvme_id is None:
            self.fail("No device id provided")
        self.dmg_command.json.value = True
        try:
            result = self.dmg_command.storage_set_faulty(uuid=nvme_id)
        except process.CmdError as details:
            self.fail("dmg command failed: {}".format(details))
        finally:
            self.dmg_command.json.value = False
        data = json.loads(result.stdout_text)
        resp = data['response']
        return resp

    def verify_disk_failure(self, num_pool):
        """Run IOR and create disk failures while IO is happening.

        Args:
            ior_namespace (str): Yaml namespace that defines the object class used for
                IOR.
            container_namespace (str): Yaml namespace that defines the container
                redundancy factor.
        """
        #dev_id = []
        #rank = []
        device_info = {}
        pool = {}

        # Get the device information.
        device_info = self.get_nvme_device_info()
        self.log.info("Device information")
        self.log.info("==================")
        self.log.info(device_info)
        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)

        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]
            # The following thread runs while performing osa operations.
            threads.append(threading.Thread(target=self.run_ior_thread,
                                            kwargs={"action": "Write",
                                                    "oclass": self.ior_cmd.dfs_oclass.value,
                                                    "test": self.ior_test_sequence[0],
                                                    "fail_on_warning": False}))
            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(5)

            count = 0
            # Evict some of the targets from the system
            for key in device_info.keys():
                if count < 1:
                    resp = self.set_nvme_faulty(device_info[key]["uuid"])
                    time.sleep(5)
                    self.log.info(resp)
                count = count + 1
            done = "Completed setting all devices to fault"
            self.print_and_assert_on_rebuild_failure(done)
            for thrd in threads:
                thrd.join()
                #if not self.out_queue.empty():
                #    self.assert_on_exception()
            # Now replace the faulty NVME device.
            count = 0
            for key in device_info.keys():
                if count < 1:
                    resp = self.dmg_command.storage_replace_nvme(old_uuid=device_info[key]["uuid"],
                                                                 new_uuid=device_info[key]["uuid"])
                    time.sleep(10)
                    self.log.info(resp)
                    # Now reintegrate the target to appropriate rank.
                    output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                               device_info[key]["uuid"],
                                                               device_info[key]["tgts"])
                    time.sleep(15)
                count = count + 1
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
            kwargs = {"pool": self.pool.uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    def test_disk_failure_w_rf(self):
        """Jira ID: DAOS-11284
        Test disk failures during the IO operation.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=deployment
        :avocado: tags=disk_failure
        """
        self.verify_disk_failure(1)

    def test_disk_reset(self):
        """Jira ID: DAOS-11284
        Test disk failures during the IO operation.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=deployment
        :avocado: tags=disk_reset
        """
        device_info = {}

        # Get the list of device ids.
        device_info = self.get_nvme_device_info()
        for key in device_info.keys():
            resp = self.dmg_command.storage_replace_nvme(old_uuid=device_info[key]["uuid"],
                                                         new_uuid=device_info[key]["uuid"])
            self.log.info(resp)
