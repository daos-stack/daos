"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re
import traceback

from daos_utils_base import DaosCommandBase
from general_utils import list_to_str, dict_to_str


class DaosCommand(DaosCommandBase):
    # pylint: disable=too-many-public-methods
    """Defines a object representing a daos command."""

    METHOD_REGEX = {
        "run": r"(.*)",
        "container_query":
            r"Pool UUID:\s+([0-9a-f-]+)\n"
            r"Container UUID:\s+([0-9a-f-]+)\n"
            r"Number of snapshots:\s+(\d+)\n"
            r"Latest Persistent Snapshot:\s+(\d+)\n"
            r"Highest Aggregated Epoch:\s+(\d+)",
    }

    def system_query(self):
        """Query the DAOS system for client endpoint information.

        Args:
            None

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos system query command fails.
        """
        return self._get_json_result(("system", "query"))

    def pool_query(self, pool, sys_name=None, sys=None):
        """Query a pool.

        Args:
            pool (str): pool UUID or label
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.
            sys (str, optional): [description]. Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos pool query command fails.

        """
        return self._get_json_result(
            ("pool", "query"), pool=pool, sys_name=sys_name, sys=sys)

    def pool_autotest(self, pool):
        """Runs autotest for pool

        Args:
            pool (str): pool UUID or label

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool autotest command fails.
        """
        return self._get_result(
            ("pool", "autotest"), pool=pool)

    def container_create(self, pool, sys_name=None, path=None, cont_type=None,
                         oclass=None, dir_oclass=None, file_oclass=None, chunk_size=None,
                         properties=None, acl_file=None, label=None):
        # pylint: disable=too-many-arguments
        """Create a container.

        Args:
            pool (str): pool UUID or label in which to create the container
            sys_name (str, optional):  DAOS system name context for servers.
                Defaults to None.
            path (str, optional): container namespace path. Defaults to None.
            cont_type (str, optional): the type of container to create. Defaults
                to None.
            oclass (str, optional): default object class. Defaults to None.
            dir_oclass (str, optional): default directory object class. Defaults to None.
            file_oclass (str, optional): default file object class. Defaults to None.
            chunk_size (str, optional): chunk size of files created. Supports
                suffixes: K (KB), M (MB), G (GB), T (TB), P (PB), E (EB).
                Defaults to None.
            properties (str, optional): String of comma-separated <name>:<value>
                pairs defining the container properties. Defaults to None
            acl_file (str, optional): ACL file. Defaults to None.
            label (str, optional): Container label. Defaults to None.

        Returns:
            dict: the daos json command output converted to a python dictionary

        Raises:
            CommandFailure: if the daos container create command fails.

        """
        # Default to RANK fault domain (rd_lvl:1) when not specified
        if properties:
            if ('rd_lvl' not in properties) and ('rf_lvl' not in properties):
                properties += ',rd_lvl:1'
        else:
            properties = 'rd_lvl:1'
        return self._get_json_result(
            ("container", "create"), pool=pool, sys_name=sys_name, path=path,
            type=cont_type, oclass=oclass, dir_oclass=dir_oclass, file_oclass=file_oclass,
            chunk_size=chunk_size, properties=properties, acl_file=acl_file, label=label)

    def container_clone(self, src, dst):
        """Clone a container to a new container.

        Args:
            src (str): the source, formatted as daos://<pool>/<cont>
            dst (str): the destination, formatted as daos://<pool>/<cont>

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container clone command fails.

        """
        return self._get_result(
            ("container", "clone"), src=src, dst=dst)

    def container_destroy(self, pool, cont, force=None, sys_name=None):
        """Destroy a container.

        Args:
            pool (str): pool UUID or label in which to create the container
            cont (str): container UUID or label
            force (bool, optional): Force the container destroy. Defaults to
                None.
            sys_name (str, optional):  DAOS system name context for servers.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container destroy command fails.

        """
        return self._get_result(
            ("container", "destroy"), pool=pool, sys_name=sys_name,
            cont=cont, force=force)

    def container_check(self, pool, cont, sys_name=None, path=None):
        """Check the integrity of container objects.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            sys_name (str, optional):  DAOS system name context for servers.
                Defaults to None.
            path (str): Container namespace path. Defaults to None

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos container check command fails.

        """
        return self._get_json_result(
            ("container", "check"), pool=pool, cont=cont,
            sys_name=sys_name, path=path)

    def container_get_acl(self, pool, cont, verbose=False, outfile=None):
        """Get the ACL for a given container.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label for which to get the ACL.
            verbose (bool, optional): Verbose mode.
            outfile (str, optional): Write ACL to file.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container get-acl command fails.

        """
        return self._get_result(
            ("container", "get-acl"), pool=pool, cont=cont,
            verbose=verbose, outfile=outfile)

    def container_delete_acl(self, pool, cont, principal):
        """Delete an entry for a given principal in an existing container ACL.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label for which to get the ACL.
            principal (str): principal portion of the ACL.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container delete-acl command fails.

        """
        return self._get_result(
            ("container", "delete-acl"), pool=pool, cont=cont,
            principal=principal)

    def container_overwrite_acl(self, pool, cont, acl_file):
        """Overwrite the ACL for a given container.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label for which to get the ACL.
            acl_file (str): input file containing ACL

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container overwrite-acl command fails.

        """
        return self._get_result(
            ("container", "overwrite-acl"), pool=pool, cont=cont,
            acl_file=acl_file)

    def container_update_acl(self, pool, cont, entry=None, acl_file=None):
        """Add or update the ACL entries for a given container.

        Args:
            pool (str): pool UUID or label
            cont (str): Container for which to get the ACL.
            entry (bool, optional): Add or modify a single ACL entry
            acl_file (str, optional): Input file containing ACL

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container get-acl command fails.

        """
        return self._get_result(
            ("container", "update-acl"), pool=pool, cont=cont,
            entry=entry, acl_file=acl_file)

    def container_list(self, pool, sys_name=None):
        """List containers in the given pool.

        Args:
            pool (str): pool UUID or label
            sys_name (str, optional): System name. Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos container list command fails.

        """
        # Sample output.
        # {
        #   "response": [
        #     {
        #       "uuid": "bad80a98-aabd-498c-b001-6547cd061c8c",
        #       "label": "container_label_not_set"
        #     },
        #     {
        #       "uuid": "dd9fc365-5729-4736-9d34-e46504a4a92d",
        #       "label": "mkc1"
        #     }
        #   ],
        #   "error": null,
        #   "status": 0
        # }
        return self._get_json_result(
            ("container", "list"), pool=pool, sys_name=sys_name)

    def pool_set_attr(self, pool, attr, value, sys_name=None):
        """Set single pool attribute.

        Args:
            pool (str): pool UUID or label
            attr (str): attribute name
            value (str): attribute value
            sys_name (str): DAOS system name. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool set-attr command fails.

        """
        return self._get_result(
            ("pool", "set-attr"), pool=pool, attr=list_to_str([attr, value], ':'),
            sys_name=sys_name)

    def pool_set_attrs(self, pool, attrs, sys_name=None):
        """Set multiple pool attributes.

        Args:
            pool (str): Pool UUID.
            attrs (dict): Attribute name/value pairs.
            sys_name (str): DAOS system name. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool set-attr command fails.

        """
        return self._get_result(
            ("pool", "set-attr"), pool=pool, attr=dict_to_str(attrs, ",", ":"), sys_name=sys_name)

    def pool_get_attr(self, pool, attr, sys_name=None):
        """Set pool attribute.

        Args:
            pool (str): pool UUID or label
            attr (str): attribute name
            sys_name (str): DAOS system name. Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos pool query command fails.

        """
        return self._get_json_result(
            ("pool", "get-attr"), pool=pool, attr=attr, sys_name=sys_name)

    def pool_list_attrs(self, pool, sys_name=None, verbose=False):
        """List pool attributes.

        Args:
            pool (str): pool UUID or label
            sys_name (str): DAOS system name. Defaults to None.
            verbose (bool): False - name only. True - name and value. Defaults
                to False.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos pool list-attrs command fails.

        """
        return self._get_json_result(
            ("pool", "list-attrs"), pool=pool, sys_name=sys_name,
            verbose=verbose)

    def container_query(self, pool, cont, sys_name=None):
        """Query a container.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos container query command fails.

        """
        return self._get_json_result(
            ("container", "query"), pool=pool, cont=cont, sys_name=sys_name)

    def container_set_prop(self, pool, cont, prop, value):
        """Call daos container set-prop for a single property.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            prop (str): container property-name
            value (str): container property-name value to set

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container set-prop command fails.

        """
        prop_value = list_to_str([prop, value], ":")
        return self._get_result(
            ("container", "set-prop"),
            pool=pool, cont=cont, prop=prop_value)

    def container_set_props(self, pool, cont, props, sys_name=None):
        """Set multiple container properties.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            props (dict): Property name/value pairs.
            sys_name (str): DAOS system name. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool set-attr command fails.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self._get_result(
            ("container", "set-prop"), pool=pool, cont=cont, attr=dict_to_str(props, ",", ":"),
            sys_name=sys_name)

    def container_get_prop(self, pool, cont, properties=None):
        """Call daos container get-prop.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            properties (list): "name" field(s). Defaults to None.

        Returns:
            str: JSON that contains the command output.

        Raises:
            CommandFailure: if the daos container get-prop command fails.

        """
        # pylint: disable=wrong-spelling-in-comment
        # Sample output
        # {
        #   "response": [
        #     {
        #       "value": 0,
        #       "name": "alloc_oid",
        #       "description": "Highest Allocated OID"
        #     },
        #     {
        #       "value": "off",
        #       "name": "cksum",
        #       "description": "Checksum"
        #     },
        #     {
        #       "value": 32768,
        #       "name": "cksum_size",
        #       "description": "Checksum Chunk Size"
        #     },
        #     {
        #       "value": "off",
        #       "name": "compression",
        #       "description": "Compression"
        #     },
        #     {
        #       "value": "off",
        #       "name": "dedup",
        #       "description": "Deduplication"
        #     },
        #     {
        #       "value": 4096,
        #       "name": "dedup_threshold",
        #       "description": "Dedupe Threshold"
        #     },
        #     {
        #       "value": 65536,
        #       "name": "ec_cell_sz",
        #       "description": "EC Cell Size"
        #     },
        #     {
        #       "value": "1",
        #       "name": "ec_pda",
        #       "description": "Performance domain affinity level of EC"
        #     },
        #     {
        #       "value": "off",
        #       "name": "encryption",
        #       "description": "Encryption"
        #     },
        #     {
        #       "value": "1",
        #       "name": "global_version",
        #       "description": "Global Version"
        #     },
        #     {
        #       "value": "mkano@",
        #       "name": "group",
        #       "description": "Group"
        #     },
        #     {
        #       "value": "mkc1",
        #       "name": "label",
        #       "description": "Label"
        #     },
        #     {
        #       "value": "unknown (0)",
        #       "name": "layout_type",
        #       "description": "Layout Type"
        #     },
        #     {
        #       "value": 1,
        #       "name": "layout_version",
        #       "description": "Layout Version"
        #     },
        #     {
        #       "value": 0,
        #       "name": "max_snapshot",
        #       "description": "Max Snapshot"
        #     },
        #     {
        #       "value": "mkano@",
        #       "name": "owner",
        #       "description": "Owner"
        #     },
        #     {
        #       "value": "rf0",
        #       "name": "rf",
        #       "description": "Redundancy Factor"
        #     },
        #     {
        #       "value": "rank (1)",
        #       "name": "rd_lvl",
        #       "description": "Redundancy Level"
        #     },
        #     {
        #       "value": "3",
        #       "name": "rp_pda",
        #       "description": "Performance domain affinity level of RP"
        #     },
        #     {
        #       "value": "off",
        #       "name": "srv_cksum",
        #       "description": "Server Checksumming"
        #     },
        #     {
        #       "value": "HEALTHY",
        #       "name": "status",
        #       "description": "Health"
        #     },
        #     {
        #       "value": [
        #         "A::OWNER@:rwdtTaAo",
        #         "A:G:GROUP@:rwtT"
        #       ],
        #       "name": "acl",
        #       "description": "Access Control List"
        #     }
        #   ],
        #   "error": null,
        #   "status": 0
        # }
        props = list_to_str(properties, ',') if properties else None

        return self._get_json_result(
            ("container", "get-prop"), pool=pool, cont=cont, prop=props)

    def container_set_owner(self, pool, cont, user, group):
        """Call daos container set-owner.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            user (str): New-user who will own the container.
            group (str): New-group who will own the container.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container set-owner command fails.

        """
        return self._get_result(
            ("container", "set-owner"),
            pool=pool, cont=cont, user=user, group=group)

    def container_set_attr(self, pool, cont, attrs, sys_name=None):
        """Call daos container set-attr.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            attrs (dict): Attribute key/val pairs.
            sys_name (str): DAOS system name. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool set-attr command fails.

        """
        return self._get_result(
            ("container", "set-attr"), pool=pool, cont=cont,
            attr=dict_to_str(attrs, ",", ":"), sys_name=sys_name)

    def container_get_attr(self, pool, cont, attr, sys_name=None):
        """Call daos container get-attr for a single attribute.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            attr (str): attribute name
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            dict: the daos json command output converted to a python dictionary

        Raises:
            CommandFailure: if the daos get-attr command fails.

        """
        return self._get_json_result(
            ("container", "get-attr"), pool=pool, cont=cont, attr=attr, sys_name=sys_name)

    def container_get_attrs(self, pool, cont, attrs, sys_name=None):
        """Call daos container get-attr for multiple attributes.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            attrs (list): Attribute names.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            dict: the daos json command output converted to a python dictionary

        Raises:
            CommandFailure: if the daos get-attr command fails.

        """
        return self._get_json_result(
            ("container", "get-attr"), pool=pool, cont=cont,
            attr=list_to_str(attrs, ","), sys_name=sys_name)

    def container_list_attrs(self, pool, cont, sys_name=None, verbose=False):
        """Call daos container list-attrs.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.
            verbose (bool, optional): True - fetch values of all attributes.

        Returns:
            dict: the daos json command output converted to a python dictionary

        Raises:
            CommandFailure: if the daos container list-attrs command fails.

        """
        return self._get_json_result(
            ("container", "list-attrs"), pool=pool, cont=cont, sys_name=sys_name,
            verbose=verbose)

    def container_create_snap(self, pool, cont, snap_name=None, epoch=None,
                              sys_name=None):
        """Call daos container create-snap.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            snap_name (str, optional): Snapshot name. Defaults to None.
            epoch (str, optional): Epoch number. Defaults to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos container create-snap command fails.

        """
        return self._get_json_result(
            ("container", "create-snap"), pool=pool, cont=cont,
            sys_name=sys_name, snap=snap_name, epc=epoch)

    def container_destroy_snap(self, pool, cont, snap_name=None, epc=None,
                               sys_name=None, epcrange=None):
        """Call daos container destroy-snap.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            snap_name (str, optional): Snapshot name. Defaults to None.
            epc (str, optional): Epoch value of the snapshot to be destroyed. Defaults to None.
            sys_name (str, optional): DAOS system name context for servers. Defaults to None.
            epcrange (str, optional): Epoch range in the format "<start>-<end>". Defaults to None.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos container destroy-snap command fails.

        """
        return self._get_json_result(
            ("container", "destroy-snap"), pool=pool, cont=cont,
            sys_name=sys_name, snap=snap_name, epc=epc, epcrange=epcrange)

    def container_list_snaps(self, pool, cont):
        """List snapshot in a container.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the command fails.

        """
        return self._get_json_result(
            ("container", "list-snaps"), pool=pool, cont=cont)

    def object_query(self, pool, cont, oid, sys_name=None):
        """Call daos object query and return its output with a dictionary.

        Args:
            pool (str): pool UUID or label
            cont (str): container UUID or label
            oid (str): oid hi lo value in the format <hi>.<lo>
            sys_name (str, optional): System name. Defaults to None.

        Returns:
            dict: cmd output
                oid: (oid.hi, oid.lo)
                ver: num
                grp_nr: num
                layout: [{grp: num, replica: [(n0, n1), (n2, n3)...]}, ...]
                Each row of replica nums is a tuple and stored top->bottom.

        Raises:
            CommandFailure: if the daos object query command fails.
        """
        self._get_result(
            ("object", "query"), pool=pool, cont=cont,
            oid=oid, sys_name=sys_name)

        # Sample daos object query output.
        # oid: 1152922453794619396.1 ver 0 grp_nr: 2
        # grp: 0
        # replica 0 1
        # replica 1 0
        # grp: 1
        # replica 0 0
        # replica 1 1
        data = {}
        vals = re.findall(
            r"oid:\s+([\d.]+)\s+ver\s+(\d+)\s+grp_nr:\s+(\d+)|"
            r"grp:\s+(\d+)\s+|"
            r"replica\s+(\d+)\s+(\d+)\s*", self.result.stdout_text)

        try:
            oid_vals = vals[0][0]
            oid_list = oid_vals.split(".")
            oid_hi = oid_list[0]
            oid_lo = oid_list[1]
            data["oid"] = (oid_hi, oid_lo)
            data["ver"] = vals[0][1]
            data["grp_nr"] = vals[0][2]

            data["layout"] = []
            for idx in range(1, len(vals)):
                if vals[idx][3] == "":
                    if "replica" in data["layout"][-1]:
                        data["layout"][-1]["replica"].append((vals[idx][4], vals[idx][5]))
                    else:
                        data["layout"][-1]["replica"] = [(vals[idx][4], vals[idx][5])]
                else:
                    data["layout"].append({"grp": vals[idx][3]})
        except IndexError:
            traceback.print_exc()
            self.log.error("--- re.findall output ---")
            self.log.error(vals)

        return data

    def filesystem_copy(self, src, dst, preserve_props=None):
        """Copy a POSIX container or path to another POSIX container or path.

        Args:
            src (str): The source, formatted as
                daos:<pool>/<cont>/<path> or posix:<path>
            dst (str): The destination, formatted as
                daos:<pool>/<cont>/<path> or posix:<path>
            preserve_props (str): The filename to read or write container properties

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos filesystem copy command fails.

        """
        return self._get_result(
            ("filesystem", "copy"), src=src, dst=dst, preserve_props=preserve_props)

    def version(self):
        """Call daos version.

        Returns:
            dict: JSON output

        Raises:
            CommandFailure: if the daos version command fails.

        """
        return self._get_json_result(("version",))
