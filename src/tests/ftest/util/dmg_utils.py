#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
from __future__ import print_function

from getpass import getuser
from grp import getgrgid
from pwd import getpwuid
import re

from command_utils_base import CommandFailure
from dmg_utils_base import DmgCommandBase


class DmgCommand(DmgCommandBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Defines a object representing a dmg command with helper methods."""

    def network_scan(self, provider=None, all_devs=False):
        """Get the result of the dmg network scan command.

        Args:
            provider (str): name of network provider tied to the device
            all_devs (bool, optional): Show all device info. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.set_sub_command("network")
        self.sub_command_class.set_sub_command("scan")
        self.sub_command_class.sub_command_class.provider.value = provider
        self.sub_command_class.sub_command_class.all.value = all_devs
        return self._get_result()

    def storage_scan(self, verbose=False):
        """Get the result of the dmg storage scan command.

        Args:
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("scan")
        self.sub_command_class.sub_command_class.verbose.value = verbose
        return self._get_result()

    def storage_format(self, reformat=False):
        """Get the result of the dmg storage format command.

        Args:
            reformat (bool): always reformat storage, could be destructive.
                This will create control-plane related metadata i.e. superblock
                file and reformat if the storage media is available and
                formattable.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("format")
        self.sub_command_class.sub_command_class.reformat.value = reformat
        return self._get_result()

    def storage_prepare(self, user=None, hugepages="4096", nvme=False,
                        scm=False, reset=False, force=True):
        """Get the result of the dmg storage format command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("prepare")
        self.sub_command_class.sub_command_class.nvme_only.value = nvme
        self.sub_command_class.sub_command_class.scm_only.value = scm
        self.sub_command_class.sub_command_class.target_user.value = \
            getuser() if user is None else user
        self.sub_command_class.sub_command_class.hugepages.value = hugepages
        self.sub_command_class.sub_command_class.reset.value = reset
        self.sub_command_class.sub_command_class.force.value = force
        return self._get_result()

    def storage_set_faulty(self, uuid):
        """Get the result of the 'dmg storage set nvme-faulty' command.

        Args:
            uuid (str): Device UUID to query.
        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("set")
        self.sub_command_class.sub_command_class.set_sub_command("nvme-faulty")
        self.sub_command_class. \
            sub_command_class.sub_command_class.uuid.value = uuid
        return self._get_result()

    def storage_query_list_devices(self, rank=None, health=False):
        """Get the result of the 'dmg storage query list-devices' command.

        Args:
            rank (int, optional): Limit response to devices on this rank.
                Defaults to None.
            health (bool, optional): Include device health in response.
                Defaults to false.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.set_sub_command("list-devices")
        self.sub_command_class. \
            sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class. \
            sub_command_class.sub_command_class.health.value = health
        return self._get_result()

    def storage_query_list_pools(self, uuid=None, rank=None, verbose=False):
        """Get the result of the 'dmg storage query list-pools' command.

        Args:
            uuid (str): Device UUID to query. Defaults to None.
            rank (int, optional): Limit response to pools on this rank.
                Defaults to None.
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.set_sub_command("list-pools")
        self.sub_command_class. \
            sub_command_class.sub_command_class.uuid.value = uuid
        self.sub_command_class. \
            sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class. \
            sub_command_class.sub_command_class.verbose.value = verbose
        return self._get_result()

    def storage_query_device_health(self, uuid):
        """Get the result of the 'dmg storage query device-health' command.

        Args:
            uuid (str): Device UUID to query.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class. \
            sub_command_class.set_sub_command("device-health")
        self.sub_command_class. \
            sub_command_class.sub_command_class.uuid.value = uuid
        return self._get_result()

    def storage_query_target_health(self, rank, tgtid):
        """Get the result of the 'dmg storage query target-health' command.

        Args:
            rank (int): Rank hosting target.
            tgtid (int): Target index to query.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class. \
            sub_command_class.set_sub_command("device-health")
        self.sub_command_class. \
            sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class. \
            sub_command_class.sub_command_class.tgtid.value = tgtid
        return self._get_result()

    def storage_query_nvme_health(self):
        """Get the result of the 'dmg storage query nvme-health' command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        self.set_sub_command("storage")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class. \
            sub_command_class.set_sub_command("nvme-health")
        return self._get_result()

    def pool_create(self, scm_size, uid=None, gid=None, nvme_size=None,
                    target_list=None, svcn=None, group=None, acl_file=None):
        """Create a pool with the dmg command.

        The uid and gid method arguments can be specified as either an integer
        or a string.  If an integer value is specified it will be converted into
        the corresponding user/group name string.

        Args:
            scm_size (int): SCM pool size to create.
            uid (object, optional): User ID with privileges. Defaults to None.
            gid (object, optional): Group ID with privileges. Defaults to None.
            nvme_size (str, optional): NVMe size. Defaults to None.
            target_list (list, optional): a list of storage server unique
                identifiers (ranks) for the DAOS pool
            svcn (str, optional): Number of pool service replicas. Defaults to
                None, in which case 1 is used by the dmg binary in default.
            group (str, optional): DAOS system group name in which to create the
                pool. Defaults to None, in which case "daos_server" is used by
                default.
            acl_file (str, optional): ACL file. Defaults to None.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg pool create command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("create")
        self.sub_command_class.sub_command_class.user.value = \
            getpwuid(uid).pw_name if isinstance(uid, int) else uid
        self.sub_command_class.sub_command_class.group.value = \
            getgrgid(gid).gr_name if isinstance(gid, int) else gid
        self.sub_command_class.sub_command_class.scm_size.value = scm_size
        self.sub_command_class.sub_command_class.nvme_size.value = nvme_size
        if target_list is not None:
            self.sub_command_class.sub_command_class.ranks.value = ",".join(
                [str(target) for target in target_list])
        self.sub_command_class.sub_command_class.nsvc.value = svcn
        self.sub_command_class.sub_command_class.sys.value = group
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        return self._get_result()

    def pool_query(self, pool):
        """Query a pool with the dmg command.

        Args:
            uuid (str): Pool UUID to query.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool query command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.pool.value = pool
        return self._get_result()

    def pool_destroy(self, pool, force=True):
        """Destroy a pool with the dmg command.

        Args:
            pool (str): Pool UUID to destroy.
            force (bool, optional): Force removal of pool. Defaults to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool destroy command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("destroy")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.force.value = force
        return self._get_result()

    def pool_get_acl(self, pool):
        """Get the ACL for a given pool.

        Args:
            pool (str): Pool for which to get the ACL.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool get-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("get-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        return self._get_result()

    def pool_update_acl(self, pool, acl_file, entry):
        """Update the acl for a given pool.

        Args:
            pool (str): Pool for which to update the ACL.
            acl_file (str): ACL file to update
            entry (str): entry to be updated

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool update-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("update-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        self.sub_command_class.sub_command_class.entry.value = entry
        return self._get_result()

    def pool_overwrite_acl(self, pool, acl_file):
        """Overwrite the acl for a given pool.

        Args:
            pool (str): Pool for which to overwrite the ACL.
            acl_file (str): ACL file to update

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool overwrite-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("overwrite-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.acl_file.value = acl_file
        return self._get_result()

    def pool_delete_acl(self, pool, principal):
        """Delete the acl for a given pool.

        Args:
            pool (str): Pool for which to delete the ACL.
            principal (str): principal to be deleted

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("delete-acl")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.principal.value = principal
        return self._get_result()

    def pool_list(self):
        """List pools.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("list")
        return self._get_result()

    def pool_set_prop(self, pool, name, value):
        """Set property for a given Pool.

        Args:
            pool (str): Pool uuid for which property is supposed
                        to be set.
            name (str): Property name to be set
            value (str): Property value to be set

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool set-prop command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("set-prop")
        self.sub_command_class.sub_command_class.pool.value = pool
        self.sub_command_class.sub_command_class.name.value = name
        self.sub_command_class.sub_command_class.value.value = value
        return self._get_result()

    def pool_exclude(self, pool_uuid, rank, tgt_idx=None):
        """Exclude a daos_server from the pool.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to exclude
            tgt_idx (int): target to be excluded from the pool

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool exclude command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("exclude")
        self.sub_command_class.sub_command_class.pool.value = pool_uuid
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.tgt_idx.value = tgt_idx
        return self._get_result()

    def pool_reintegrate(self, pool_uuid, rank, tgt_idx=None):
        """Reintegrate a daos_server to the pool.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to reintegrate
            tgt_idx (int): target to be reintegrated to the pool

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool reintegrate command fails.

        """
        self.set_sub_command("pool")
        self.sub_command_class.set_sub_command("reintegrate")
        self.sub_command_class.sub_command_class.pool.value = pool_uuid
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.tgt_idx.value = tgt_idx
        return self._get_result()

    def system_query(self, rank=None, verbose=False):
        """Query the state of the system.

        Args:
            rank (str, optional): rank to query. Defaults to None (query all).
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system query command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("query")
        self.sub_command_class.sub_command_class.rank.value = rank
        self.sub_command_class.sub_command_class.verbose.value = verbose
        return self._get_result()

    def system_start(self):
        """Start the system.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system start command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("start")
        return self._get_result()

    def system_stop(self, force=False):
        """Stop the system.

        Args:
            force (bool, optional): whether to force the stop. Defaults to
                False.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system stop command fails.

        """
        self.set_sub_command("system")
        self.sub_command_class.set_sub_command("stop")
        self.sub_command_class.sub_command_class.force.value = force
        return self._get_result()


def check_system_query_status(stdout_str):
    """Check if any server crashed.

    Args:
        stdout_str (list): list obtained from 'dmg system query -v'

    Returns:
        bool: True if no server crashed, False otherwise.

    """
    check = True
    rank_info = []
    failed_rank_list = []
    # iterate to obtain failed rank list
    for i, _ in enumerate(stdout_str):
        rank_info.append(stdout_str[i][0])
        print("rank_info: \n{}".format(rank_info))
        for items in rank_info:
            item = items.split()
            if item[3] in ["Unknown", "Evicted", "Errored", "Unresponsive"]:
                failed_rank_list.append(items)
    # if failed rank list is not empty display the failed ranks
    # and return False
    if failed_rank_list:
        for failed_list in failed_rank_list:
            print("failed_list: {}\n".format(failed_list))
            out = failed_list.split()
            print("Rank {} failed with state '{}'".format(out[0], out[3]))
        check = False
    return check


def get_pool_uuid_service_replicas_from_stdout(stdout_str):
    """Get Pool UUID and Service replicas from stdout.

    stdout_str is something like:
    Active connections: [wolf-3:10001]
    Creating DAOS pool with 100MB SCM and 0B NvMe storage (1.000 ratio)
    Pool-create command SUCCEEDED: UUID: 9cf5be2d-083d-4f6b-9f3e-38d771ee313f,
    Service replicas: 0
    This method makes it easy to create a test.

    Args:
        stdout_str (str): Output of pool create command.

    Returns:
        Tuple (str, str): Tuple that contains two items; Pool UUID and Service
            replicas if found. If not found, the tuple contains None.

    """
    # Find the following with regex. One or more of whitespace after "UUID:"
    # followed by one of more of number, alphabets, or -. Use parenthesis to
    # get the returned value.
    uuid = None
    svc = None
    match = re.search(r" UUID: (.+), Service replicas: (.+)", stdout_str)
    if match:
        uuid = match.group(1)
        svc = match.group(2)
    return uuid, svc


# ************************************************************************
# *** External usage should be replaced by DmgCommand.storage_format() ***
# ************************************************************************
def storage_format(path, hosts, insecure=True):
    """Execute format command through dmg tool to servers provided.

    Args:
        path (str): path to tool's binary
        hosts (list): list of servers to run format on.
        insecure (bool): toggle insecure mode

    Returns:
        Avocado CmdResult object that contains exit status, stdout information.

    """
    # Create and setup the command
    dmg = DmgCommand(path)
    dmg.insecure.value = insecure
    dmg.hostlist.value = hosts

    try:
        result = dmg.storage_format()
    except CommandFailure as details:
        print("<dmg> command failed: {}".format(details))
        return None

    return result
