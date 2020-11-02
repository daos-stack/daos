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
import json

from command_utils_base import CommandFailure
from dmg_utils_base import DmgCommandBase


class DmgCommand(DmgCommandBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Defines a object representing a dmg command with helper methods."""

    # Member state defined in control/system/member.go. Used for dmg system
    # query.
    SYSTEM_QUERY_STATES = {
        "UNKNOWN": 0,
        "AWAIT_FORMAT": 1,
        "STARTING": 2,
        "READY": 3,
        "JOINED": 4,
        "STOPPING": 5,
        "STOPPED": 6,
        "EVICTED": 7,
        "ERRORED": 8,
        "UNRESPONSIVE": 9
    }

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
        # pylint: disable=pointless-string-statement
        """Get the result of the dmg storage scan command.

        Args:
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            dict: Values obtained from stdout in dictionary. Most of the values
                are in list.

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        self.result = self._get_result(("storage", "scan"), verbose=verbose)

        # Sample dmg storage scan verbose output. Don't delete this sample
        # because it helps to develop and debug the regex.
        """
        --------
        wolf-130
        --------
        SCM Namespace Socket ID Capacity
        ------------- --------- --------
        pmem0         0         3.2 TB
        pmem1         0         3.2 TB

        NVMe PCI     Model                FW Revision Socket ID Capacity
        --------     -----                ----------- --------- --------
        0000:5e:00.0 INTEL SSDPE2KE016T8  VDV10170    0         1.6 TB
        0000:5f:00.0 INTEL SSDPE2KE016T8  VDV10170    0         1.6 TB
        0000:81:00.0 INTEL SSDPED1K750GA  E2010475    1         750 GB
        0000:da:00.0 INTEL SSDPED1K750GA  E2010475    1         750 GB
        """

        # Sample dmg storage scan output. Don't delete this sample because it
        # helps to develop and debug the regex.
        """
         Hosts    SCM Total             NVMe Total
        -----    ---------             ----------
        wolf-130 6.4 TB (2 namespaces) 4.7 TB (4 controllers)
        """

        data = {}

        if verbose:
            vals = re.findall(
                r"--------\n([a-z0-9-]+)\n--------|"
                r"\n([a-z0-9_]+)[ ]+([\d]+)[ ]+([\d.]+) ([A-Z]+)|"
                r"([a-f0-9]+:[a-f0-9]+:[a-f0-9]+.[a-f0-9]+)[ ]+"
                r"(\S+)[ ]+(\S+)[ ]+(\S+)[ ]+(\d+)[ ]+([\d.]+)"
                r"[ ]+([A-Z]+)[ ]*\n", self.result.stdout)

            data = {}
            host = vals[0][0]
            data[host] = {}
            data[host]["scm"] = {}
            i = 1
            while i < len(vals):
                if vals[i][1] == "":
                    break
                pmem_name = vals[i][1]
                socket_id = vals[i][2]
                capacity = "{} {}".format(vals[i][3], vals[i][4])
                data[host]["scm"][pmem_name] = {}
                data[host]["scm"][pmem_name]["socket"] = socket_id
                data[host]["scm"][pmem_name]["capacity"] = capacity
                i += 1

            data[host]["nvme"] = {}
            while i < len(vals):
                pci_addr = vals[i][5]
                model = "{} {}".format(vals[i][6], vals[i][7])
                fw_revision = vals[i][8]
                socket_id = vals[i][9]
                capacity = "{} {}".format(vals[i][10], vals[i][11])
                data[host]["nvme"][pci_addr] = {}
                data[host]["nvme"][pci_addr]["model"] = model
                data[host]["nvme"][pci_addr]["fw_revision"] = fw_revision
                data[host]["nvme"][pci_addr]["socket"] = socket_id
                data[host]["nvme"][pci_addr]["capacity"] = capacity
                i += 1

        else:
            vals = re.findall(
                r"([a-z0-9-\[\]]+)\s+([\d.]+)\s+([A-Z]+)\s+"
                r"\(([\w\s]+)\)\s+([\d.]+)\s+([A-Z]+)\s+\(([\w\s]+)",
                self.result.stdout)
            self.log.info("--- Non-verbose output parse result ---")
            self.log.info(vals)

            data = {}
            for row in vals:
                host = row[0]
                data[host] = {
                    "scm": {"capacity": None, "details": None},
                    "nvme": {"capacity": None, "details": None}}
                data[host]["scm"]["capacity"] = " ".join(row[1:3])
                data[host]["scm"]["details"] = row[3]
                data[host]["nvme"]["capacity"] = " ".join(row[4:6])
                data[host]["nvme"]["details"] = row[6]

        return data

    def storage_format(self, reformat=False, timeout=30):
        """Get the result of the dmg storage format command.

        Args:
            reformat (bool): always reformat storage, could be destructive.
                This will create control-plane related metadata i.e. superblock
                file and reformat if the storage media is available and
                formattable.
            timeout: seconds after which the format is considered a failure and
                times out.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        saved_timeout = self.timeout
        self.timeout = timeout
        result = self._get_result(("storage", "format"), reformat=reformat)
        self.timeout = saved_timeout
        return result

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

    def storage_scan_nvme_health(self):
        """Get the result of the 'dmg storage scan --nvme-health' command.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: If dmg storage scan --nvme-health command fails.

        """
        return self._get_result(("storage", "scan"), nvme_health=True)

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
        if self.json.value:
            # Sample json output.
            # "response": {
            #     "UUID": "ebac9285-61ec-4d2e-aa2d-4d0f7dd6b7d6",
            #     "Svcreps": [
            #     0
            #     ]
            # },
            # "error": null,
            # "status": 0
            output = json.loads(self.result.stdout)
            data["uuid"] = output["response"]["UUID"]
            data["svc"] = ",".join(
                [str(svc) for svc in output["response"]["Svcreps"]])

        else:
            match = re.findall(
                r"UUID:\s+([A-Za-z0-9-]+),\s+"
                r"Service replicas:\s+([0-9]+(,[0-9]+)*)",
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
        #   0: (<A>, <B>, <C>, <D>, <E>, '', '', '', '', '', '', '', '', '')
        #   1: ('', '', '', '', '', <F>, '', '', '', '', '', '', '', '')
        #   2: ('', '', '', '', '', '', <G>, <H>, <I>, <J>, <K>, '', '', '')
        #   3: ('', '', '', '', '', '', <L>, <M>, <N>, <O>, <P>, '', '', '')
        #   4: ('', '', '', '', '', '', '', '', '', '', '', <Q>, <R>, <S>)
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
        #           "status": <Q>,
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
                4: {"status": 11, "objects": 12, "records": 13}
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

    def pool_update_acl(self, pool, acl_file=None, entry=None):
        """Update the acl for a given pool.

        Args:
            pool (str): Pool for which to update the ACL.
            acl_file (str, optional): ACL file to update
            entry (str, optional): entry to be updated

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

    def pool_extend(self, pool, ranks, scm_size, nvme_size):
        """Extend the daos_server pool

        Args:
            pool (str): Pool uuid.
            ranks (int): Ranks of the daos_server to extend
            scm_size (int): SCM pool size to extend
            nvme_size (int): NVME pool size to extend

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool extend command fails.

        """
        return self._get_result(
            ("pool", "extend"), pool=pool, ranks=ranks,
            scm_size=scm_size, nvme_size=nvme_size)

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

    def cont_set_owner(self, pool, cont, user, group):
        """Dmg container set-owner to the specified new user/group.

        Args:
            pool (str): Pool uuid.
            cont (str): Container uuid.
            user (str): new user for the container.
            group (str): new group for the container.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool reintegrate command fails.

        """
        return self._get_result(
            ("cont", "set-owner"), pool=pool, cont=cont, user=user, group=group)

    def system_query(self, ranks=None, verbose=True):
        """Query system to obtain the status of the servers.

        Args:
            ranks (str): Specify specific ranks to obtain it's status. Use
                comma separated list for multiple ranks. e.g., 0,1.
                Defaults to None, which means report all available ranks.
            verbose (bool): To obtain detailed query report

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system query command fails.

        """
        return self._get_result(
            ("system", "query"), ranks=ranks, verbose=verbose)

    def system_start(self):
        """Start the system.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system start command fails.

        """
        return self._get_result(("system", "start"))

    def system_stop(self, force=False, ranks=None):
        """Stop the system.

        Args:
            force (bool, optional): whether to force the stop. Defaults to
                False.
            ranks (str, optional): comma separated ranks to stop. Defaults to
                None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg system stop command fails.

        """
        return self._get_result(("system", "stop"), force=force, ranks=ranks)

    def pool_evict(self, pool, sys=None):
        """Evict a pool.

        Args:
            pool (str):  UUID of DAOS pool to evict connection to
            sys (str, optional): DAOS system that the pools connections be
                evicted from. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool evict command fails.
        """
        return self._get_result(("pool", "evict"), pool=pool, sys=sys)


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
