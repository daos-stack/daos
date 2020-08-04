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

    # As the handling of these regular expressions are moved inside their
    # respective methods, they should be removed from this definition.
    METHOD_REGEX = {
        "run":
            r"(.*)",
        "network_scan":
            r"[-]+(?:\n|\n\r)([a-z0-9-]+)(?:\n|\n\r)[-]+|NUMA\s+"
            r"Socket\s+(\d+)|(ofi\+[a-z0-9;_]+)\s+([a-z0-9, ]+)",
        "pool_list":
            r"(?:([0-9a-fA-F-]+) +([0-9,]+))",
        "storage_query_list_pools":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+|(?:UUID:([a-z0-9-]+)\s+Rank:([0-9]+)"
            r"\s+Targets:\[([0-9 ]+)\])(?:\s+Blobs:\[([0-9 ]+)\]\s+?$)",
        "storage_query_list_devices":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+.*\s+|(?:UUID:([a-z0-9-]+)\s+"
            r"Targets:\[([0-9 ]+)\]\s+Rank:([0-9]+)\s+State:([A-Z]+))",
        "storage_query_device_health":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+.*\s+UUID:([a-z0-9-]+)\s+Targets:"
            r"\[([0-9 ]+)\]\s+Rank:([0-9]+)\s+State:(\w+)\s+.*\s+|(?:Temp.*|"
            r"Cont.*Busy Time|Pow.*Cycles|Pow.*Duration|Unsafe.*|Media.*|"
            r"Read.*|Write.*|Unmap.*|Checksum.*|Err.*Entries|Avail.*|"
            r"Dev.*Reli.*|Vola.*):\s*([A-Za-z0-9]+)",
        "storage_query_target_health":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+|Devices\s+|UUID:([a-z0-9-]+)\s+"
            r"Targets:\[([0-9 ]+)\]\s+Rank:(\d+)\s+State:(\w+)|"
            r"(?:Read\s+Errors|Write\s+Errors|Unmap\s+Errors|Checksum\s+Errors|"
            r"Error\s+Log\s+Entries|Media\s+Errors|Temperature|"
            r"Available\s+Spare|Device\s+Reliability|Read\s+Only|"
            r"Volatile\s+Memory\s+Backup):\s?([A-Za-z0-9- ]+)",
        "storage_set_faulty":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+|Devices\s+|(?:UUID:[a-z0-9-]+\s+"
            r"Targets:\[[0-9 ]+\]\s+Rank:\d+\s+State:(\w+))",
        "system_query":
            r"(\d\s+([0-9a-fA-F-]+)\s+([0-9.]+)\s+[A-Za-z]+)",
        "system_start":
            r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
        "system_stop":
            r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
    }

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
        return self._get_result(
            ("network", "scan"), provider=provider, all=all_devs)

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
        return self._get_result(("storage", "scan"), verbose=verbose)

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
        return self._get_result(("storage", "format"), reformat=reformat)

    def storage_prepare(self, user=None, hugepages="4096", nvme=False,
                        scm=False, reset=False, force=True):
        """Get the result of the dmg storage format command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        kwargs = {
            "nvme_only": nvme,
            "scm_only": scm,
            "target_user": getuser() if user is None else user,
            "hugepages": hugepages,
            "reset": reset,
            "force": force
        }
        return self._get_result(("storage", "prepare"), **kwargs)

    def storage_set_faulty(self, uuid, force=True):
        """Get the result of the 'dmg storage set nvme-faulty' command.

        Args:
            uuid (str): Device UUID to query.
            force (bool, optional): Force setting device state to FAULTY.
                Defaults to True.
        """
        return self._get_result(
            ("storage", "set", "nvme-faulty"), uuid=uuid, force=force)

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
        return self._get_result(
            ("storage", "query", "list-devices"), rank=rank, health=health)

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
        return self._get_result(
            ("storage", "query", "list-pools"), uuid=uuid, rank=rank,
            verbose=verbose)

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
        return self._get_result(
            ("storage", "query", "device-health"), uuid=uuid)

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
        return self._get_result(
            ("storage", "query", "target-health"), rank=rank, tgtid=tgtid)

    def storage_query_nvme_health(self):
        """Get the result of the 'dmg storage query nvme-health' command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        return self._get_result(("storage", "query", "nvme-health"))

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

        Raises:
            CommandFailure: if the 'dmg pool create' command fails and
                self.exit_status_exception is set to True.

        Returns:
            dict: a dictionary containing the 'uuid' and 'svc' of the new pool
                successfully extracted form the dmg command result.

        """
        kwargs = {
            "user": getpwuid(uid).pw_name if isinstance(uid, int) else uid,
            "group": getgrgid(gid).gr_name if isinstance(gid, int) else gid,
            "scm_size": scm_size,
            "nvme_size": nvme_size,
            "nsvc": svcn,
            "sys": group,
            "acl_file": acl_file
        }
        if target_list is not None:
            kwargs["ranks"] = ",".join([str(target) for target in target_list])
        self._get_result(("pool", "create"), **kwargs)

        # Extract the new pool UUID and SVC list from the command output
        data = {}
        match = re.findall(
            r"UUID:\s+([A-Za-z0-9-]+),\s+Service replicas:\s+([A-Za-z0-9-]+)",
            self.result.stdout)
        if match:
            data["uuid"] = match[0][0]
            data["svc"] = match[0][1]
        return data

    def pool_query(self, pool):
        """Query a pool with the dmg command.

        Args:
            uuid (str): Pool UUID to query.

        Raises:
            CommandFailure: if the dmg pool query command fails.

        Returns:
            dict: a dictionary containing the pool information when successfully
                extracted form the dmg command result.

        """
        self._get_result(("pool", "query"), pool=pool)

        # Extract the new pool information from the command output.
        # Sample output:
        #   Pool <A>, ntarget=<B>, disabled=<C>, leader=<D>, version=<E>
        #   Pool space info:
        #   - Target(VOS) count:<F>
        #   - SCM:
        #     Total size: <G>
        #     Free: <H>, min:<I>, max:<J>, mean:<K>
        #   - NVMe:
        #     Total size: <L>
        #     Free: <M>, min:<N>, max:<O>, mean:<P>
        #   Rebuild <Q>, <R> objs, <S> recs
        #
        # This yields the following tuple of tuples when run through the regex:
        #   0: (<A>, <B>, <C>, <D>, <E>, '', '', '', '', '', '', '', '')
        #   1: ('', '', '', '', '', <F>, '', '', '', '', '', '', '')
        #   2: ('', '', '', '', '', '', <G>, <H>, <I>, <J>, <K>, '', '')
        #   3: ('', '', '', '', '', '', <L>, <M>, <N>, <O>, <P>, '', '')
        #   4: ('', '', '', '', '', '', '', '', '', '', <Q>, <R>, <S>)
        #
        # This method will convert the regex result into the following dict:
        #   data = {
        #       "uuid": <A>,
        #       "ntarget": <B>,
        #       "disabled": <C>,
        #       "leader": <D>,
        #       "version": <E>,
        #       "target_count": <F>,
        #       "scm": {
        #           "total": <G>,
        #           "free": <H>,
        #           "free_min": <I>,
        #           "free_max": <J>,
        #           "free_mean": <K>
        #       },
        #       "nvme": {
        #           "total": <L>,
        #           "free": <M>,
        #           "free_min": <N>,
        #           "free_max": <O>,
        #           "free_mean": <P>
        #       },
        #       "rebuild": {
        #           "state": <Q>,
        #           "objects": <R>,
        #           "records": <S>
        #       }
        #   }
        #
        data = {}
        match = re.findall(
            r"(?:Pool\s+([0-9a-fA-F-]+),\s+ntarget=(\d+),\s+disabled=(\d+),"
            r"\s+leader=(\d+),\s+version=(\d+)|Target\(VOS\)\s+count:"
            r"\s*(\d+)|(?:(?:SCM:|NVMe:)\s+Total\s+size:\s+([0-9.]+\s+[A-Z]+)"
            r"\s+Free:\s+([0-9.]+\s+[A-Z]+),\smin:([0-9.]+\s+[A-Z]+),"
            r"\s+max:([0-9.]+\s+[A-Z]+),\s+mean:([0-9.]+\s+[A-Z]+))"
            r"|Rebuild\s+(\w+),\s+([0-9]+)\s+objs,\s+([0-9]+)\s+recs)",
            self.result.stdout)
        if match:
            # Mapping of the pool data entries to the match[0] indices
            pool_map = {
                "uuid": 0,
                "ntarget": 1,
                "disabled": 2,
                "leader": 3,
                "version": 4
            }
            # Mapping of the pool space entries to the match[2|3] indices
            space_map = {
                "total": 6,
                "free": 7,
                "free_min": 8,
                "free_max": 9,
                "free_mean": 10
            }
            # Mapping of the second indices mappings to the first match indices
            map_values = {
                0: pool_map,
                1: {"target_count": 5},
                2: space_map,
                3: space_map,
                4: {"status": 10, "objects": 11, "records": 12}
            }
            for index_1, match_list in enumerate(match):
                if index_1 not in map_values:
                    continue
                for key, index_2 in map_values[index_1].items():
                    if index_1 == 2:
                        if "scm" not in data:
                            data["scm"] = {}
                        data["scm"][key] = match_list[index_2]
                    elif index_1 == 3:
                        if "nvme" not in data:
                            data["nvme"] = {}
                        data["nvme"][key] = match_list[index_2]
                    elif index_1 == 4:
                        if "rebuild" not in data:
                            data["rebuild"] = {}
                        data["rebuild"][key] = match_list[index_2]
                    else:
                        data[key] = match_list[index_2]
        return data

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
        return self._get_result(("pool", "destroy"), pool=pool, force=force)

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
        return self._get_result(("pool", "get-acl"), pool=pool)

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
        return self._get_result(
            ("pool", "update-acl"), pool=pool, acl_file=acl_file, entry=entry)

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
        return self._get_result(
            ("pool", "overwrite-acl"), pool=pool, acl_file=acl_file)

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
        return self._get_result(
            ("pool", "delete-acl"), pool=pool, principal=principal)

    def pool_list(self):
        """List pools.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        """
        return self._get_result(("pool", "list"))

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
        return self._get_result(
            ("pool", "set-prop"), pool=pool, name=name, value=value)

    def pool_exclude(self, pool, rank, tgt_idx=None):
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
        return self._get_result(
            ("pool", "exclude"), pool=pool, rank=rank, tgt_idx=tgt_idx)

    def pool_drain(self, pool, rank, tgt_idx=None):
        """Drain a daos_server from the pool.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to drain
            tgt_idx (int): target to be excluded from the pool

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool drain command fails.

        """
        return self._get_result(
            ("pool", "drain"), pool=pool, rank=rank, tgt_idx=tgt_idx)

    def pool_reintegrate(self, pool, rank, tgt_idx=None):
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
        return self._get_result(
            ("pool", "reintegrate"), pool=pool, rank=rank, tgt_idx=tgt_idx)

    def system_query(self, rank=None, verbose=True):
        """Query system to obtain the status of the servers.

        Args:
            rank: Specify specific rank to obtain it's status
                  Defaults to None, which means report all available
                  ranks.
            verbose (bool): To obtain detailed query report

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage prepare command fails.

        """
        return self._get_result(("system", "query"), rank=rank, verbose=verbose)

    def system_start(self):
        """Start the system.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system start command fails.

        """
        return self._get_result(("system", "start"))

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
        return self._get_result(("system", "stop"), force=force)


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
