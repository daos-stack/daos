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

from ClusterShell.NodeSet import NodeSet

from command_utils_base import CommandFailure
from dmg_utils_base import DmgCommandBase
from general_utils import get_numeric_list


class DmgCommand(DmgCommandBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Defines a unit test version of the DmgCommand class.

    The unit test version of the DmgCommandBase class provides additional
    methods to issue dmg commands in unit test cases with the dmg command output
    returned as a dictionary of keyed values.

    Each time a command is issued the resulting CmdResult object, containing
    information such as the exit code, stdout, stderr, etc., will be stored to
    the DmgCommand.result attribute.  This can be useful for detecting failed
    commands as part of negative testing.
    """

    # Eventually move each regex into the appropriate class method to return the
    # result of the regex in a more usable dictionary of values.  As each class
    # method is updated the associated regex should be removed from this list.
    METHOD_REGEX = {
        "run":
            r"(.*)",
        "network_scan":
            r"[-]+(?:\n|\n\r)([a-z0-9-]+)(?:\n|\n\r)[-]+|NUMA\s+"
            r"Socket\s+(\d+)|(ofi\+[a-z0-9;_]+)\s+([a-z0-9, ]+)",
        "pool_list":
            r"(?:([0-9a-fA-F-]+) +([0-9,]+))",
        "pool_query":
            r"(?:Pool\s+([0-9a-fA-F-]+),\s+ntarget=(\d+),\s+disabled=(\d+),"
            r"\s+leader=(\d+),\s+version=(\d+)|Target\(VOS\)\s+count:"
            r"\s*(\d+)|(?:(?:SCM:|NVMe:)\s+Total\s+size:\s+([0-9.]+\s+[A-Z]+)"
            r"\s+Free:\s+([0-9.]+\s+[A-Z]+),\smin:([0-9.]+\s+[A-Z]+),"
            r"\s+max:([0-9.]+\s+[A-Z]+),\s+mean:([0-9.]+\s+[A-Z]+))"
            r"|Rebuild\s+\w+,\s+([0-9]+)\s+objs,\s+([0-9]+)\s+recs)",
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
    }

    def __init__(self, path, yaml_cfg=None):
        """Create a dmg Command object.

        Args:
            path (str): path to the dmg command
            yaml_cfg (DmgYamlParameters, optional): dmg config file
                settings. Defaults to None, in which case settings
                must be supplied as command-line parameters.
        """
        super(DmgCommand, self).__init__(
            "/run/dmg/*", "dmg", path, yaml_cfg)

        # This attribute stores the CmdResult from the last run() call.
        self.result = None

    def _set_result(self, sub_command_list, **kwargs):
        """Set the result of the defined dmg command.

        Use the sub_command_list and kwargs to set up the dmg command.  Assign
        the self.result to the CmdResult yielded from the dmg command execution.

        Args:
            sub_command_list (list): a list of dmg sub commands that define the
                command to execute
        """
        # Set the subcommands
        this_sub_command = self
        for sub_command in sub_command_list:
            this_sub_command.set_sub_command(sub_command)
            this_sub_command = this_sub_command.sub_command_class

        # Set the sub-command arguments
        for name, value in kwargs.items():
            getattr(this_sub_command, name).value = value

        # Update the configuration file
        if self.yaml:
            self.create_yaml_file()

        # Issue the command and store the command result
        self.result = self.run()

    def network_scan(self, provider=None, show_all=False):
        """Get the result of the dmg network scan command.

        Updates DmgCommand.result with the 'dmg network scan' CmdResult.

        Args:
            provider (str): name of network provider tied to the device
            show_all (bool, optional): Show all device info. Defaults to False.

        Raises:
            CommandFailure: if the 'dmg network scan' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg network scan
        self._set_result(("network", "scan"), provider=provider, all=show_all)

        # TODO: Extract the data from the command output
        return self.result

    def pool_create(self, scm_size, uid=None, gid=None, nvme_size=None,
                    target_list=None, svcn=None, group=None, acl_file=None):
        """Create a pool with the dmg command.

        The uid and gid method arguments can be specified as either an integer
        or a string.  If an integer value is specified it will be converted into
        the corresponding user/group name string.

        Updates DmgCommand.result with the 'dmg pool create' CmdResult.

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
        # Convert inputs to dmg pool create arguments
        kwargs = {
            "scm_size": scm_size,
            "user": getpwuid(uid).pw_name if isinstance(uid, int) else uid,
            "group": getgrgid(gid).gr_name if isinstance(gid, int) else gid,
            "nvme_size": nvme_size,
            "nsvc": svcn,
            "sys": group,
            "acl_file": acl_file
        }
        if target_list is not None:
            kwargs["ranks"] = ",".join([str(target) for target in target_list])

        # Execute dmg pool create
        self._set_result(("pool", "create"), **kwargs)

        # Extract the new pool UUID and SVC list from the command output
        data = {}
        match = re.findall(
            r"UUID:\s+([A-Za-z0-9-]+),\s+Service replicas:\s+([A-Za-z0-9-]+)",
            self.result.stdout)
        if match:
            data["uuid"] = match[0]
            data["svc"] = match[1]
        return data

    def pool_query(self, pool):
        """Query a pool with the dmg command.

        Updates DmgCommand.result with the 'dmg pool query' CmdResult.

        Args:
            uuid (str): Pool UUID to query.

        Raises:
            CommandFailure: if the 'dmg pool query' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        self._set_result(("pool", "query"), pool=pool)

        # TODO: Extract the data from the command output
        return self.result

    def pool_destroy(self, pool, force=True):
        """Destroy a pool with the dmg command.

        Updates DmgCommand.result with the 'dmg pool destroy' CmdResult.

        Args:
            pool (str): Pool UUID to destroy.
            force (bool, optional): Force removal of pool. Defaults to True.

        Raises:
            CommandFailure: if the 'dmg pool destroy' command fails and
                self.exit_status_exception is set to True.

        Returns:
            dict: a dictionary containing nothing.

        """
        # Execute dmg pool destroy
        self._set_result(("pool", "destroy"), pool=pool, force=force)
        data = {}
        return data

    def pool_get_acl(self, pool):
        """Get the ACL for a given pool.

        Updates DmgCommand.result with the 'dmg pool get-acl' CmdResult.

        Args:
            pool (str): Pool for which to get the ACL.

        Raises:
            CommandFailure: if the 'dmg pool get-acl' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        self._set_result(("pool", "get-acl"), pool=pool)

        # TODO: Extract the data from the command output
        return self.result

    def pool_update_acl(self, pool, acl_file, entry):
        """Update the acl for a given pool.

        Updates DmgCommand.result with the 'dmg pool update-acl' CmdResult.

        Args:
            pool (str): Pool for which to update the ACL.
            acl_file (str): ACL file to update
            entry (str): entry to be updated

        Raises:
            CommandFailure: if the 'dmg pool update-acl' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        kwargs = {"pool": pool, "acl_file": acl_file, "entry": entry}
        self._set_result(("pool", "update-acl"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def pool_overwrite_acl(self, pool, acl_file):
        """Overwrite the acl for a given pool.

        Updates DmgCommand.result with the 'dmg pool overwrite-acl' CmdResult.

        Args:
            pool (str): Pool for which to overwrite the ACL.
            acl_file (str): ACL file to update

        Raises:
            CommandFailure: if the 'dmg pool overwrite-acl' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        kwargs = {"pool": pool, "acl_file": acl_file}
        self._set_result(("pool", "overwrite-acl"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def pool_delete_acl(self, pool, principal):
        """Delete the acl for a given pool.

        Updates DmgCommand.result with the 'dmg pool delete-acl' CmdResult.

        Args:
            pool (str): Pool for which to delete the ACL.
            principal (str): principal to be deleted

        Raises:
            CommandFailure: if the 'dmg pool delete-acl' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        kwargs = {"pool": pool, "principal": principal}
        self._set_result(("pool", "delete-acl"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def pool_list(self):
        """List pools.

        Updates DmgCommand.result with the 'dmg pool list' CmdResult.

        Raises:
            CommandFailure: if the 'dmg pool list' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        self._set_result(("pool", "list"))

        # TODO: Extract the data from the command output
        return self.result

    def pool_set_prop(self, pool, name, value):
        """Set property for a given Pool.

        Updates DmgCommand.result with the 'dmg pool set-prop' CmdResult.

        Args:
            pool (str): Pool uuid for which property is supposed
                        to be set.
            name (str): Property name to be set
            value (str): Property value to be set

        Raises:
            CommandFailure: if the 'dmg pool set-prop' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"pool": pool, "name": name, "value": value}
        self._set_result(("pool", "set-prop"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def pool_exclude(self, pool, rank, tgt_idx=None):
        """Exclude a daos_server from the pool.

        Updates DmgCommand.result with the 'dmg pool exclude' CmdResult.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to exclude
            tgt_idx (int): target to be excluded from the pool

        Raises:
            CommandFailure: if the 'dmg pool exclude' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"pool": pool, "rank": rank, "tgt_idx": tgt_idx}
        self._set_result(("pool", "exclude"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def pool_reintegrate(self, pool, rank, tgt_idx=None):
        """Reintegrate a daos_server to the pool.

        Updates DmgCommand.result with the 'dmg pool reintegrate' CmdResult.

        Args:
            pool (str): Pool uuid.
            rank (int): Rank of the daos_server to reintegrate
            tgt_idx (int): target to be reintegrated to the pool

        Raises:
            CommandFailure: if the 'dmg pool reintegrate' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"pool": pool, "rank": rank, "tgt_idx": tgt_idx}
        self._set_result(("pool", "reintegrate"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_scan(self, verbose=False):
        """Get the result of the dmg storage scan command.

        Updates DmgCommand.result with the 'dmg storage scan' CmdResult.

        Args:
            verbose (bool, optional): create verbose output. Defaults to False.

        Raises:
            CommandFailure: if the 'dmg storage scan' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        self._set_result(("storage", "scan"), verbose=verbose)

        # TODO: Extract the data from the command output
        return self.result

    def storage_format(self, reformat=False):
        """Get the result of the dmg storage format command.

        Updates DmgCommand.result with the 'dmg storage format' CmdResult.

        Args:
            reformat (bool): always reformat storage, could be destructive.
                This will create control-plane related metadata i.e. superblock
                file and reformat if the storage media is available and
                formattable.

        Raises:
            CommandFailure: if the 'dmg storage format' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        self._set_result(("storage", "format"), reformat=reformat)

        # TODO: Extract the data from the command output
        return self.result

    def storage_prepare(self, target_user=None, hugepages="4096",
                        nvme_only=False, scm_only=False, reset=False,
                        force=True):
        """Get the result of the dmg storage format command.

        Updates DmgCommand.result with the 'dmg storage prepare' CmdResult.

        Args:
            target_user (str, optional): target user. Defaults to None.
            hugepages (str, optional): number of hugepages. Defaults to "4096".
            nvme_only (bool, optional): prepare NVMe only. Defaults to False.
            scm_only (bool, optional): prepare SCM only. Defaults to False.
            reset (bool, optional): reset the devices. Defaults to False.
            force (bool, optional): force the prepare. Defaults to True.

        Raises:
            CommandFailure: if the 'dmg storage prepare' command fails and
                self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {
            "target_user": getuser() if target_user is None else target_user,
            "hugepages": hugepages,
            "nvme_only": nvme_only,
            "scm_only": scm_only,
            "reset": reset,
            "force": force
        }
        self._set_result(("storage", "prepare"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_set_faulty(self, uuid, force=True):
        """Get the result of the 'dmg storage set nvme-faulty' command.

        Updates DmgCommand.result with the 'dmg storage set nvme-faulty'
        CmdResult.

        Args:
            uuid (str): Device UUID to query.
            force (bool, optional): Force setting device state to FAULTY.
                Defaults to True.

        Raises:
            CommandFailure: if the 'dmg storage set nvme-faulty' command fails
                and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"uuid": uuid, "force": force}
        self._set_result(("storage", "set", "nvme-faulty"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_query_list_devices(self, rank=None, health=False):
        """Get the result of the 'dmg storage query list-devices' command.

        Updates DmgCommand.result with the 'dmg storage query list-devices'
        CmdResult.

        Args:
            rank (int, optional): Limit response to devices on this rank.
                Defaults to None.
            health (bool, optional): Include device health in response.
                Defaults to false.

        Raises:
            CommandFailure: if the 'dmg storage query list-devices' command
                fails and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"rank": rank, "health": health}
        self._set_result(("storage", "query", "list-devices"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_query_list_pools(self, uuid=None, rank=None, verbose=False):
        """Get the result of the 'dmg storage query list-pools' command.

        Updates DmgCommand.result with the 'dmg storage query list-pools'
        CmdResult.

        Args:
            uuid (str): Device UUID to query. Defaults to None.
            rank (int, optional): Limit response to pools on this rank.
                Defaults to None.
            verbose (bool, optional): create verbose output. Defaults to False.

        Raises:
            CommandFailure: if the 'dmg storage query list-pools' command fails
                and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"uuid": uuid, "rank": rank, "verbose": verbose}
        self._set_result(("storage", "query", "list-pools"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_query_device_health(self, uuid):
        """Get the result of the 'dmg storage query device-health' command.

        Updates DmgCommand.result with the 'dmg storage query device-health'
        CmdResult.

        Args:
            uuid (str): Device UUID to query.

        Raises:
            CommandFailure: if the 'dmg storage query device-health' command
                fails and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        self._set_result(("storage", "query", "device-health"), uuid=uuid)

        # TODO: Extract the data from the command output
        return self.result

    def storage_query_target_health(self, rank, tgtid):
        """Get the result of the 'dmg storage query target-health' command.

        Updates DmgCommand.result with the 'dmg storage query target-health'
        CmdResult.

        Args:
            rank (int): Rank hosting target.
            tgtid (int): Target index to query.

        Raises:
            CommandFailure: if the 'dmg storage query target-health' command
                fails and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        kwargs = {"rank": rank, "tgtid": tgtid}
        self._set_result(("storage", "query", "target-health"), **kwargs)

        # TODO: Extract the data from the command output
        return self.result

    def storage_query_nvme_health(self):
        """Get the result of the 'dmg storage query nvme-health' command.

        Updates DmgCommand.result with the 'dmg storage query nvme-health'
        CmdResult.

        Raises:
            CommandFailure: if the 'dmg storage query nvme-health' command fails
                and self.exit_status_exception is set to True.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        # Execute dmg pool query
        self._set_result(("storage", "query", "nvme-health"))

        # TODO: Extract the data from the command output
        return self.result

    def system_query(self, rank=None, verbose=False):
        """Query the state of the system with verbose disabled.

        Updates DmgCommand.result with the 'dmg system query' CmdResult.

        Args:
            rank (str, optional): rank to query. Defaults to None (query all).
            verbose (bool, optional): whether to display rank details. Defaults
                to False.

        Raises:
            CommandFailure: if the 'dmg system query' command fails and
                self.exit_status_exception is set to True.

        Returns:
            dict: a dictionary of host NodeSets and their unique states.

        """
        self._set_result(("system", "query"), rank=rank, verbose=verbose)

        data = {}
        if rank is not None and "," not in rank and "-" not in rank:
            # Process the unique single rank system query output, e.g.
            #   Rank 1
            #   ------
            #   address : 10.8.1.11:10001
            #   uuid    : d7a69a41-59a2-4dec-a620-a52217851285
            #   status  : Joined
            #   reason  :
            match = re.findall(
                r"(?:Rank|address\s+:|uuid\s+:|status\s+:|reason\s+:)\s+(.*)",
                self.result.stdout)
            if match:
                rank = int(match[0])
                data[rank]["address"] = match[1]
                data[rank]["uuid"] = match[2]
                data[rank]["state"] = match[3]      # Status
                data[rank]["reason"] = match[4]
        elif verbose:
            # Process the verbose multiple rank system query output, e.g.
            #   Rank UUID                                 Control Address State
            #   ---- ----                                 --------------- -----
            #   0    385af2f9-1863-406c-ae94-bffdcd02f379 10.8.1.10:10001 Joined
            #   1    d7a69a41-59a2-4dec-a620-a52217851285 10.8.1.11:10001 Joined
            print("stdout:\n{}".format(self.result.stdout))
            match = re.findall(
                r"(\d+)\s+([0-9a-f-]+)\s+(.*)\s+([A-Za-z]+)(?:|\s+([A-Za-z]+))",
                self.result.stdout)
            for info in match:
                rank = int(info[0])
                data[rank]["address"] = info[2]
                data[rank]["uuid"] = info[1]
                data[rank]["state"] = info[3]
                data[rank]["reason"] = match[4]
        else:
            # Process the non-verbose multiple rank system query output, e.g.
            #   Rank  State
            #   ----  -----
            #   [0-1] Joined
            match = re.findall(
                r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)", self.result.stdout)
            for info in match:
                for rank in get_numeric_list(info[0]):
                    data[rank]["state"] = info[1]
        return data

    def system_start(self):
        """Start the system.

        Updates DmgCommand.result with the 'dmg system start' CmdResult.

        Raises:
            CommandFailure: if the 'dmg system start' command fails and
                self.exit_status_exception is set to True.

        Returns:
            dict: a dictionary of host NodeSets and their unique states.

        """
        self._set_result(("system", "start"))

        # Populate a dictionary with host set keys for each unique state
        data = {}
        match = re.findall(
            r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
            self.result.stdout)
        for hosts, state in match:
            data[NodeSet(hosts)] = state
        return data

    def system_stop(self, force=False):
        """Stop the system.

        Updates DmgCommand.result with the 'dmg system stop' CmdResult.

        Args:
            force (bool, optional): whether to force the stop. Defaults to
                False.

        Raises:
            CommandFailure: if the 'dmg system stop' command fails and
                self.exit_status_exception is set to True.

        Returns:
            dict: a dictionary of host NodeSets and their unique states.

        """
        self._set_result(("system", "stop"), force=force)

        # Populate a dictionary with host set keys for each unique state
        data = {}
        match = re.findall(
            r"(\d+|\[[0-9-,]+\])\s+([A-Za-z]+)\s+([A-Za-z]+)",
            self.result.stdout)
        for hosts, state in match:
            data[NodeSet(hosts)] = state
        return data


def check_system_query_status(data):
    """Check if any server crashed.

    Args:
        data (dict): dictionary of system query data obtained from
            DmgCommand.system_query()

    Returns:
        bool: True if no server crashed, False otherwise.

    """
    failed_states = ("Unknown", "Evicted", "Errored", "Unresponsive")
    failed_rank_list = []

    # Check the state of each rank.
    for rank in data:
        rank_info = [
            "{}: {}".format(key, data[rank][key])
            for key in sorted(data[rank].keys())
        ]
        print("Rank {} info:\n  {}".format(rank, "\n  ".join(rank_info)))
        if "state" in data[rank] and data[rank]["state"] in failed_states:
            failed_rank_list.append(rank)

    # Display the details of any failed ranks
    if failed_rank_list:
        for rank in failed_rank_list:
            print(
                "Rank {} failed with state '{}'".format(
                    rank, data[rank]["state"]))

    # Return True if no ranks failed
    return not bool(failed_rank_list)


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
