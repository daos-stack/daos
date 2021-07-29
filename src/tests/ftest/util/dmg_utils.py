#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

from grp import getgrgid
from pwd import getpwuid
import re

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

    def network_scan(self, provider=None):
        """Get the result of the dmg network scan command.

        Args:
            provider (str): name of network provider tied to the device

        Returns:
            dict: dictionary of output in JSON format

        Raises:
            CommandFailure: if the dmg network scan command fails.

        """
        # Sample json output for --provider=all. Output is abbreviated.
        # {
        #   "response": {
        #     "host_errors": {},
        #     "HostFabrics": {
        #     "7046809990821404843": {
        #         "HostFabric": {
        #           "Interfaces": [
        #             {
        #               "Provider": "ofi+psm2",
        #               "Device": "ib1",
        #               "NumaNode": 1,
        #               "Priority": 0,
        #               "NetDevClass": 32
        #             },
        #             {
        #               "Provider": "ofi+psm2",
        #               "Device": "ib0",
        #               "NumaNode": 0,
        #               "Priority": 1,
        #               "NetDevClass": 32
        #             },
        #             {
        #               "Provider": "ofi+verbs;ofi_rxm",
        #               "Device": "ib0",
        #               "NumaNode": 0,
        #               "Priority": 2,
        #               "NetDevClass": 32
        #             },
        #             {
        #               "Provider": "ofi+verbs;ofi_rxm",
        #               "Device": "ib1",
        #               "NumaNode": 1,
        #               "Priority": 3,
        #               "NetDevClass": 32
        #             }
        #           ],
        #           "Providers": [
        #             "ofi+psm2",
        #             "ofi+verbs;ofi_rxm",
        #             "ofi+tcp;ofi_rxm",
        #             "ofi+verbs",
        #             "ofi+tcp",
        #             "ofi+sockets"
        #           ],
        #           "NumaCount": 2,
        #           "CoresPerNuma": 24
        #         },
        #         "HostSet": "localhost:10001"
        #       }
        #     }
        #   },
        #   "error": null,
        #   "status": 0
        # }
        return self._get_json_result(("network", "scan"), provider=provider)

    def storage_scan(self, verbose=False):
        """Get the result of the dmg storage scan command.

        Args:
            verbose (bool, optional): create verbose output. Defaults to False.

        Returns:
            dict: dictionary of output in JSON format

        Raises:
            CommandFailure: if the dmg storage scan command fails.

        """
        # Sample json output. --verbose and non-verbose combined. Output is
        # abbreviated.
        # {
        #     "response": {
        #         "host_errors": {},
        #         "HostStorage": {
        #         "5044895924483624073": {
        #             "storage": {
        #             "nvme_devices": [
        #                 {
        #                     "info": "",
        #                     "model": "INTEL SSDPED1K750GA",
        #                     "serial": "PHKS750500GU750BGN",
        #                     "pci_addr": "0000:90:00.0",
        #                     "fw_rev": "E2010435",
        #                     "socket_id": 1,
        #                     "health_stats": null,
        #                     "namespaces": [
        #                       {
        #                         "id": 1 ,
        #                         "size": 750156374016
        #                       }
        #                     ],
        #                   "smd_devices": null
        #                 },
        #                 {
        #                     "info": "",
        #                     "model": "",
        #                     "serial": "",
        #                     "pci_addr": "0000:da:00.0",
        #                     "fw_rev": "",
        #                     "socket_id": 1,
        #                     "health_stats": null,
        #                     "namespaces": [
        #                       {
        #                         "id": 1,
        #                         "size": 750156374016
        #                       }
        #                   ],
        #                   "smd_devices": null
        #                 }
        #             ],
        #             "scm_modules": null,
        #             "scm_namespaces": [
        #                 {
        #                     "uuid": "2270f4a6-b24b-4dba-a450-6f2d5d688708",
        #                     "blockdev": "pmem1",
        #                     "dev": "namespace1.0",
        #                     "numa_node": 1,
        #                     "size": 3183575302144,
        #                     "mount": null
        #                 },
        #                 {
        #                     "uuid": "7963f81a-0a6b-4cca-9845-bb68f0e81c46",
        #                     "blockdev": "pmem0",
        #                     "dev": "namespace0.0",
        #                     "numa_node": 0,
        #                     "size": 3183575302144,
        #                     "mount": null
        #                 }
        #             ],
        #             "scm_mount_points": null,
        #             "smd_info": null,
        #             "reboot_required": false
        #           },
        #           "hosts": "localhost:10001"
        #         }
        #       }
        #     },
        #     "error": null,
        #     "status": 0
        # }
        return self._get_json_result(("storage", "scan"), verbose=verbose)

    def storage_format(self, reformat=False, timeout=30, verbose=False,
                       force=False):
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
            force (bool, optional): force storage format on a host, stopping any
                running engines (CAUTION: destructive operation). Defaults to
                False.

        Returns:
            CmdResult: an avocado CmdResult object containing the dmg command
                information, e.g. exit status, stdout, stderr, etc.

        Raises:
            CommandFailure: if the dmg storage format command fails.

        """
        saved_timeout = self.timeout
        self.timeout = timeout
        self._get_result(
            ("storage", "format"), reformat=reformat, verbose=verbose,
            force=force)
        self.timeout = saved_timeout
        return self.result

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
            CommandFailure: if the dmg storage query command fails.

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
            CommandFailure: if the dmg storage query command fails.

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
            CommandFailure: if the dmg storage query command fails.

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
            CommandFailure: if the dmg storage query command fails.

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
                    target_list=None, svcn=None, acl_file=None, size=None,
                    tier_ratio=None, properties=None, label=None):
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
            acl_file (str, optional): ACL file. Defaults to None.
            size (str, optional): NVMe pool size to create with tier_ratio.
                Defaults to None.
            tier_ratio (str, optional): SCM pool size to create as a ratio of
                size. Defaults to None.
            properties (str, optional): Comma separated name:value string
                Defaults to None
            label (str, optional): Pool label. Defaults to None.

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
            "size": size,
            "tier_ratio": tier_ratio,
            "scm_size": scm_size,
            "nvme_size": nvme_size,
            "nsvc": svcn,
            "acl_file": acl_file,
            "properties": properties,
            "label": label
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
        data["scm_per_rank"] = output["response"]["tier_bytes"][0]
        data["nvme_per_rank"] = output["response"]["tier_bytes"][1]

        return data

    def pool_query(self, pool):
        """Query a pool with the dmg command.

        Args:
            uuid (str): Pool UUID to query.

        Raises:
            CommandFailure: if the dmg pool query command fails.

        Returns:
            dict: dictionary of output in JSON format.

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
        return self._get_json_result(("pool", "query"), pool=pool)

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
        # Sample JSON Output:
        # {
        #    "response": {
        #        "status": 0,
        #        "pools": [
        #        {
        #            "uuid": "3dd3f313-6e37-4890-9e64-93a34d04e9f5",
        #            "label": "foobar",
        #            "svc_reps": [
        #            0
        #            ]
        #        },
        #        {
        #            "uuid": "6871d543-9a12-4530-b704-d937197c131c",
        #            "label": "foobaz",
        #            "svc_reps": [
        #            0
        #            ]
        #        },
        #        {
        #            "uuid": "aa503e26-e974-4634-ac5a-738ee00f0c39",
        #            "svc_reps": [
        #            0
        #            ]
        #        }
        #        ]
        #    },
        #    "error": null,
        #    "status": 0
        # }
        output = self._get_json_result(("pool", "list"))

        data = {}
        if output["response"] is None or output["response"]["pools"] is None:
            return data

        for pool in output["response"]["pools"]:
            data[pool["uuid"]] = pool["svc_reps"]
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

    def pool_extend(self, pool, ranks):
        """Extend the daos_server pool.

        Args:
            pool (str): Pool uuid.
            ranks (int): Ranks of the daos_server to extend

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                       information.

        Raises:
            CommandFailure: if the dmg pool extend command fails.

        """
        return self._get_result(
            ("pool", "extend"), pool=pool, ranks=ranks)

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
            dict: dictionary of output in JSON format

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
        #         "state": "excluded",
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
            dict: dictionary of output in JSON format

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

    def system_erase(self):
        """Erase system metadata prior to reformat.

        Raises:
            CommandFailure: if the dmg system erase command fails.

        Returns:
            dict: dictionary of output in JSON format.

        """
        return self._get_json_result(("system", "erase"))

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

    def pool_evict(self, pool):
        """Evict a pool.

        Args:
            pool (str):  UUID of DAOS pool to evict connection to

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the dmg pool evict command fails.

        """
        return self._get_result(("pool", "evict"), pool=pool)

    def config_generate(self, access_points, num_engines=None, min_ssds=None,
                        net_class=None):
        """Produce a server configuration.

        Args:
            access_points (str): Comma separated list of access point addresses.
            num_pmem (int): Number of SCM (pmem) devices required per
                storage host in DAOS system. Defaults to None.
            num_nvme (int): Minimum number of NVMe devices required per storage
                host in DAOS system. Defaults to None.
            net_class (str): Network class preferred. Defaults to None.
                i.e. "best-available"|"ethernet"|"infiniband"

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        """
        return self._get_result(
            ("config", "generate"), access_points=access_points,
            num_engines=num_engines, min_ssds=min_ssds, net_class=net_class)

    def telemetry_metrics_list(self, host):
        """List telemetry metrics.

        Args:
            host (str): Server host from which to obtain the metrics

        Raises:
            CommandFailure: if the dmg system query command fails.

        Returns:
            dict: dictionary of output in JSON format

        """
        return self._get_json_result(
            ("telemetry", "metrics", "list"), host=host)

    def telemetry_metrics_query(self, host, metrics=None):
        """Query telemetry metrics.

        Args:
            host (str): Server host from which to obtain the metrics
            metrics (str, None): Comma-separated list of metric names to query.
                Defaults to None which will query all metric names.

        Raises:
            CommandFailure: if the dmg system query command fails.

        Returns:
            dict: dictionary of output in JSON format

        """
        # Sample output (metric="process_start_time_seconds"):
        # {
        # "response": {
        #   "metric_sets": [
        #     {
        #       "name": "process_start_time_seconds",
        #       "description": "Start time of the process since unix epoch in
        #                       seconds.",
        #       "type": 3,
        #       "metrics": [
        #         {
        #           "labels": {},
        #           "value": 1622576326.6
        #         }
        #       ]
        #     }
        #   ]
        # },
        # "error": null,
        # "status": 0
        # }
        return self._get_json_result(
            ("telemetry", "metrics", "query"), host=host, metrics=metrics)


def check_system_query_status(data):
    """Check if any server crashed.

    Args:
        data (dict): dictionary of system query data obtained from
            DmgCommand.system_query()

    Returns:
        bool: True if no server crashed, False otherwise.

    """
    failed_states = ("unknown", "excluded", "errored", "unresponsive")
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
