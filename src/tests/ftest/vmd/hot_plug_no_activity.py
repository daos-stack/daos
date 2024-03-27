"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from dmg_utils import check_system_query_status, get_storage_query_device_info
from general_utils import report_errors, run_pcmd
from ior_test_base import IorTestBase


class HotPlugNoActivityTest(IorTestBase):
    """Test class to test VMD hot-remove and hot-plug during no activity.

    :avocado: recursive
    """
    def repeat_query_list_devices(self, exp_disk_state, exp_led_state, error_msg, errors,
                                  dmg_command, rank):
        """Repeatedly call dmg storage query list-devices verify the states.

        Args:
            exp_disk_state (str): Expected disk state.
            exp_led_state (str): Expected LED state.
            error_msg (str): Error message to add to the error list.
            errors (list): Error list for this test.
            dmg_command (DmgCommand): DmgCommand object to call the command.
            rank (int): Rank where the NVMe disk we want to access is located.
        """
        removed_state = False
        for count in range(2):
            time.sleep(30)
            device_info = get_storage_query_device_info(dmg=dmg_command)
            self.log.info("device_info (repeat_query_list_devices) = %s", device_info)
            dev_state = None
            led_state = None
            for device in device_info:
                if device["rank"] == rank:
                    device_control = device["ctrlr"]
                    dev_state = device_control["dev_state"]
                    led_state = device_control["led_state"]
            if dev_state == exp_disk_state and led_state == exp_led_state:
                removed_state = True
                break
            self.log.info("%d: Disk state = %s; LED state = %s", count, dev_state, led_state)
        if not removed_state:
            errors.append(error_msg)

    def get_total_nvme_bytes(self, dmg_command, host_port, rank):
        """Get total NVMe bytes using 'dmg storage query usage'

        Args:
            dmg_command (DmgCommand): DmgCommand object used to call the command and to obtain the
                port number.
            host_port (str): Host where the NVMe disk we want to access is located. <host>:<port>
            rank (int): Rank where the NVMe disk we want to access is located.

        Returns:
            int: total_bytes value of the NVMe device we're interested, which is specified by
                host_port and rank parameter. If not found, for example because of invalid host_port
                rank combination, returns None.

        """
        total_bytes = None
        usage_out = dmg_command.storage_query_usage()
        # There's a hash value under HostStorage. Obtain "storage" -> "nvme_devices" and "hosts"
        # under it. There may be multiple hosts depending on the setup.
        for hash_value in usage_out["response"]["HostStorage"].values():
            if hash_value["hosts"] == host_port:
                for nvme_device in hash_value["storage"]["nvme_devices"]:
                    # In HW medium cluster, there's only one dictionary in the smd_devices list, so
                    # we may be able to just index it, but use for loop just in case.
                    for smd_device in nvme_device["smd_devices"]:
                        if smd_device["rank"] == rank:
                            total_bytes = smd_device["total_bytes"]
        self.log.info("total_bytes = %s", total_bytes)
        return total_bytes

    def test_no_activity(self):
        """Conduct VMD hot-remove and hot-plug during no activity.

        1. Determine the PCI address (TrAddr) of the disk we'll hot-remove and verify that its state
        is NORMAL and LED is OFF.
        2. Store the total NVMe size.
        3. Create a pool and a container.
        4. Write data with IOR.
        5. Hot remove the disk we selected at step 1.
        6. Repeatedly call "dmg storage query list-devices" until the disk state becomes UNPLUGGED
        and LED becomes NA.
        7. Verify that the disk space is down compared to before the remove.
        8. Hot-plug.
        9. Repeatedly call "dmg storage query list-devices" until the disk state becomes NORMAL and
        LED becomes OFF.
        10. Verify that the disk space is back to the original.
        11. Verify that none of the engines have crashed.
        12. Verify that the disks are healthy by checking the container status.
        13. Rung IOR and check that it works.

        Jira ID: DAOS-15008

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,hot_plug
        :avocado: tags=HotPlugNoActivityTest,test_no_activity
        """
        # 1. Determine the PCI address (TrAddr) of the disk we'll hot-remove and verify that its
        # state is NORMAL and LED is OFF.
        msg = ("Determine the PCI address (TrAddr) of the disk we'll hot-remove and verify that "
               "its state is NORMAL and LED is OFF.")
        self.log_step(msg)
        dmg_command = self.get_dmg_command()
        device_info = get_storage_query_device_info(dmg=dmg_command)
        self.log.info("device_info = %s", device_info)
        pci_addr = None
        dev_state = None
        led_state = None
        host_port = None
        rank = 0
        # We'll use the rank 0 device.
        for device in device_info:
            if device["rank"] == rank:
                device_control = device["ctrlr"]
                pci_addr = device_control["pci_addr"]
                dev_state = device_control["dev_state"]
                led_state = device_control["led_state"]
                host_port = device["hosts"]
                break
        msg = (f"pci_addr = {pci_addr}; dev_state = {dev_state}; led_state = {led_state}; "
               f"host_port = {host_port}")
        self.log.info(msg)
        errors = []
        exp_disk_state = "NORMAL"
        if dev_state != exp_disk_state:
            errors.append(
                f"Unexpected disk state! Expected = {exp_disk_state}, Actual = {dev_state}")
        exp_led_state = "OFF"
        if led_state != exp_led_state:
            errors.append(f"Unexpected LED state! Expected = {exp_led_state}, Actual = {led_state}")

        # 2. Store the total NVMe size.
        self.log_step("Store the total NVMe size.")
        total_bytes_orig = self.get_total_nvme_bytes(
            dmg_command=dmg_command, host_port=host_port, rank=rank)

        # 3. Create a pool and a container.
        self.log_step("Create a pool and a container")
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(pool=self.pool)

        # 4. Write data with IOR.
        self.log_step("Write data with IOR.")
        self.ior_cmd.set_daos_params(self.server_group, self.pool, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        # 5. Hot remove the disk we selected at step 1.
        # self.log_step("Hot remove the disk we selected at step 1.")
        # command = f"sudo python3 /home/mkano/daos/build/external/debug/spdk/scripts/rpc.py -s /var/tmp/spdk_0.sock vmd_remove_device {pci_addr}"
        # rpc_out = run_pcmd(hosts=self.hostlist_servers, command=command)
        # self.log.debug(f"## Hot remove out = {rpc_out}")
        # stdout contains the output as list. Each item in the list represents a line. e.g.,
        # '[',
        # '  {',
        # '    "name": "Nvme_wolf-313.wolf.hpdd.intel.com_0_1_0",',
        # '    "ctrlrs": [',
        # '      {',
        # '        "state": "enabled",',
        # ...
        # We'll remove the first and the last bracket, concatenate all items into a single string,
        # then pass it into yaml.safe_load(). (It works even there are white spaces at the beginning
        # of each line.)
        # stdout = rpc_out[0]["stdout"]
        # First and last item of stdout is "[" and "]", so remove them.
        # stdout.pop()
        # stdout.pop(0)
        # Concatenate each line into a single string.
        # stdout_str = "".join(stdout)
        # Convert the string to a yaml object so that we can easily reference the values.
        # yaml_out = yaml.safe_load(stdout_str)
        # self.log.debug(f"## yaml_out = {yaml_out}")
        # name = yaml_out["name"]
        # self.log.debug(f"## name = {name}")
        # state = yaml_out["ctrlrs"][0]["state"]
        # self.log.debug(f"## state = {state}")

        # 6. Repeatedly call "dmg storage query list-devices" until the disk state becomes UNPLUGGED
        # and LED becomes NA.
        # msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
        #        "UNPLUGGED and LED becomes NA.")
        # self.log_step(msg)
        # error_msg = "Disk and LED state didn't turn to removed state. (UNPLUGGED and OFF)"
        # self.repeat_query_list_devices(
        #     exp_disk_state="UNPLUGGED", exp_led_state="OFF", error_msg=error_msg, errors=errors,
        #     dmg_command=dmg_command, rank=rank)

        # 7. Verify that the disk space is down compared to before the remove.
        # self.log_step("Verify that the disk space is down compared to before the remove.")
        # total_bytes_hot_remove = self.get_total_nvme_bytes(
        #     dmg_command=dmg_command, host_port=host_port, rank=rank)
        # if total_bytes_hot_remove >= total_bytes_orig:
        #     msg = (f"Total NVMe bytes haven't been reduced after a hot remove! "
        #            f"Original = {total_bytes_orig}; Hot-removed = {total_bytes_hot_remove}")
        #     errors.append(msg)

        # 8. Hot-plug.
        # self.log_step("Hot-plug.")
        # command = f"sudo python3 /home/mkano/daos/build/external/debug/spdk/scripts/rpc.py -s /var/tmp/spdk_0.sock vmd_rescan"
        # rpc_out = run_pcmd(hosts=self.hostlist_servers, command=command)
        # self.log.debug(f"## Hot plug out = {rpc_out}")

        # 9. Repeatedly call "dmg storage query list-devices" until the disk state becomes NORMAL
        # and LED becomes OFF.
        msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
               "NORMAL and LED becomes OFF.")
        self.log_step(msg)
        error_msg = "Disk and LED state didn't turn to plugged state. (NORMAL and OFF)"
        self.repeat_query_list_devices(
            exp_disk_state="NORMAL", exp_led_state="OFF", error_msg=error_msg, errors=errors,
            dmg_command=dmg_command, rank=rank)

        # 10. Verify that the disk space is back to the original.
        self.log_step("Verify that the disk space is back to the original.")
        total_bytes_hot_plug = self.get_total_nvme_bytes(
            dmg_command=dmg_command, host_port=host_port, rank=rank)
        if total_bytes_hot_plug != total_bytes_orig:
            msg = (f"Total NVMe bytes haven't been recovered! Original = {total_bytes_orig}; "
                   f"After hot plug = {total_bytes_hot_plug}")
            errors.append(msg)

        # 11. Verify that none of the engines have crashed.
        self.log_step("Verify that none of the engines have crashed.")
        system_query_out = dmg_command.system_query()
        system_healthy = check_system_query_status(data=system_query_out)
        if not system_healthy:
            errors.append("One or more ranks crashed after hot remove and plug!")

        # 12. Verify that the disks are healthy by checking the container status.
        self.log_step("Verify that the disks are healthy by checking the container status.")
        expected_props = {"status": "HEALTHY"}
        container_healthy = self.container.verify_prop(expected_props=expected_props)
        if not container_healthy:
            errors.append("Container status isn't HEALTHY after hot remove and plug!")

        # 13. Rung IOR and check that it works.
        self.log_step("Rung IOR and check that it works.")
        cmd_result = self.run_ior_with_pool(create_pool=False, create_cont=False)
        if cmd_result.exit_status != 0:
            errors.append(f"IOR after hot-plug failed! cmd_result = {cmd_result}")

        self.log.info("##### Errors ######")
        report_errors(test=self, errors=errors)
