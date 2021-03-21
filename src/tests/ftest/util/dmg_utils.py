#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from getpass import getuser
from grp import getgrgid
from pwd import getpwuid
import re
import json

from dmg_utils_base import DmgCommandBase
from general_utils import get_numeric_list
from dmg_utils_params import DmgYamlParameters, DmgTransportCredentials


def get_dmg_command(group, cert_dir, bin_dir, config_file, config_temp=None):
    """Get a dmg command object.

    Args:
        group (str): daos_server group name
        cert_dir (str): directory in which to copy certificates
        bin_dir (str): location of the dmg executable
        config_file (str): configuration file name and path
        config_temp (str, optional): file name and path to use to generate the
            configuration file locally and then copy it to all the hosts using
            the config_file specification. Defaults to None, which creates and
            utilizes the file specified by config_file.

    Returns:
        DmgCommand: the dmg command object

    """
    transport_config = DmgTransportCredentials(cert_dir)
    config = DmgYamlParameters(config_file, group, transport_config)
    command = DmgCommand(bin_dir, config)
    if config_temp:
        # Setup the DaosServerCommand to write the config file data to the
        # temporary file and then copy the file to all the hosts using the
        # assigned filename
        command.temporary_file = config_temp
    return command


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
        "storage_query_list_pools":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+|(?:UUID:([a-z0-9-]+)\s+Rank:([0-9]+)"
            r"\s+Targets:\[([0-9 ]+)\])(?:\s+Blobs:\[([0-9 ]+)\]\s+?$)",
        "storage_query_list_devices":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+.*\s+|UUID:([a-f0-90-]{36}).*"
            r"TrAddr:([a-z0-9:.]+)]\s+Targets:\[([0-9 ]+).*Rank:([0-9]+)"
            r"\s+State:([A-Z]+)",
        "storage_query_device_health":
            r"[-]+\s+([a-z0-9-]+)\s+[-]+\s+.*\s+|UUID:([a-f0-90-]{36}).*"
            r"TrAddr:([a-z0-9:.]+)]\s+Targets:\[([0-9 ]+).*Rank:([0-9]+)\s+"
            r"State:([A-Z]+)|(?:Timestamp|Temp.*|Cont.*Busy Time|Pow.*Cycles"
            r"|Pow.*Duration|Unsafe.*|Media.*|Read.*|Write.*|Unmap.*|Checksum"
            r".*|Err.*Entries|Avail.*|Dev.*Reli.*|Vola.*):\s*([A-Za-z0-9 :]+)",
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

    def _get_json_result(self, sub_command_list=None, **kwargs):
        """Wrap the base _get_result method to force JSON output."""
        prev_json_val = self.json.value
        self.json.update(True)
        prev_output_check = self.output_check
        self.output_check = "both"
        try:
            self._get_result(sub_command_list, **kwargs)
        finally:
            self.json.update(prev_json_val)
            self.output_check = prev_output_check
        return json.loads(self.result.stdout)

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
        self._get_result(("storage", "scan"), verbose=verbose)

        data = {}
        if verbose:
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
            match = re.findall(
                r"(?:([a-zA-Z0-9]+-[0-9]+)|"
                r"(?:([0-9a-fA-F:.]+)\s+([a-zA-Z0-9 ]+)\s+"
                r"([a-zA-Z0-9]+)\s+(\d+)\s+([0-9\.]+\s+[A-Z]+))|"
                r"(?:([a-zA-Z0-9]+)\s+(\d+)\s+([0-9\.]+\s+[A-Z]+)))",
                self.result.stdout_text)
            host = ""
            for item in match:
                if item[0]:
                    host = item[0]
                    data[host] = {"scm": {}, "nvme": {}}
                elif item[1]:
                    data[host]["nvme"][item[1]] = {
                        "model": item[2],
                        "fw": item[3],
                        "socket": item[4],
                        "capacity": item[5],
                    }
                elif item[6]:
                    data[host]["scm"][item[6]] = {
                        "socket": item[7],
                        "capacity": item[8],
                    }
        else:
            # Sample dmg storage scan non-verbose output. Don't delete this
            # sample because it helps to develop and debug the regex.
            """
            Hosts    SCM Total             NVMe Total
            -----    ---------             ----------
            wolf-130 6.4 TB (2 namespaces) 4.7 TB (4 controllers)
            """
            values = re.findall(
                r"([a-z0-9-\[\]]+)\s+([\d.]+)\s+([A-Z]+)\s+"
                r"\(([\w\s]+)\)\s+([\d.]+)\s+([A-Z]+)\s+\(([\w\s]+)",
                self.result.stdout_text)
            self.log.info("--- Non-verbose output parse result ---")
            self.log.info(values)

            data = {}
            for row in values:
                host = row[0]
                data[host] = {
                    "scm": {"capacity": None, "details": None},
                    "nvme": {"capacity": None, "details": None}}
                data[host]["scm"]["capacity"] = " ".join(row[1:3])
                data[host]["scm"]["details"] = row[3]
                data[host]["nvme"]["capacity"] = " ".join(row[4:6])
                data[host]["nvme"]["details"] = row[6]

        self.log.info("storage_scan data: %s", str(data))
        return data

    def storage_format(self, reformat=False, timeout=30, verbose=False):
        """Get the result of the dmg storage format command.

        Args:
            reformat (bool): always reformat storage, could be destructive.
                This will create control-plane related metadata i.e. superblock
                file and reformat if the storage media is available and
                formattable.
            timeout: seconds after which the format is considered a failure and
                times out.
            verbose (bool): show results of each SCM & NVMe device format
                operation.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        saved_timeout = self.timeout
        self.timeout = timeout
        self._get_result(
            ("storage", "format"), reformat=reformat, verbose=verbose)
        self.timeout = saved_timeout
        return self.result

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
                None, in which case the default value is set by the server.
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

        # Extract the new pool UUID and SVC list from the command output
        data = {}
        # Sample json output.
        # "response": {
        #   "uuid": "ebac9285-61ec-4d2e-aa2d-4d0f7dd6b7d6",
        #   "svc_reps": [
        #     0
        #   ],
        #   "tgt_ranks": [
        #     0,
        #     1
        #   ],
        #   "scm_bytes": 256000000,
        #   "nvme_bytes": 0
        # },
        # "error": null,
        # "status": 0
        output = self._get_json_result(("pool", "create"), **kwargs)
        if output["response"] is None:
            return data

        data["uuid"] = output["response"]["uuid"]
        data["svc"] = ",".join(
            [str(svc) for svc in output["response"]["svc_reps"]])
        data["ranks"] = ",".join(
            [str(r) for r in output["response"]["tgt_ranks"]])
        data["scm_per_rank"] = output["response"]["scm_bytes"]
        data["nvme_per_rank"] = output["response"]["nvme_bytes"]

        return data

    def pool_query(self, pool, use_json=True):
        """Query a pool with the dmg command.

        Args:
            uuid (str): Pool UUID to query.
            use_json (bool): Whether to use --json. Defaults to True.

        Raises:
            CommandFailure: if the dmg pool query command fails.

        Returns:
            dict: dictionary of output in JSON format if use_json is set to
                True. Otherwise, CmdResult that contains exit status, stdout,
                and other information.

        """
        # Sample JSON output
        # {
        #     "response": {
        #         "status": 0,
        #         "uuid": "EDAE0965-7A6E-48BD-A71C-A29F199C679F",
        #         "total_targets": 8,
        #         "active_targets": 8,
        #         "total_nodes": 1,
        #         "disabled_targets": 0,
        #         "version": 1,
        #         "leader": 0,
        #         "rebuild": {
        #             "status": 0,
        #             "state": "idle",
        #             "objects": 0,
        #             "records": 0
        #         },
        #         "scm": {
        #             "total": 16000000000,
        #             "free": 15999992320,
        #             "min": 1999999040,
        #             "max": 1999999040,
        #             "mean": 1999999040
        #         },
        #         "nvme": {
        #             "total": 32000000000,
        #             "free": 31999950848,
        #             "min": 3999993856,
        #             "max": 3999993856,
        #             "mean": 3999993856
        #         }
        #     },
        #     "error": null,
        #     "status": 0
        # }
        if use_json:
            return self._get_json_result(("pool", "query"), pool=pool)

        return self._get_result(("pool", "query"), pool=pool)

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

        Raises:
            CommandFailure: if the dmg pool pool list command fails.

        Returns:
            dict: a dictionary of pool UUID keys and svc replica values

        """
        self._get_result(("pool", "list"))

        # Populate a dictionary with svc replicas for each pool UUID key listed
        # Sample dmg pool list output:
        #    Pool UUID                            Svc Replicas
        #    ---------                            ------------
        #    43bf2fe8-cb92-46ec-b9e9-9b056725092a 0
        #    98736dfe-cb92-12cd-de45-9b09875092cd 1
        data = {}
        match = re.findall(
            r"(?:([0-9a-fA-F][0-9a-fA-F-]+)\W+([0-9][0-9,-]*))",
            self.result.stdout_text)
        for info in match:
            data[info[0]] = get_numeric_list(info[1])
        return data

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
        """Extend the daos_server pool.

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

        Raises:
            CommandFailure: if the dmg system query command fails.

        Returns:
            dict: a dictionary of host ranks and their unique states.

        """
        # Sample output:
        # {
        # "response": {
        #     "members": [
        #     {
        #         "addr": "10.8.1.11:10001",
        #         "state": "joined",
        #         "fault_domain": "/wolf-11.wolf.hpdd.intel.com",
        #         "rank": 0,
        #         "uuid": "e7f2cb06-a111-4d55-a6a5-b494b70d62ab",
        #         "fabric_uri": "ofi+sockets://192.168.100.11:31416",
        #         "fabric_contexts": 17,
        #         "info": ""
        #     },
        #     {
        #         "addr": "10.8.1.74:10001",
        #         "state": "evicted",
        #         "fault_domain": "/wolf-74.wolf.hpdd.intel.com",
        #         "rank": 1,
        #         "uuid": "db36ab28-fdb0-4822-97e6-89547393ed03",
        #         "fabric_uri": "ofi+sockets://192.168.100.74:31416",
        #         "fabric_contexts": 17,
        #         "info": ""
        #     }
        #     ]
        # },
        # "error": null,
        # "status": 0
        # }
        return self._get_json_result(
            ("system", "query"), ranks=ranks, verbose=verbose)

    def system_leader_query(self):
        """Query system to obtain the MS leader and replica information.

        Raises:
            CommandFailure: if the dmg system query command fails.

        Returns:
            dictionary of output in JSON format

        """
        # Example JSON output:
        # {
        #   "response": {
        #     "CurrentLeader": "127.0.0.1:10001",
        #     "Replicas": [
        #       "127.0.0.1:10001"
        #     ]
        #   },
        #   "error": null,
        #   "status": 0
        # }
        return self._get_json_result(("system", "leader-query"))

    def system_start(self, ranks=None):
        """Start the system.

        Args:
            ranks (str, optional): comma separated ranks to stop. Defaults to
                None.

        Raises:
            CommandFailure: if the dmg system start command fails.

        Returns:
            dict: a dictionary of host ranks and their unique states.

        """
        self._get_result(("system", "start"), ranks=ranks)

        # Populate a dictionary with host set keys for each unique state
        data = {}
        match = re.findall(
            r"(?:\[*([0-9-,]+)\]*)\s+([A-Za-z]+)\s+(.*)",
            self.result.stdout_text)
        for info in match:
            for rank in get_numeric_list(info[0]):
                data[rank] = info[1].strip()
        return data

    def system_stop(self, force=False, ranks=None):
        """Stop the system.

        Args:
            force (bool, optional): whether to force the stop. Defaults to
                False.
            ranks (str, optional): comma separated ranks to stop. Defaults to
                None.

        Raises:
            CommandFailure: if the dmg system stop command fails.

        Returns:
            dict: a dictionary of host ranks and their unique states.

        """
        self._get_result(("system", "stop"), force=force, ranks=ranks)

        # Populate a dictionary with host set keys for each unique state, ex:
        #   Rank Operation Result
        #   ---- --------- ------
        #   0    stop      want Stopped, got Ready
        data = {}
        match = re.findall(
            r"(?:\[*([0-9-,]+)\]*)\s+([A-Za-z]+)\s+(.*)",
            self.result.stdout_text)
        for info in match:
            for rank in get_numeric_list(info[0]):
                data[rank] = info[1].strip()
        return data

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


def check_system_query_status(data):
    """Check if any server crashed.

    Args:
        data (dict): dictionary of system query data obtained from
            DmgCommand.system_query()

    Returns:
        bool: True if no server crashed, False otherwise.

    """
    failed_states = ("unknown", "evicted", "errored", "unresponsive")
    failed_rank_list = {}

    # Check the state of each rank.
    if "response" in data and "members" in data["response"]:
        for member in data["response"]["members"]:
            rank_info = [
                "{}: {}".format(key, member[key]) for key in sorted(member)]
            print(
                "Rank {} info:\n  {}".format(
                    member["rank"], "\n  ".join(rank_info)))
            if "state" in member and member["state"].lower() in failed_states:
                failed_rank_list[member["rank"]] = member["state"]

    # Display the details of any failed ranks
    for rank in sorted(failed_rank_list):
        print(
            "Rank {} failed with state '{}'".format(
                rank, failed_rank_list[rank]))

    # Return True if no ranks failed
    return not bool(failed_rank_list)
