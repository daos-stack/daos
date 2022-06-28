'''
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import traceback
import random
import base64
import time

from general_utils import get_random_bytes, pcmd, run_pcmd
from pydaos.raw import DaosApiError
from agent_utils import include_local_host
from command_utils_base import CommandFailure
from ior_test_base import IorTestBase

# pylint: disable=global-variable-not-assigned,global-statement
# pylint: disable=too-many-ancestors
class UpgradeDowngradeTest(IorTestBase):
    """
    Tests DAOS container attribute get/set/list.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ContainerAttributeTest object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None

    @staticmethod
    def create_data_set(num_attributes):
        """Create the large attribute dictionary.

        Args:
            num_attributes (int): number of attributes to be created on container.
        Returns:
            dict: a large attribute dictionary

        """
        data_set = {}
        for index in range(num_attributes):
            size = random.randint(1, 10) # nosec
            key = str(index).encode("utf-8")
            data_set[key] = get_random_bytes(size)
        return data_set

    def verify_list_attr(self, indata, attributes_list):
        """
        Verify the length of the Attribute names

        Args:
            indata (dict): Dict used to set attr
            attributes_list (list): List obtained from list attr
        """
        length = sum(map(len, indata.keys()))
        size = sum(map(len, attributes_list))

        self.log.info("Verifying list_attr output:")
        self.log.info("  set_attr names:  %s", list(indata.keys()))
        self.log.info("  set_attr size:   %s", length)
        self.log.info("  list_attr names: %s", attributes_list)
        self.log.info("  list_attr size:  %s", size)

        if length != size:
            self.fail(
                "FAIL: Size does not matching for Names in list attr, Expected "
                "len={} and received len={}".format(length, size))
        # verify the Attributes names in list_attr retrieve
        for key in indata.keys():
            if key.decode() not in attributes_list:
                self.fail(
                    "FAIL: Name does not match after list attr, Expected "
                    "buf={} and received buf={}".format(key, attributes_list))

    def verify_get_attr(self, indata, outdata):
        """
        verify the Attributes value after get_attr

        Args:
             indata (dict): In data item of container get_attr.
             outdata (dict): Out data from container get_attr.
        """
        decoded = {}
        for key, val in outdata.items():
            if isinstance(val, bytes):
                decoded[key.decode()] = val
            else:
                decoded[key] = base64.b64decode(val)

        self.log.info("Verifying get_attr output:")
        self.log.info("  get_attr data: %s", indata)
        self.log.info("  set_attr data: %s", decoded)

        for attr, value in indata.items():
            if value != decoded.get(attr.decode(), None):
                self.fail(
                    "FAIL: Value does not match after get({}), Expected "
                    "val={} and received val={}".format(attr, value,
                                        decoded.get(attr.decode(), None)))

    def check_result(self, result):
        """check for command result, raise failure when error cncountered

        Args:
             result (dict): dictionary of result to check.
        """
        self.log.info("--check_result, result= %s", result)
        if result[0]['exit_status'] != 0:
            self.fail("##Error detected from check_result")
        else:
            self.log.info("--check_result passed")

    def show_daos_version(self, all_hosts, hosts_client):
        """show daos version

        Args:
            all_hosts (list): all hosts.
            hosts_client (list): client hosts to show daos and dmg version.
        """
        result = run_pcmd(all_hosts, "rpm -qa | grep daos")
        self.check_result(result)
        result = run_pcmd(hosts_client, "dmg version")
        self.check_result(result)
        result = run_pcmd(hosts_client, "daos version")
        self.check_result(result)

    def updowngrade(self, hosts, updown, rpms):
        """Upgrade downgrade hosts

        Args:
            hosts (list): test hosts.
            updown (str): upgrade or downgrade
            rpms (list): full path of RPMs to be upgrade or downgrade
        """
        cmds = []
        for rpm in rpms:
            cmds.append("sudo yum {} -y {}".format(updown, rpm))
        cmds.append("sudo ipcrm -a")
        cmds.append("sudo ipcs")
        self.log.info("==upgrade_downgrading on hosts: %s", hosts)
        for cmd in cmds:
            self.log.info("==cmd= %s", cmd)
            result = run_pcmd(hosts, cmd)
        self.log.info("==sleeping 5 more seconds")
        time.sleep(5)
        self.log.info("==pcmd yum upgrade/downgrade result= %s", result)

    def daos_ver_after_upgraded(self, host):
        """To display daos and dmg version, and check for error.

        Args:
            host (str): test host.
        """
        cmds = [
                "daos version",
                "dmg version",
                "daos pool query {}".format(self.pool.identifier)
               ]
        for cmd in cmds:
            self.log.info("==cmd= %s", cmd)
            result = pcmd(host, cmd, False)
            if 0 not in result or len(result) > 1:
                failed = []
                for item, value in list(result.items()):
                    if item != 0:
                        failed.extend(value)
                raise CommandFailure("##Error occurred running '{}' on {}".format(
                    cmd, host))
            self.log.info("==>%s result= %s", cmd, result)

    def test_upgrade_downgrade(self):
        """
        Test ID: DAOS-10274: DAOS interoperability test user interface test
                 DAOS-10275: DAOS interoperability test system and pool upgrade basic
                 DAOS-10280: DAOS upgrade from 2.0 to 2.2 and downgrade back to 2.0
        Test description:
                 (1)Show rpm, dmg and daos versions on all hosts
                 (2)Create pool containers and attributes, setup and run IOR
                 (3)Dmg system stop
                 (4)Upgrade RPMs to specified version
                 (5)Restart servers
                 (6)Restart agent
                    verify pool and container attributes
                    verify IOR data integrity and data of symlink after upgraded
                 (7)Dmg pool get-prop after RPMs upgraded before Pool upgraded
                 (8)Dmg pool upgrade and get-prop after RPMs upgraded
                 (9)Create new pool after rpms Upgraded
                 (10)Downgrade and cleanup
                 (11)Restart servers and agent
        To launch test:
                 (1)sudo yum install all needed RPMs to all hosts
                 (2)define RPMs to updown_grade.yaml
                 (3)./launch.py upgrade_downgrade -ts boro-[..] -tc boro-[..]
        :avocado: tags=manual
        :avocado: tags=interop
        :avocado: tags=upgrade_downgrade
        """
        # (1)Setup
        hosts_client = self.hostlist_clients
        hosts_server = self.hostlist_servers
        all_hosts = include_local_host(hosts_server)
        upgd_rpms = self.params.get("upgrade_rpms", '/run/interop/*')
        downgd_rpms = self.params.get("downgrade_rpms", '/run/interop/*')
        num_attributes = self.params.get("num_attributes", '/run/attrtests/*')
        mount_dir = self.params.get("mount_dir", '/run/dfuse/*')
        self.log.info("(1)==Show rpm, dmg and daos versions on all hosts.")
        self.show_daos_version(all_hosts, hosts_client)

        # (2)Create pool containers and attributes
        self.log.info("(2)==Create pool containers and attributes.")
        self.add_pool(connect=False)
        pool_id = self.pool.identifier
        self.add_container(self.pool)
        self.container.open()
        self.daos_cmd = self.get_daos_command()
        attr_dict = self.create_data_set(num_attributes)

        try:
            self.container.container.set_attr(data=attr_dict)
            data = self.daos_cmd.container_list_attrs(
                pool=self.pool.uuid,
                cont=self.container.uuid,
                verbose=False)
            self.verify_list_attr(attr_dict, data['response'])

            data = self.daos_cmd.container_list_attrs(
                pool=self.pool.uuid,
                cont=self.container.uuid,
                verbose=True)
            self.verify_get_attr(attr_dict, data['response'])
        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
        self.container.close()
        self.pool.disconnect()

        # IOR before upgrade
        self.log.info("(2.1)==Setup and run IOR.")
        result = run_pcmd(hosts_client, "mkdir -p {}".format(mount_dir))
        ior_timeout = self.params.get("ior_timeout", '/run/ior/*')
        iorflags_write = self.params.get("write_flg", '/run/ior/iorflags/')
        dfs_oclass = self.params.get("dfs_oclass", '/run/ior/*')
        testfile = os.path.join(mount_dir, "testfile")
        testfile_sav = os.path.join(mount_dir, "testfile_sav")
        testfile_sav2 = os.path.join(mount_dir, "testfile_sav2")
        symlink_testfile = os.path.join(mount_dir, "symlink_testfile")
        self.ior_cmd.flags.update(iorflags_write)
        self.run_ior_with_pool(
            timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)
        result = run_pcmd(hosts_client, "cd {}".format(mount_dir))
        result = run_pcmd(hosts_client, "ls -l {}".format(testfile))
        result = run_pcmd(hosts_client, "cp {0} {1}".format(testfile, testfile_sav))
        self.check_result(result)
        result = run_pcmd(hosts_client, "cp {0} {1}".format(testfile, testfile_sav2))
        self.check_result(result)
        result = run_pcmd(
            hosts_client, "ln -vs {0} {1}".format(testfile_sav2, symlink_testfile))
        result = run_pcmd(hosts_client, "diff {0} {1}".format(testfile, testfile_sav))
        self.check_result(result)
        result = run_pcmd(hosts_client, "ls -l {}".format(symlink_testfile))
        self.check_result(result)
        self.container.close()
        self.pool.disconnect()
        result = run_pcmd(hosts_client, "fusermount3 -u {}".format(mount_dir))
        self.check_result(result)

        # (3)dmg system stop
        self.log.info("(3)==Dmg system stop.")
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))

        # (4)Upgrade
        self.log.info("(4)==Upgrade RPMs to .")
        self.updowngrade(all_hosts, "upgrade", upgd_rpms)

        # (5)Restart servers
        self.log.info("(5)==Restart servers.")
        self.restart_servers()

        # (6)Verification after upgraded
        self.log.info("(6)==verify pool and container attributes after upgraded.")
        # Restart agent
        self.log.info("(6.1)====Restarting rel_2.2 agent after upgrade.")
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)

        self.get_dmg_command().pool_list(verbose=True)
        self.get_dmg_command().pool_query(pool=pool_id)
        self.daos_cmd.pool_query(pool=pool_id)

        # Verify pool container attributes
        self.log.info("(6.2)====Verifying container attributes after upgrade.")
        data = self.daos_cmd.container_list_attrs(
            pool=self.pool.uuid,
            cont=self.container.uuid,
            verbose=True)
        self.verify_get_attr(attr_dict, data['response'])
        self.daos_ver_after_upgraded(hosts_client)

        # Verify IOR data and symlink
        self.log.info("(6.3)====Verifying container IOR data and symlink.")
        result = run_pcmd(
            hosts_client,
            "dfuse --mountpoint {0} --pool {1} --container {2}".format(
                mount_dir, pool_id, self.container))
        self.check_result(result)
        result = run_pcmd(hosts_client, "diff {0} {1}".format(testfile, testfile_sav))
        self.check_result(result)
        result = run_pcmd(hosts_client, "diff {0} {1}".format(symlink_testfile, testfile_sav2))
        self.check_result(result)

        # (7)Dmg pool get-prop
        self.log.info("(7)==Dmg pool get-prop after RPMs upgraded before Pool upgraded")
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool_id))
        self.check_result(result)

        # (8)Pool property verification after upgraded
        self.log.info("(8)==Dmg pool upgrade and get-prop after RPMs upgraded")
        result = run_pcmd(hosts_client, "dmg pool upgrade {}".format(pool_id))
        self.check_result(result)
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool_id))
        self.check_result(result)

        # (9)Create new pool
        self.log.info("(9)==Create new pool after rpms Upgraded")
        self.add_pool(connect=False)
        pool2_id = self.pool.identifier
        self.get_dmg_command().pool_list(verbose=True)
        self.get_dmg_command().pool_query(pool=pool2_id)
        self.daos_cmd.pool_query(pool=pool2_id)
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool2_id))
        self.check_result(result)

        # (10)Downgrade and cleanup
        self.log.info("(10)==Downgrade and cleanup.")
        result = run_pcmd(hosts_client, "fusermount3 -u {}".format(mount_dir))
        self.check_result(result)
        self.container.close()
        self.pool.disconnect()
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))
        self.updowngrade(all_hosts, "downgrade", downgd_rpms)

        # (11)Restart server and agent for the cleanup
        self.log.info("(11)==Restart 2.0 servers and agent.")
        self.restart_servers()
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)
