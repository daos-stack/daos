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
from daos_utils_base import DaosCommandBase
import re


class DaosCommand(DaosCommandBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Defines a object representing a daos command."""

    METHOD_REGEX = {
        "run": r"(.*)",
        "container_create": r"container ([0-9a-f-]+)",
        # daos pool list-cont returns the date, host name, and container UUID
        # as below:
        # 03/31-21:32:24.53 wolf-3 2f69b198-8478-472e-b6c8-02a451f4de1b
        # UUID is made up of 36 characters of hex and -.
        "pool_list_cont": r"([0-9a-f-]{36})",
        # Sample pool query output.
        # 04/19-18:31:26.90 wolf-3 Pool 3e59b386-fda0-404e-af7e-3ff0a38d1f81,
        #    ntarget=8, disabled=0
        # 04/19-18:31:26.90 wolf-3 Pool space info:
        # 04/19-18:31:26.90 wolf-3 - Target(VOS) count:8
        # 04/19-18:31:26.90 wolf-3 - SCM:
        # 04/19-18:31:26.90 wolf-3   Total size: 1000000000
        # 04/19-18:31:26.90 wolf-3   Free: 999997440, min:124999680,
        #     max:124999680, mean:124999680
        # 04/19-18:31:26.90 wolf-3 - NVMe:
        # 04/19-18:31:26.90 wolf-3   Total size: 0
        # 04/19-18:31:26.90 wolf-3   Free: 0, min:0, max:0, mean:0
        # 04/19-18:31:26.90 wolf-3 Rebuild idle, 0 objs, 0 recs
        "pool_query": r"(?:Pool\s*([A-Za-z0-9-]+),\s*ntarget=([0-9])," +
                      r"\s*disabled=([0-9])|Target\(VOS\) count:\s*([0-9])|" +
                      r"(?:SCM:\s+.*|NVMe:\s+.*)Total\s+size:\s+([0-9]+)" +
                      r"\s+.*Free:\s+([0-9]+),\s+min:([0-9]+),\s+" +
                      r"max:([0-9]+),\s+mean:([0-9]+)|" +
                      r"Rebuild\s*idle,\s*([0-9]+)\s*objs,\s*([0-9]+)\s*recs)",
        # Sample list-attrs output.
        # 04/19-21:16:31.62 wolf-3 Pool attributes:
        # 04/19-21:16:31.62 wolf-3 attr0
        # 04/19-21:16:31.62 wolf-3 attr1
        "pool_list_attrs": r"\b([^:\s]+)\n",
        # Sample get-attr output - no line break.
        # 04/19-21:16:32.66 wolf-3 Pool's attr2 attribute value:
        # 04/19-21:16:32.66 wolf-3 val2
        "pool_get_attr": r"\b(\S+)$",
        "container_query":
            r"Pool UUID:\s+([0-9a-f-]+)\n" +
            r"Container UUID:\s+([0-9a-f-]+)\n" +
            r"Number of snapshots:\s+(\d+)\n" +
            r"Latest Persistent Snapshot:\s+(\d+)\n" +
            r"Highest Aggregated Epoch:\s+(\d+)",
        # Sample get-attr output - no line break.
        # 04/20-17:47:07.86 wolf-3 Container's attr1 attribute value:  04
        # /20-17:47:07.86 wolf-3 val1
        "container_get_attr": r"value:  \S+ \S+ (.+)$",
        # Sample list output.
        #  04/20-17:52:33.63 wolf-3 Container attributes:
        #  04/20-17:52:33.63 wolf-3 attr1
        #  04/20-17:52:33.63 wolf-3 attr2
        "container_list_attrs": r"\n \S+ \S+ (.+)"
    }

    def pool_query(self, pool, sys_name=None, svc=None, sys=None):
        """Query a pool.

        Args:
            pool (str): pool UUID
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.
            svc (str, optional): the pool service replicas, e.g. '1,2,3'.
                Defaults to None.
            sys (str, optional): [description]. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool query command fails.

        """
        return self._get_result(
            ("pool", "query"), pool=pool, sys_name=sys_name, svc=svc, sys=sys)

    def container_create(self, pool, sys_name=None, svc=None, cont=None,
                         path=None, cont_type=None, oclass=None,
                         chunk_size=None, properties=None, acl_file=None):
        # pylint: disable=too-many-arguments
        """Create a container.

        Args:
            pool (str): UUID of the pool in which to create the container
            sys_name (str, optional):  DAOS system name context for servers.
                Defaults to None.
            svc (str, optional): the pool service replicas, e.g. '1,2,3'.
                Defaults to None.
            cont (str, optional): container UUID. Defaults to None.
            path (str, optional): container namespace path. Defaults to None.
            cont_type (str, optional): the type of container to create. Defaults
                to None.
            oclass (str, optional): object class. Defaults to None.
            chunk_size (str, optional): chunk size of files created. Supports
                suffixes: K (KB), M (MB), G (GB), T (TB), P (PB), E (EB).
                Defaults to None.
            properties (str, optional): String of comma-separated <name>:<value>
                pairs defining the container properties. Defaults to None
            acl_file (str, optional): ACL file. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container create command fails.

        """
        return self._get_result(
            ("container", "create"), pool=pool, sys_name=sys_name, svc=svc,
            cont=cont, path=path, type=cont_type, oclass=oclass,
            chunk_size=chunk_size, properties=properties, acl_file=acl_file)

    def container_destroy(self, pool, svc, cont, force=None, sys_name=None):
        """Destroy a container.

        Args:
            pool (str): UUID of the pool in which to create the container
            svc (str): the pool service replicas, e.g. '1,2,3'.
            cont (str): container UUID.
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
            ("container", "destroy"), pool=pool, sys_name=sys_name, svc=svc,
            cont=cont, force=force)

    def container_get_acl(self, pool, svc, cont,
                          verbose=False, outfile=None):
        """Get the ACL for a given container.

        Args:
            pool (str): Pool UUID
            svc (str): Service replicas
            cont (str): Container for which to get the ACL.
            verbose (bool, optional): Verbose mode.
            outfile (str, optional): Write ACL to file.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container get-acl command fails.

        """
        return self._get_result(
            ("container", "get-acl"), pool=pool, svc=svc, cont=cont,
            verbose=verbose, outfile=outfile)

    def container_delete_acl(self, pool, svc, cont, principal):
        """Delete an entry for a given principal in an existing container ACL.

        Args:
            pool (str): Pool UUID
            svc (str): Service replicas
            cont (str): Container for which to get the ACL.
            principal (str): principal portion of the ACL.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container delete-acl command fails.

        """
        return self._get_result(
            ("container", "delete-acl"), pool=pool, svc=svc, cont=cont,
            principal=principal)

    def container_overwrite_acl(self, pool, svc, cont, acl_file):
        """Overwrite the ACL for a given container.

        Args:
            pool (str): Pool UUID
            svc (str): Service replicas
            cont (str): Container for which to get the ACL.
            acl_file (str): input file containing ACL

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container overwrite-acl command fails.

        """
        return self._get_result(
            ("container", "overwrite-acl"), pool=pool, svc=svc, cont=cont,
            acl_file=acl_file)

    def container_update_acl(self, pool, svc, cont, entry=None, acl_file=None):
        """Add or update the ACL entries for a given container.

        Args:
            pool (str): Pool UUID
            svc (str): Service replicas
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
            ("container", "update-acl"), pool=pool, svc=svc, cont=cont,
            entry=entry, acl_file=acl_file)

    def pool_list_cont(self, pool, svc, sys_name=None):
        """List containers in the given pool.

        Args:
            pool (str): Pool UUID
            svc (str): Service replicas. If there are multiple, numbers must be
                separated by comma like 1,2,3
            sys_name (str, optional): System name. Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool list-containers command fails.

        """
        return self._get_result(
            ("pool", "list-containers"), pool=pool, svc=svc, sys_name=sys_name)

    def pool_set_attr(self, pool, attr, value, svc):
        """Set pool attribute.

        Args:
            pool (str): Pool UUID.
            attr (str): Attribute name.
            value (str): Attribute value
            svc (str): Service replicas.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool set-attr command fails.

        """
        return self._get_result(
            ("pool", "set-attr"), pool=pool, svc=svc, attr=attr, value=value)

    def pool_get_attr(self, pool, attr, svc):
        """Set pool attribute.

        Args:
            pool (str): Pool UUID.
            attr (str): Pool UUID.
            svc (str): Service replicas.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool query command fails.

        """
        return self._get_result(
            ("pool", "get-attr"), pool=pool, svc=svc, attr=attr)

    def pool_list_attrs(self, pool, svc):
        """List pool attributes.

        Args:
            pool (str): Pool UUID.
            svc (str): Service replicas.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos pool list-attrs command fails.

        """
        return self._get_result(("pool", "list-attrs"), pool=pool, svc=svc)

    def container_query(self, pool, cont, svc=None, sys_name=None):
        """Query a container.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            svc (str, optional): pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container query command fails.

        """
        return self._get_result(
            ("container", "query"), pool=pool, svc=svc, cont=cont,
            sys_name=sys_name)

    def container_update_acl(self, pool, cont, entry, svc=None):
        """Call daos container update-acl.
        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            entry (str): Container acl entry to be updated.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container update-acl command fails.

        """
        return self._get_result(
            ("container", "update-acl"),
            pool=pool, svc=svc, cont=cont, entry=entry)

    def container_set_prop(self, pool, cont, prop, value, svc=None):
        """Call daos container set-prop.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            prop (str): Container property-name.
            value (str): Container property-name value to set.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container set-prop command fails.

        """
        prop_value = ":".join([prop, value])
        return self._get_result(
            ("container", "set-prop"),
            pool=pool, svc=svc, cont=cont, prop=prop_value)

    def container_get_prop(self, pool, cont, svc=None):
        """Call daos container get-prop.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container get-prop command fails.

        """
        return self._get_result(
            ("container", "get-prop"), pool=pool, svc=svc, cont=cont)

    def container_set_owner(self, pool, cont, user, group, svc=None):
        """Call daos container set-owner.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            user (str): New-user who will own the container.
            group (str): New-group who will own the container.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container set-owner command fails.

        """
        return self._get_result(
            ("container", "set-owner"),
            pool=pool, svc=svc, cont=cont, user=user, group=group)

    def container_set_attr(
            self, pool, cont, attr, val, svc=None, sys_name=None):
        """Call daos container set-attr.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            attr (str): Attribute name.
            val (str): Attribute value.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container set-attr command fails.

        """
        return self._get_result(
            ("container", "set-attr"), pool=pool, svc=svc, cont=cont,
            sys_name=sys_name, attr=attr, value=val)

    def container_get_attr(self, pool, cont, attr, svc=None, sys_name=None):
        """Call daos container get-attr.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            attr (str): Attribute name.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos get-attr command fails.

        """
        return self._get_result(
            ("container", "get-attr"), pool=pool, svc=svc, cont=cont,
            sys_name=sys_name, attr=attr)

    def container_list_attrs(self, pool, cont, svc=None, sys_name=None):
        """Call daos container list-attrs.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container list-attrs command fails.

        """
        return self._get_result(
            ("container", "list-attrs"), pool=pool, svc=svc, cont=cont,
            sys_name=sys_name)

    def container_create_snap(self, pool, cont, snap_name=None, epoch=None,
                              svc=None, sys_name=None):
        """Call daos container create-snap.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            snap_name (str, optional): Snapshot name. Defaults to None.
            epoch (str, optional): Epoch number. Defaults to None.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.

        Returns:
            dict: Dictionary that stores the created epoch in the key "epoch".

        Raises:
            CommandFailure: if the daos container create-snap command fails.

        """
        self._get_result(
            ("container", "create-snap"), pool=pool, svc=svc, cont=cont,
            sys_name=sys_name, snap=snap_name, epc=epoch)

        # Sample create-snap output.
        # snapshot/epoch 1582610056530034697 has been created
        data = {}
        match = re.findall(
            r"[A-Za-z\/]+\s([0-9]+)\s[a-z\s]+", self.result.stdout)
        if match:
            data["epoch"] = match[0]
        return data

    def container_destroy_snap(self, pool, cont, snap_name=None, epc=None,
                               svc=None, sys_name=None, epcrange=None):
        """Call daos container destroy-snap.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            snap_name (str, optional): Snapshot name. Defaults to None.
            epc (str, optional): Epoch value of the snapshot to be destroyed.
                Defaults to None.
            svc (str, optional): Pool service replicas, e.g., '1,2,3'. Defaults
                to None.
            sys_name (str, optional): DAOS system name context for servers.
                Defaults to None.
            epcrange (str, optional): Epoch range in the format "<start>-<end>".
                Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other
                information.

        Raises:
            CommandFailure: if the daos container destroy-snap command fails.

        """
        kwargs = {
            "pool": pool,
            "svc": svc,
            "cont": cont,
            "sys_name": sys_name,
            "snap": snap_name,
            "epc": epc,
            "epcrange": epcrange
        }
        return self._get_result(("container", "destroy-snap"), **kwargs)

    def container_list_snaps(self, pool, cont, svc=None):
        """List snapshot in a container.

        Args:
            pool (str): Pool UUID.
            cont (str): Container UUID.
            svc (str): Service replicas. Defaults to None.

        Returns:
            dict: Dictionary that contains epoch values in key "epochs". Value
                is a list of string.
        """
        self._get_result(
            ("container", "list-snaps"), pool=pool, cont=cont, svc=svc)

        # Sample container list-snaps output.
        # Container's snapshots :
        # 1598478249040609297 1598478258840600594 1598478287952543761
        data = {}
        match = re.findall(r"(\d{19})", self.result.stdout)
        if match:
            data["epochs"] = match
        return data
