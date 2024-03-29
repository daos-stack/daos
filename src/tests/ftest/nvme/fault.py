'''
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from exception_utils import CommandFailure
from nvme_utils import ServerFillUp, get_device_ids, set_device_faulty


class NvmeFault(ServerFillUp):
    """
    Test Class Description: To validate IO works fine when NVMe fault generated
                            on single or multiple servers with single drive.
    :avocado: recursive
    """

    def test_nvme_fault(self):
        """Jira ID: DAOS-4722.

        Test Description: Test NVMe disk fault.
        Use Case: Create a large pool and start filling it up. While IO is in progress,
                  remove a single disk from a single server.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme
        :avocado: tags=NvmeFault,test_nvme_fault
        """
        pool_capacity = self.params.get('pool_capacity', '/run/faulttests/*')
        no_of_servers = self.params.get('no_of_servers', '/run/faulttests/*')
        no_of_drives = self.params.get('no_of_drives', '/run/faulttests/*')
        dmg = self.get_dmg_command()

        # Create the Pool with Maximum NVMe size
        self.log_step(f'Creating a pool using {pool_capacity}% of the free NVMe space')
        self.add_pool()
        self.result.clear()

        # Create the IOR threads
        self.log_step('Creating a thread to run I/O (ior)')
        ior_thread = self.create_ior_thread(operation="Auto_Write", percent=pool_capacity)

        # Set NVMe device faulty during IO
        self.log_step('Waiting 60 seconds before setting any devices faulty')
        time.sleep(60)

        # Determine which devices to set faulty
        self.log_step('Collecting device information')
        device_ids = get_device_ids(dmg, self.hostlist_servers)
        set_faulty_devices = []
        self.log.debug('Device information')
        for host in sorted(device_ids):
            if len(set_faulty_devices) < no_of_servers:
                set_faulty_devices.append({'host': host, 'devices': []})
            self.log.debug('  %s:', host)
            for uuid in sorted(device_ids[host]):
                _info = ''
                if len(set_faulty_devices[-1]['devices']) < no_of_drives:
                    set_faulty_devices[-1]['devices'].append(
                        {'uuid': uuid, 'has_sys_xs': device_ids[host][uuid]['has_sys_xs']})
                    _info = ' <-- set faulty'
                self.log.debug('    %s: %s%s', uuid, device_ids[host][uuid], _info)

        # Set the device faulty
        scan_nvme_health_hosts = self.hostlist_servers.copy()
        for entry in set_faulty_devices:
            _host = entry['host']
            for device in entry['devices']:
                _uuid = device['uuid']
                _has_sys_xs = device['has_sys_xs']
                self.log_step(f"Setting device {_uuid} faulty on {_host} and waiting for rebuild")
                set_device_faulty(self, dmg, _host, _uuid, self.pool, _has_sys_xs)
                if _has_sys_xs:
                    scan_nvme_health_hosts.remove(_host)

        # Wait to finish the IOR thread
        self.log_step('Waiting for I/O to complete (ior thread)')
        ior_thread.join()

        # Verify if any test failed for any IOR run
        self.log_step('Verify results from the ior thread')
        for test_result in self.result:
            if "FAIL" in test_result:
                self.fail(test_result)
        self.log.info("pool_percentage_used -- After -- %s", self.pool.pool_percentage_used())

        # Check nvme-health command works
        self.log_step('Checking device health (dmg storage scan nvme health)')
        try:
            dmg.hostlist = scan_nvme_health_hosts
            dmg.storage_scan_nvme_health()
        except CommandFailure as error:
            self.fail("dmg storage scan --nvme-health failed: {}".format(error))

        self.log.info('Test passed')
