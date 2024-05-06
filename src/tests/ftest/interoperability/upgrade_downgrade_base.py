'''
  (C) Copyright 2023-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import base64
import os
import random
import time
import traceback

from agent_utils import include_local_host
from command_utils_base import CommandFailure
from general_utils import get_random_bytes, pcmd, run_pcmd
from ior_test_base import IorTestBase
from pydaos.raw import DaosApiError


class UpgradeDowngradeBase(IorTestBase):
    # pylint: disable=global-variable-not-assigned,global-statement
    """
    Tests DAOS container attribute get/set/list.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ContainerAttributeTest object."""
        super().__init__(*args, **kwargs)
        self.daos_cmd = None
        self.upgrade_repo = ""
        self.downgrade_repo = ""

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
            size = random.randint(1, 10)  # nosec
            key = str(index).encode("utf-8")
            data_set[key] = get_random_bytes(size)
        return data_set

    def verify_list_attr(self, indata, attributes_list):
        """Verify the length of the Attribute names

        Args:
            indata (dict): Dict used to set attr
            attributes_list (list): List obtained from list attr
        """
        length = sum(map(len, indata.keys()))
        size = sum(map(len, attributes_list))

        self.log.info("==Verifying list_attr output:")
        self.log.info("  set_attr names:  %s", list(indata.keys()))
        self.log.info("  set_attr size:   %s", length)
        self.log.info("  list_attr names: %s", attributes_list)
        self.log.info("  list_attr size:  %s", size)

        if length != size:
            self.fail(
                "FAIL: Size does not match for Names in list attr, Expected "
                "len={} and received len={}".format(length, size))
        # verify the Attributes names in list_attr retrieve
        for key in indata.keys():
            if key.decode() not in attributes_list:
                self.fail(
                    "FAIL: Name does not match after list attr, Expected "
                    "buf={} and received buf={}".format(key, attributes_list))

    def verify_get_attr(self, indata, outdata):
        """verify the Attributes value after get_attr

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

    def verify_pool_attrs(self, pool_attr_dict):
        """"verify pool attributes

        Args:
            pool_attr_dict (dict): expected pool attributes data.
        """
        try:
            pool_attrs = self.daos_cmd.pool_list_attrs(pool=self.pool.uuid, verbose=True)
            self.verify_list_attr(pool_attr_dict, pool_attrs['response'])
        except DaosApiError as excep:
            self.log.info(excep)
            self.log.info(traceback.format_exc())
            self.fail("#Test failed at pool attributes verification.\n")

    def check_result(self, result):
        """check for command result, raise failure when error encountered

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
            all_hosts (NodeSet): all hosts.
            hosts_client (NodeSet): client hosts to show daos and dmg version.
        """
        result = run_pcmd(all_hosts, "rpm -qa | grep daos")
        self.check_result(result)
        result = run_pcmd(hosts_client, "dmg version")
        self.check_result(result)
        result = run_pcmd(hosts_client, "daos version")
        self.check_result(result)

    def updowngrade_via_repo(self, servers, clients, repo_1, repo_2):
        """Upgrade downgrade hosts

        Args:
            hosts (NodeSet): test hosts.
            updown (str): upgrade or downgrade
            repo_1 (str): path of the original repository and to be downgraded
            repo_2 (str): path of the new repository to be upgraded
        """
        repo_1_sav = repo_1 + "_sav"
        repo_2_sav = repo_2 + "_sav"
        cmds = [
            "sudo yum remove daos -y",
            "sudo mv {0} {1}".format(repo_1, repo_1_sav),
            "sudo mv {0} {1}".format(repo_2_sav, repo_2),
            "rpm -qa | grep daos",
            "sudo yum install daos-server-tests -y",
            "sudo yum install daos-tests -y",
            "rpm -qa | grep daos"]
        cmds_client = cmds + ["sudo yum install -y ior"]
        cmds_client += ["sudo cp /etc/daos/daos_agent.yml.rpmsave /etc/daos/daos_agent.yml"]
        cmds_client += ["sudo cp /etc/daos/daos_control.yml.rpmsave /etc/daos/daos_control.yml"]
        cmds_svr = cmds + ["sudo cp /etc/daos/daos_server.yml.rpmsave /etc/daos/daos_server.yml"]

        if servers:
            self.log.info("==upgrade_downgrading on servers: %s", servers)
            for cmd in cmds_svr:
                self.log.info("==cmd= %s", cmd)
                result = run_pcmd(servers, cmd)
            self.log.info("==servers pcmd yum upgrade/downgrade result= %s", result)
            # (5)Restart servers
            self.log.info("==Restart servers after upgrade/downgrade.")
            self.restart_servers()
        if clients:
            self.log.info("==upgrade_downgrading on hosts_client: %s", clients)
            for cmd in cmds_client:
                self.log.info("==cmd= %s", cmd)
                result = run_pcmd(clients, cmd)
            self.log.info("==clients pcmd yum upgrade/downgrade result= %s", result)

        self.log.info("==sleeping 5 more seconds after upgrade/downgrade")
        time.sleep(5)

    def upgrade(self, servers, clients):
        """Upgrade hosts via repository or RPMs

        Args:
            servers (NodeSet): servers to be upgraded.
            clients (NodeSet): clients to be upgraded.
        """
        if ".repo" in self.upgrade_repo:
            repo_2 = self.upgrade_repo
            repo_1 = self.downgrade_repo
            self.updowngrade_via_repo(servers, clients, repo_1, repo_2)
        else:
            all_hosts = servers + clients
            self.updowngrade_via_rpms(all_hosts, "upgrade", self.upgrade_repo)

    def downgrade(self, servers, clients):
        """Downgrade hosts via repository or RPMs

        Args:
            servers (NodeSet): servers to be upgraded.
            clients (NodeSet): clients to be upgraded.
        """
        if ".repo" in self.upgrade_repo:
            repo_1 = self.upgrade_repo
            repo_2 = self.downgrade_repo
            self.updowngrade_via_repo(servers, clients, repo_1, repo_2)
        else:
            all_hosts = servers + clients
            self.updowngrade_via_rpms(all_hosts, "downgrade", self.downgrade_repo)

    def updowngrade_via_rpms(self, hosts, updown, rpms):
        """Upgrade downgrade hosts

        Args:
            hosts (NodeSet): test hosts.
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
            host (NodeSet): test host.
        """
        cmds = [
            "daos version",
            "dmg version",
            "daos pool query {}".format(self.pool.identifier)]
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

    def verify_daos_libdaos(self, step, hosts_client, cmd, positive_test, agent_server_ver,
                            exp_err=None):
        """Verify daos and libdaos interoperability between different version of agent and server.

        Args:
            step (str): test step for logging.
            hosts_client (NodeSet): clients to launch test.
            cmd (str): command to launch.
            positive_test (bool): True for positive test, false for negative test.
            agent_server_ver (str): agent and server version.
            exp_err (str): expected error message for negative testcase.
        """
        if positive_test:
            self.log.info("==(%s)Positive_test: %s, on %s", step, cmd, agent_server_ver)
        else:
            self.log.info("==(%s)Negative_test: %s, on %s", step, cmd, agent_server_ver)
        return1 = run_pcmd(hosts_client, cmd)
        if positive_test:
            if return1[0]['exit_status']:
                self.fail("##({0})Test failed, {1}, on {2}".format(step, cmd, agent_server_ver))
        else:
            self.log.info("-->return1= %s", return1)
            if not return1[0]['exit_status']:
                self.fail("##({0})Test failed, {1}, on {2}".format(step, cmd, agent_server_ver))
            if exp_err not in return1[0]['stdout'][0]:
                self.fail("##({0})Test failed, {1}, on {2}, expect_err {3} "
                          "not shown on stdout".format(step, cmd, agent_server_ver, exp_err))
        self.log.info("==(%s)Test passed, %s, on %s", step, cmd, agent_server_ver)

    def has_fault_injection(self, hosts):
        """Check if RPMs with fault-injection function.

        Args:
            hosts (string, list): client hosts to execute the command.
        """
        status = True
        result = run_pcmd(hosts, "daos_debug_set_params -v 67174515")
        self.log.info("--check_result, result= %s", result)
        if result[0]['stdout'] == []:
            self.log.info("#Host client rpms did not have fault-injection")
            status = False
        return status

    def enable_disable_fault_injection(self, hosts, enable=True):
        """Enable and disable fault injection.

        Args:
            hosts (string, list): client hosts to execute the command.
            enable (Bool): enable or disable the fault injection
        """
        if enable:
            result = run_pcmd(hosts, "daos_debug_set_params -v 67174515")
        else:
            result = run_pcmd(hosts, "daos_debug_set_params -v 67108864")
        self.check_result(result)

    def verify_pool_upgrade_status(self, pool_id, expected_status):
        """Verify pool upgrade status.

        Args:
            pool_id (str): pool to be verified.
            expected_status (str): pool upgrade expected status.
        """
        prop_value = self.get_dmg_command().pool_get_prop(
            pool_id, "upgrade_status")['response'][0]['value']
        if prop_value != expected_status:
            self.fail("##prop_value != expected_status {}".format(expected_status))

    def pool_upgrade_with_fault(self, hosts, pool_id):
        """Execute dmg pool upgrade with fault injection.

        Args:
            hosts (string, list): client hosts to execute the command.
            pool_id (str): pool to be upgraded
        """
        # Verify pool status before upgrade
        expected_status = "not started"
        self.verify_pool_upgrade_status(pool_id, expected_status)

        # Enable fault-injection
        self.enable_disable_fault_injection(hosts, enable=True)

        # Pool upgrade
        result = run_pcmd(hosts, "dmg pool upgrade {}".format(pool_id))
        self.check_result(result)
        # Verify pool status during upgrade
        expected_status = "in progress"
        self.verify_pool_upgrade_status(pool_id, expected_status)
        # Verify pool status during upgrade
        expected_status = "failed"
        self.verify_pool_upgrade_status(pool_id, expected_status)

        # Disable fault-injection
        self.enable_disable_fault_injection(hosts, enable=False)
        # Verify pool upgrade resume after removal of fault-injection
        expected_status = "completed"
        self.verify_pool_upgrade_status(pool_id, expected_status)

    def diff_versions_agent_server(self):
        """Interoperability of different versions of DAOS agent and server.
        Test step:
            (1)Setup
            (2)dmg system stop
            (3)Upgrade 1 server-host to new version
            (4)Negative test - dmg pool query on mix-version servers
            (5)Upgrade rest server-hosts to 2.2
            (6)Restart 2.0 agent
            (7)Verify 2.0 agent connect to 2.2 server, daos and libdaos
            (8)Upgrade agent to 2.2
            (9)Verify pool and containers create on 2.2 agent and server
            (10)Downgrade server to 2.0
            (11)Verify 2.2 agent to 2.0 server, daos and libdaos
            (12)Downgrade agent to 2.0

        """
        # (1)Setup
        self.log.info("==(1)Setup, create pool and container.")
        hosts_client = self.hostlist_clients
        hosts_server = self.hostlist_servers
        all_hosts = include_local_host(hosts_server | hosts_client)
        self.upgrade_repo = self.params.get("upgrade_repo", '/run/interop/*')
        self.downgrade_repo = self.params.get("downgrade_repo", '/run/interop/*')
        self.add_pool(connect=False)
        pool_id = self.pool.identifier
        self.add_container(self.pool)
        self.container.open()
        cmd = "dmg system query"
        positive_test = True
        negative_test = False
        agent_server_ver = "2.0 agent to 2.0 server"
        self.verify_daos_libdaos("1.1", hosts_client, cmd, positive_test, agent_server_ver)

        # (2)dmg system stop
        self.log.info("==(2)Dmg system stop.")
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))

        # (3)Upgrade 1 server-host to new
        self.log.info("==(3)Upgrade 1 server to 2.2.")
        server = hosts_server[0:1]
        self.upgrade(server, [])
        self.log.info("==(3.1)server %s Upgrade to 2.2 completed.", server)

        # (4)Negative test - dmg pool query on mix-version servers
        self.log.info("==(4)Negative test - dmg pool query on mix-version servers.")
        agent_server_ver = "2.0 agent, mix-version server-hosts"
        cmd = "dmg pool list"
        exp_err = "unable to contact the DAOS Management Service"
        self.verify_daos_libdaos(
            "4.1", hosts_client, cmd, negative_test, agent_server_ver, exp_err)

        # (5)Upgrade rest server-hosts to 2.2
        server = hosts_server[1:len(hosts_server)]
        self.log.info("==(5) Upgrade rest server %s to 2.2.", server)
        self.upgrade(server, [])
        self.log.info("==(5.1) server %s Upgrade to 2.2 completed.", server)

        # (6)Restart 2.0 agent
        self.log.info("==(6)Restart 2.0 agent")
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)

        # (7)Verify 2.0 agent connect to 2.2 server
        self.log.info("==(7)Verify 2.0 agent connect to 2.2 server")
        agent_server_ver = "2.0 agent to 2.2 server"
        cmd = "daos pool query {0}".format(pool_id)
        self.verify_daos_libdaos("7.1", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "dmg pool query {0}".format(pool_id)
        exp_err = "admin:0.0.0 are not compatible"
        self.verify_daos_libdaos(
            "7.2", hosts_client, cmd, negative_test, agent_server_ver, exp_err)
        cmd = "sudo daos_agent dump-attachinfo"
        self.verify_daos_libdaos("7.3", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos cont create {0} --type POSIX --properties 'rf:2'".format(pool_id)
        self.verify_daos_libdaos("7.4", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos pool autotest --pool {0}".format(pool_id)
        self.verify_daos_libdaos("7.5", hosts_client, cmd, positive_test, agent_server_ver)

        # (8)Upgrade agent to 2.2
        self.log.info("==(8)Upgrade agent to 2.2, now 2.2 servers 2.2 agent.")
        self.upgrade([], hosts_client)
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)

        # (9)Pool and containers create on 2.2 agent and server
        self.log.info("==(9)Create new pools and containers on 2.2 agent to 2.2 server")
        agent_server_ver = "2.2 agent to 2.2 server"
        cmd = "dmg pool create --size 5G New_pool1"
        self.verify_daos_libdaos("9.1", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "dmg pool list"
        self.verify_daos_libdaos("9.2", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos cont create New_pool1 C21 --type POSIX --properties 'rf:2'"
        self.verify_daos_libdaos("9.3", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos cont create New_pool1 C22 --type POSIX --properties 'rf:2'"
        self.verify_daos_libdaos("9.4", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos container list New_pool1"
        self.verify_daos_libdaos("9.5", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "sudo daos_agent dump-attachinfo"
        self.verify_daos_libdaos("9.6", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos pool autotest --pool New_pool1"
        self.verify_daos_libdaos("9.7", hosts_client, cmd, positive_test, agent_server_ver)

        # (10)Downgrade server to 2.0
        self.log.info("==(10)Downgrade server to 2.0, now 2.2 agent to 2.0 server.")
        self.log.info("==(10.1)Dmg system stop.")
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))
        self.log.info("==(10.2)Downgrade server to 2.0")
        self.downgrade(hosts_server, [])
        self.log.info("==(10.3)Restart 2.0 agent")
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)

        # (11)Verify 2.2 agent to 2.0 server
        agent_server_ver = "2.2 agent to 2.0 server"
        cmd = "daos pool query {0}".format(pool_id)
        self.verify_daos_libdaos("11.1", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "dmg pool query {0}".format(pool_id)
        exp_err = "does not match"
        self.verify_daos_libdaos(
            "11.2", hosts_client, cmd, negative_test, agent_server_ver, exp_err)
        cmd = "sudo daos_agent dump-attachinfo"
        self.verify_daos_libdaos("11.3", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos cont create {0} 'C_oldP' --type POSIX --properties 'rf:2'".format(
            pool_id)
        self.verify_daos_libdaos("11.4", hosts_client, cmd, positive_test, agent_server_ver)
        cmd = "daos cont create New_pool1 'C_newP' --type POSIX --properties 'rf:2'"
        exp_err = "DER_NO_SERVICE(-2039)"
        self.verify_daos_libdaos(
            "11.5", hosts_client, cmd, negative_test, agent_server_ver, exp_err)
        exp_err = "common ERR"
        cmd = "daos pool autotest --pool {0}".format(pool_id)
        self.verify_daos_libdaos(
            "11.6", hosts_client, cmd, negative_test, agent_server_ver, exp_err)

        # (12)Downgrade agent to 2.0
        self.log.info("==(12)Agent %s  Downgrade started.", hosts_client)
        self.downgrade([], hosts_client)
        self.log.info("==Test passed")

    def upgrade_and_downgrade(self, fault_on_pool_upgrade=False):
        """upgrade and downgrade test base.
        Test step:
            (1)Setup and show rpm, dmg and daos versions on all hosts
            (2)Create pool, container and pool attributes
            (3)Setup and run IOR
                (3.a)DFS
                (3.b)HDF5
                (3.c)POSIX symlink to a file
            (4)Dmg system stop
            (5)Upgrade RPMs to specified new version
            (6)Restart servers
            (7)Restart agent
                verify pool attributes
                verify IOR data integrity after upgraded
                (7.a)DFS
                (7.b)HDF5
                (7.c)POSIX symlink to a file
            (8)Dmg pool get-prop after RPMs upgraded before Pool upgraded
            (9)Dmg pool upgrade and verification after RPMs upgraded
                (9.a)Enable fault injection during pool upgrade
                (9.b)Normal pool upgrade without fault injection
            (10)Create new pool after rpms Upgraded
            (11)Downgrade and cleanup
            (12)Restart servers and agent

        Args:
            fault_on_pool_upgrade (bool): Enable fault-injection during pool upgrade.
        """
        # (1)Setup
        self.log.info("(1)==Setup and show rpm, dmg and daos versions on all hosts.")
        hosts_client = self.hostlist_clients
        hosts_server = self.hostlist_servers
        all_hosts = include_local_host(hosts_server)
        self.upgrade_repo = self.params.get("upgrade_repo", '/run/interop/*')
        self.downgrade_repo = self.params.get("downgrade_repo", '/run/interop/*')
        num_attributes = self.params.get("num_attributes", '/run/attrtests/*')
        ior_api = self.params.get("api", '/run/ior/*')
        mount_dir = self.params.get("mount_dir", '/run/dfuse/*')
        self.show_daos_version(all_hosts, hosts_client)

        # (2)Create pool container and pool attributes
        self.log.info("(2)==Create pool attributes.")
        self.add_pool(connect=False)
        pool_id = self.pool.identifier
        self.add_container(self.pool)
        self.container.open()
        self.daos_cmd = self.get_daos_command()
        pool_attr_dict = self.create_data_set(num_attributes)
        self.pool.pool.set_attr(data=pool_attr_dict)
        self.verify_pool_attrs(pool_attr_dict)
        self.container.close()
        self.pool.disconnect()

        # (3)Setup and run IOR
        self.log.info("(3)==Setup and run IOR.")
        result = run_pcmd(hosts_client, "mkdir -p {}".format(mount_dir))
        ior_timeout = self.params.get("ior_timeout", '/run/ior/*')
        iorflags_write = self.params.get("write_flg", '/run/ior/iorflags/*')
        iorflags_read = self.params.get("read_flg", '/run/ior/iorflags/*')
        testfile = os.path.join(mount_dir, "testfile")
        testfile_sav = os.path.join(mount_dir, "testfile_sav")
        testfile_sav2 = os.path.join(mount_dir, "testfile_sav2")
        symlink_testfile = os.path.join(mount_dir, "symlink_testfile")
        # (3.a)ior dfs
        if ior_api in ("DFS", "POSIX"):
            self.log.info("(3.a)==Run non-HDF5 IOR write and read.")
            self.ior_cmd.flags.update(iorflags_write)
            self.run_ior_with_pool(
                timeout=ior_timeout, create_pool=True, create_cont=True, stop_dfuse=False)
            self.ior_cmd.flags.update(iorflags_read)
            self.run_ior_with_pool(
                timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)

        # (3.b)ior hdf5
        elif ior_api == "HDF5":
            self.log.info("(3.b)==Run IOR HDF5 write and read.")
            hdf5_plugin_path = self.params.get("plugin_path", '/run/hdf5_vol/')
            self.ior_cmd.flags.update(iorflags_write)
            self.run_ior_with_pool(
                plugin_path=hdf5_plugin_path, mount_dir=mount_dir,
                timeout=ior_timeout, create_pool=True, create_cont=True, stop_dfuse=False)
            self.ior_cmd.flags.update(iorflags_read)
            self.run_ior_with_pool(
                plugin_path=hdf5_plugin_path, mount_dir=mount_dir,
                timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)
        else:
            self.fail("##(3)Unsupported IOR api {}".format(ior_api))

        # (3.c)ior posix test file with symlink
        if ior_api == "POSIX":
            self.log.info("(3.c)==Symlink mounted testfile.")
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

        # Verify pool attributes before upgrade
        self.log.info("(3.2)==verify pool attributes before upgrade.")
        self.verify_pool_attrs(pool_attr_dict)

        # (4)dmg system stop
        self.log.info("(4)==Dmg system stop.")
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))

        # (5)Upgrade
        self.log.info("(5)==Upgrade RPMs to 2.2.")
        self.upgrade(hosts_server, hosts_client)

        self.log.info("==sleeping 30 more seconds")
        time.sleep(30)
        # (6)Restart servers
        self.log.info("(6)==Restart servers.")
        self.restart_servers()

        # (7)Verification after upgrade
        # Restart agent
        self.log.info("(7.1)====Restarting rel_2.2 agent after upgrade.")
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)

        self.get_dmg_command().pool_list(verbose=True)
        self.get_dmg_command().pool_query(pool=pool_id)
        self.daos_cmd.pool_query(pool=pool_id)

        # Verify pool attributes
        self.log.info("(7.2)====Verifying pool attributes after upgrade.")
        self.verify_pool_attrs(pool_attr_dict)
        self.daos_ver_after_upgraded(hosts_client)

        # Verify IOR data and symlink
        self.log.info("(7.3)====Verifying container data IOR read.")
        if ior_api == "DFS":
            self.log.info("(7.a)==Run IOR DFS read verification.")
            self.run_ior_with_pool(
                timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)
        elif ior_api == "HDF5":
            self.log.info("(7.b)==Run IOR HDF5 read verification.")
            self.run_ior_with_pool(
                plugin_path=hdf5_plugin_path, mount_dir=mount_dir,
                timeout=ior_timeout, create_pool=False, create_cont=False, stop_dfuse=False)
        else:
            self.log.info("(7.c)==Run Symlink check after upgraded.")
            result = run_pcmd(
                hosts_client,
                "dfuse --mountpoint {0} --pool {1} --container {2}".format(
                    mount_dir, pool_id, self.container))
            self.check_result(result)
            result = run_pcmd(hosts_client, "diff {0} {1}".format(testfile, testfile_sav))
            self.check_result(result)
            result = run_pcmd(hosts_client, "diff {0} {1}".format(symlink_testfile, testfile_sav2))
            self.check_result(result)

        # (8)Dmg pool get-prop
        self.log.info("(8)==Dmg pool get-prop after RPMs upgraded before Pool upgraded")
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool_id))
        self.check_result(result)

        # (9)Pool property verification after upgraded
        self.log.info("(9)==Dmg pool upgrade and get-prop after RPMs upgraded")

        if fault_on_pool_upgrade and self.has_fault_injection(hosts_client):
            self.log.info("(9.1a)==Pool upgrade with fault-injection.")
            self.pool_upgrade_with_fault(hosts_client, pool_id)
        else:
            self.log.info("(9.1b)==Pool upgrade.")
            result = run_pcmd(hosts_client, "dmg pool upgrade {}".format(pool_id))
            self.check_result(result)
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool_id))
        self.check_result(result)
        self.log.info("(9.2)==verify pool attributes after pool-upgraded.")
        self.verify_pool_attrs(pool_attr_dict)
        self.pool.destroy()

        # (10)Create new pool
        self.log.info("(10)==Create new pool after rpms Upgraded")
        self.add_pool(connect=False)
        pool2_id = self.pool.identifier
        self.get_dmg_command().pool_list(verbose=True)
        self.get_dmg_command().pool_query(pool=pool2_id)
        self.daos_cmd.pool_query(pool=pool2_id)
        result = run_pcmd(hosts_client, "dmg pool get-prop {}".format(pool2_id))
        self.check_result(result)

        # (11)Downgrade and cleanup
        self.log.info("(11)==Downgrade and cleanup.")
        if ior_api == "POSIX":
            result = run_pcmd(hosts_client, "fusermount3 -u {}".format(mount_dir))
            self.check_result(result)
        self.container.close()
        self.pool.disconnect()
        self.pool.destroy()
        self.get_dmg_command().system_stop()
        errors = []
        errors.extend(self._stop_managers(self.server_managers, "servers"))
        errors.extend(self._stop_managers(self.agent_managers, "agents"))
        self.log.info("(11.1)==Downgrade RPMs to 2.0.3.")
        self.downgrade(hosts_server, hosts_client)
        self.log.info("==sleeping 30 more seconds")
        time.sleep(30)

        # (12)Cleanup restart server and agent
        self.log.info("(12)==Restart 2.0 servers and agent.")
        self.restart_servers()
        self._start_manager_list("agent", self.agent_managers)
        self.show_daos_version(all_hosts, hosts_client)
        if fault_on_pool_upgrade and not self.has_fault_injection(hosts_client):
            self.fail("##(12)Upgraded-rpms did not have fault-injection feature.")
        self.log.info("==(12)Test passed")
