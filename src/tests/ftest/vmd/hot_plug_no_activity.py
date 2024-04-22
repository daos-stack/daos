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
                                  dmg_command, uuid):
        """Repeatedly call dmg storage query list-devices verify the states.

        Args:
            exp_disk_state (str): Expected disk state.
            exp_led_state (str): Expected LED state.
            error_msg (str): Error message to add to the error list.
            errors (list): Error list for this test.
            dmg_command (DmgCommand): DmgCommand object to call the command.
            uuid (str): Device UUID.
        """
        removed_state = False
        for count in range(2):
            time.sleep(30)
            device_info = get_storage_query_device_info(dmg=dmg_command)
            self.log.info("## device_info (repeat_query_list_devices) = %s", device_info)
            dev_state = None
            led_state = None
            for device in device_info:
                if device["uuid"] == uuid:
                    device_control = device["ctrlr"]
                    dev_state = device_control["dev_state"]
                    led_state = device_control["led_state"]
            if dev_state == exp_disk_state and led_state == exp_led_state:
                removed_state = True
                break
            self.log.info("## %d: Disk state = %s; LED state = %s", count, dev_state, led_state)
        if not removed_state:
            errors.append(error_msg)

    def get_uuid_to_total_byes(self, dmg_command, host_port):
        """Call 'dmg storage query usage' and get UUID and total bytes of each NVMe drive.

        Args:
            dmg_command (DmgCommand): DmgCommand object used to call the command and to obtain the
                port number.
            host_port (str): Host where the NVMe disk we want to access is located. <host>:<port>

        Returns:
            dict: UUID to total bytes of each NVMe drive that are in the given host.

        """
        uuid_to_total_bytes = {}
        usage_out = dmg_command.storage_query_usage()
        # There's a hash value under HostStorage. Obtain "storage" -> "nvme_devices" and "hosts"
        # under it. There may be multiple hosts depending on the setup.
        for hash_value in usage_out["response"]["HostStorage"].values():
            if hash_value["hosts"] == host_port:
                for nvme_device in hash_value["storage"]["nvme_devices"]:
                    # In HW medium cluster, there's only one dictionary in the smd_devices list, so
                    # we may be able to just index it, but use for loop just in case.
                    for smd_device in nvme_device["smd_devices"]:
                        uuid_to_total_bytes[smd_device["uuid"]] = smd_device["total_bytes"]
        return uuid_to_total_bytes

    def test_no_activity(self):
        """Test VMD hot-remove and hot-plug during no activity.

        1. Determine the PCI address (TrAddr) of the disk we'll hot-remove and verify that its state
        is NORMAL and LED is OFF.
        2. Store the total NVMe size of each drive.
        3. Create a pool and a container.
        4. Write data with IOR.
        5. Call dmg storage set nvme-faulty --uuid=<UUID>
        6. Repeatedly call "dmg storage query list-devices" until the disk state becomes EVICTED
        and LED becomes ON.
        7. Hot remove the disk we selected at step 1.
        8. Repeatedly call "dmg storage query list-devices" until the disk state becomes UNPLUGGED
        and LED becomes NA.
        9. For those untouched devices, verify that the space is unchanged after the hot remove.
        10. Hot-plug.
        11. Repeatedly call "dmg storage query list-devices" until the disk state becomes EVICTED
        and LED becomes ON.
        12. Call dmg storage replace nvme --old-uuid=<UUID> --new-uuid=<UUID>
        13. Repeatedly call "dmg storage query list-devices" until the disk state becomes NORMAL and
        LED becomes OFF.
        14. Verify that the disk spaces are back to the original.
        15. Verify that none of the engines have crashed.
        16. Verify that the disks are healthy by checking the container status.
        17. Rung IOR and check that it works.

        Jira ID: DAOS-15008

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=vmd,hot_plug
        :avocado: tags=HotPlugNoActivityTest,test_no_activity
        """
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
        # We'll use the first rank 0 device. There could be multiple devices mapped to rank 0.
        for device in device_info:
            if device["rank"] == rank:
                remove_uuid = device["uuid"]  # UUID of the drive that we'll remove and plug.
                device_control = device["ctrlr"]
                pci_addr = device_control["pci_addr"]
                dev_state = device_control["dev_state"]
                led_state = device_control["led_state"]
                host_port = device["hosts"]
                break
        msg = (f"pci_addr = {pci_addr}; dev_state = {dev_state}; led_state = {led_state}; "
               f"host_port = {host_port}; uuid = {remove_uuid}")
        self.log.info(msg)
        errors = []
        exp_disk_state = "NORMAL"
        if dev_state != exp_disk_state:
            errors.append(
                f"Unexpected disk state! Expected = {exp_disk_state}, Actual = {dev_state}")
        exp_led_state = "OFF"
        if led_state != exp_led_state:
            errors.append(f"Unexpected LED state! Expected = {exp_led_state}, Actual = {led_state}")

        self.log_step("Store the total NVMe size of each drive.")
        uuid_to_total_bytes_orig = self.get_uuid_to_total_byes(
            dmg_command=dmg_command, host_port=host_port)
        self.log.debug("## uuid_to_total_bytes_orig = %s", uuid_to_total_bytes_orig)

        self.log_step("Create a pool and a container")
        self.pool = self.get_pool(connect=False)
        self.container = self.get_container(pool=self.pool)

        self.log_step("Write data with IOR.")
        self.ior_cmd.set_daos_params(self.server_group, self.pool, self.container.identifier)
        self.run_ior_with_pool(create_pool=False, create_cont=False)

        self.log_step("Call dmg storage set nvme-faulty --uuid=<UUID>.")
        dmg_command.storage_set_faulty(uuid=remove_uuid)

        exp_disk_state = "EVICTED"
        exp_led_state = "ON"
        msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
               f"{exp_disk_state} and LED becomes {exp_led_state}.")
        self.log_step(msg)
        error_msg = f"Disk state must be {exp_disk_state} and LED state must be {exp_led_state}."
        self.repeat_query_list_devices(
            exp_disk_state=exp_disk_state, exp_led_state=exp_led_state, error_msg=error_msg,
            errors=errors, dmg_command=dmg_command, uuid=remove_uuid)

        self.log_step("Hot remove the disk we selected at step 1.")
        spdk_sock_path = self.params.get(
            "sock_addr", "/run/server_config/engines/0/spdk_rpc_server/*")
        command = (f"sudo /usr/share/spdk/scripts/rpc.py -s {spdk_sock_path} vmd_remove_device "
                   f"{pci_addr}")
        rpc_out = run_pcmd(hosts=self.hostlist_servers, command=command)
        self.log.debug("## Hot remove out = %s", rpc_out)
        exit_status = rpc_out[0]["exit_status"]
        if exit_status != 0:
            self.fail(f"Hot remove failed! {rpc_out}")

        exp_disk_state = "UNPLUGGED"
        exp_led_state = "NA"
        msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
               f"{exp_disk_state} and LED becomes {exp_led_state}.")
        self.log_step(msg)
        error_msg = (f"Disk and LED state didn't turn to removed state. ({exp_disk_state} and "
                     f"{exp_led_state})")
        self.repeat_query_list_devices(
            exp_disk_state=exp_disk_state, exp_led_state=exp_led_state, error_msg=error_msg,
            errors=errors, dmg_command=dmg_command, uuid=remove_uuid)

        # Note: For this step, if we don't use JSON as in manual test, we get the sum of total bytes
        # from all drives in a host, so we just compare the values before and after the hot remove.
        # However, automated test uses JSON, which returns total bytes for each drive, so we can do
        # finer grained comparisons.
        msg = ("For those untouched devices, verify that the space is unchanged after the hot "
               "remove.")
        self.log_step(msg)
        uuid_to_total_bytes_after = self.get_uuid_to_total_byes(
            dmg_command=dmg_command, host_port=host_port)
        self.log.debug("## uuid_to_total_bytes_after = %s", uuid_to_total_bytes_after)
        # Check that the removed device doesn't appear in 'dmg storage query usage' output.
        if remove_uuid in uuid_to_total_bytes_after:
            msg = (f"Removed device ({remove_uuid}) appears after hot remove! "
                   f"{uuid_to_total_bytes_after}")
            errors.append(msg)
        for disk_uuid, total_bytes in uuid_to_total_bytes_orig.items():
            if disk_uuid != remove_uuid:
                if disk_uuid not in uuid_to_total_bytes_after:
                    msg = f"Untouched disk ({disk_uuid}) disappeared after a hot remove!"
                    errors.append(msg)
                elif total_bytes != uuid_to_total_bytes_after[disk_uuid]:
                    msg = f"Hot remove resulted in untouched disk ({disk_uuid}) size change!"
                    errors.append(msg)

        self.log_step("Hot-plug.")
        command = f"sudo /usr/share/spdk/scripts/rpc.py -s {spdk_sock_path} vmd_rescan"
        rpc_out = run_pcmd(hosts=self.hostlist_servers, command=command)
        self.log.debug("## Hot plug out = %s", rpc_out)
        exit_status = rpc_out[0]["exit_status"]
        if exit_status != 0:
            self.fail(f"Hot plug failed! {rpc_out}")

        exp_disk_state = "EVICTED"
        exp_led_state = "ON"
        msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
               f"{exp_disk_state} and LED becomes {exp_led_state}.")
        self.log_step(msg)
        error_msg = f"Disk must be {exp_disk_state} and LED must be {exp_led_state}."
        self.repeat_query_list_devices(
            exp_disk_state=exp_disk_state, exp_led_state=exp_led_state, error_msg=error_msg,
            errors=errors, dmg_command=dmg_command, uuid=remove_uuid)

        self.log_step("Call dmg storage replace nvme --old-uuid=<UUID> --new-uuid=<UUID>")
        dmg_command.storage_replace_nvme(old_uuid=remove_uuid, new_uuid=remove_uuid)

        exp_disk_state = "NORMAL"
        exp_led_state = "OFF"
        msg = ("Repeatedly call 'dmg storage query list-devices' until the disk state becomes "
               f"{exp_disk_state} and LED becomes {exp_led_state}.")
        self.log_step(msg)
        error_msg = (f"Disk and LED state didn't go back to normal state! ({exp_disk_state} and "
                     f"{exp_led_state})")
        self.repeat_query_list_devices(
            exp_disk_state=exp_disk_state, exp_led_state=exp_led_state, error_msg=error_msg,
            errors=errors, dmg_command=dmg_command, uuid=remove_uuid)

        self.log_step("Verify that the disk spaces are back to the original.")
        uuid_to_total_bytes_after = self.get_uuid_to_total_byes(
            dmg_command=dmg_command, host_port=host_port)
        if uuid_to_total_bytes_after != uuid_to_total_bytes_orig:
            msg = (f"Disk sizes changed after hot remove! Orig = {uuid_to_total_bytes_orig}; "
                   f"After = {uuid_to_total_bytes_after}")
            errors.append(msg)

        self.log_step("Verify that none of the engines have crashed.")
        system_query_out = dmg_command.system_query()
        if not check_system_query_status(data=system_query_out):
            errors.append("One or more ranks crashed after hot remove and plug!")

        self.log_step("Verify that the disks are healthy by checking the container status.")
        expected_props = {"status": "HEALTHY"}
        container_healthy = self.container.verify_prop(expected_props=expected_props)
        if not container_healthy:
            errors.append("Container status isn't HEALTHY after hot remove and plug!")

        self.log_step("Rung IOR and check that it works.")
        cmd_result = self.run_ior_with_pool(create_pool=False, create_cont=False)
        if cmd_result.exit_status != 0:
            errors.append(f"IOR after hot-plug failed! cmd_result = {cmd_result}")

        self.log.info("##### Errors ######")
        report_errors(test=self, errors=errors)
