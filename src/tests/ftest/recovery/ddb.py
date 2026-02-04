"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import os
import re

from apricot import TestWithServers
from ddb_utils import DdbCommand
from general_utils import create_string_buffer, get_random_string, report_errors
from pydaos.raw import DaosObjClass, IORequest
from run_utils import command_as_user, run_remote


def insert_objects(context, container, object_count, dkey_count, akey_count, base_dkey,
                   base_akey, base_data):
    """Insert objects, dkeys, akeys, and data into the container.

    Args:
        context (DaosContext):
        container (TestContainer): Container to insert objects.
        object_count (int): Number of objects to insert.
        dkey_count (int): Number of dkeys to insert.
        akey_count (int): Number of akeys to insert.
        base_dkey (str): Base dkey. Index numbers will be appended to it.
        base_akey (str):Base akey. Index numbers will be appended to it.
        base_data (str):Base data that goes inside akey. Index numbers will be appended
            to it.

    Returns:
        tuple: Inserted objects, dkeys, akeys, and data as (ioreqs, dkeys, akeys,
        data_list)

    """
    ioreqs = []
    dkeys = []
    akeys = []
    data_list = []

    container.open()

    for obj_index in range(object_count):
        # Insert object.
        ioreqs.append(IORequest(
            context=context, container=container.container, obj=None,
            objtype=DaosObjClass.OC_S1))

        for dkey_index in range(dkey_count):
            # Prepare the dkey to insert into the object.
            dkey_str = " ".join(
                [base_dkey, str(obj_index), str(dkey_index)]).encode("utf-8")
            dkeys.append(create_string_buffer(value=dkey_str, size=len(dkey_str)))

            for akey_index in range(akey_count):
                # Prepare the akey to insert into the dkey.
                akey_str = " ".join(
                    [base_akey, str(obj_index), str(dkey_index),
                     str(akey_index)]).encode("utf-8")
                akeys.append(create_string_buffer(value=akey_str, size=len(akey_str)))

                # Prepare the data to insert into the akey.
                data_str = " ".join(
                    [base_data, str(obj_index), str(dkey_index),
                     str(akey_index)]).encode("utf-8")
                data_list.append(create_string_buffer(value=data_str, size=len(data_str)))
                c_size = ctypes.c_size_t(ctypes.sizeof(data_list[-1]))

                # Insert dkeys, akeys, and the data.
                ioreqs[-1].single_insert(
                    dkey=dkeys[-1], akey=akeys[-1], value=data_list[-1], size=c_size)

    return (ioreqs, dkeys, akeys, data_list)


class DdbTest(TestWithServers):
    """Test ddb subcommands.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DdbTest object."""
        super().__init__(*args, **kwargs)
        # how many objects and keys to insert/expect
        self.object_count = 5
        self.dkey_count = 2
        self.akey_count = 1
        # Generate random keys and data to insert into the object.
        self.random_dkey = get_random_string(10)
        self.random_akey = get_random_string(10)
        self.random_data = get_random_string(10)

    def run_cmd_check_result(self, command):
        """Run given command as root and check its result.

        Args:
            command (str): Command to execute.
        """
        command_root = command_as_user(command=command, user="root")
        result = run_remote(
            log=self.log, hosts=self.hostlist_servers, command=command_root)
        if not result.passed:
            self.fail(f"{command} failed on {result.failed_hosts}!")

    def test_recovery_ddb_ls(self):
        """Test ddb ls.

        1. Verify container UUID.
        2. Verify object count in the container.
        3. Verify there are two dkeys for every object. Also verify the dkey string and
        the size.
        4. Verify there is one akey for every dkey. Also verify the key string and the
        size.
        5. Restart the server for the cleanup.
        6. Reset the container and the pool to prepare for the cleanup.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=recovery
        :avocado: tags=DdbTest,ddb_cmd,test_recovery_ddb_ls
        """
        # This is where we load pool for MD-on-SSD. It's called tmpfs_mount in ddb
        # prov_mem documentation, but use daos_load_path here for clarity.
        daos_load_path = "/mnt/daos_load"
        md_on_ssd = self.server_managers[0].manager.job.using_control_metadata
        if md_on_ssd:
            self.log_step("MD-on-SSD: Create a directory to load pool data under /mnt.")
            self.run_cmd_check_result(command=f"mkdir {daos_load_path}")

        self.log_step("Create a pool and a container.")
        pool = self.get_pool()
        container = self.get_container(pool)

        if md_on_ssd:
            ddb_command = DdbCommand(
                server_host=self.server_managers[0].hosts[0:1], path=self.bin,
                vos_path="\"\"")
        else:
            # Find the vos file name. e.g., /mnt/daos0/<pool_uuid>/vos-0.
            vos_paths = self.server_managers[0].get_vos_files(pool)
            if not vos_paths:
                self.fail(
                    f"vos file wasn't found in {self.server_managers[0].get_vos_paths(pool)[0]}")
            ddb_command = DdbCommand(self.server_managers[0].hosts[0:1], self.bin, vos_paths[0])

        errors = []

        object_count = self.object_count
        dkey_count = self.dkey_count
        akey_count = self.akey_count
        self.log_step("Insert objects with API.")
        insert_objects(
            context=self.context, container=container, object_count=object_count,
            dkey_count=dkey_count, akey_count=akey_count, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)

        self.log_step("Stop server to use ddb.")
        self.get_dmg_command().system_stop()

        db_path = None
        if md_on_ssd:
            self.log_step(f"MD-on-SSD: Load pool dir to {daos_load_path}")
            control_metadata_path = self.server_managers[0].manager.job.yaml.\
                metadata_params.path.value
            db_path = os.path.join(control_metadata_path, "daos_control", "engine0")
            ddb_command.prov_mem(db_path=db_path, tmpfs_mount=daos_load_path)

        self.log_step("Verify container UUID.")
        if md_on_ssd:
            # "ddb ls" command for MD-on-SSD is quite different.
            # PMEM: ddb /mnt/daos/<pool_uuid>/vos-0 ls
            # MD-on-SSD: ddb --db_path=/var/tmp/daos_testing/control_metadata/daos_control
            # /engine0 --vos_path /mnt/daos_load/<pool_uuid>/vos-0 ls
            ddb_command.db_path.update(value=" ".join(["--db_path", db_path]))
            ddb_command.vos_path.update(
                value=os.path.join(daos_load_path, pool.uuid.lower(), "vos-0"))
        cmd_result = ddb_command.list_component()
        # Sample output.
        #   Listing contents of '/'
        #   CONT: (/[0]) /3082b7d3-32f9-41ea-bcbf-5d6450c1b34f
        # stdout is a list which contains each line as separate element. Concatenate them
        # to single string so that we can apply regex.
        # Matches the container uuid
        uuid_regex = r"([0-f]{8}-[0-f]{4}-[0-f]{4}-[0-f]{4}-[0-f]{12})"
        match = re.search(uuid_regex, cmd_result.joined_stdout)
        if match is None:
            self.fail("Unexpected output from ddb command, unable to parse.")
        self.log.info("Container UUID from ddb ls = %s", match.group(1))

        actual_uuid = match.group(1).lower()
        expected_uuid = container.uuid.lower()
        if actual_uuid != expected_uuid:
            errors.append(
                f"Unexpected container UUID! Expected = {expected_uuid}; Actual = "
                f"{actual_uuid}")

        self.log_step("Verify object count in the container.")
        cmd_result = ddb_command.list_component(component_path="[0]")
        # Sample output.
        #   Listing contents of 'CONT: (/[0]) /3082b7d3-32f9-41ea-bcbf-5d6450c1b34f'
        #   OBJ: (/[0]/[0]) /3082b7d3-32f9-41ea-bcbf-5d6450c1b34f/937030214649643008.1.0.1
        #   OBJ: (/[0]/[1]) /3082b7d3-32f9-41ea-bcbf-5d6450c1b34f/937030214649643009.1.0.1
        #   OBJ: (/[0]/[2]) /3082b7d3-32f9-41ea-bcbf-5d6450c1b34f/937030214649643016.1.0.1
        # Matches an object id. (4 digits separated by a period '.')
        object_id_regex = r"\d+\.\d+\.\d+\.\d+"
        match = re.findall(object_id_regex, cmd_result.joined_stdout)
        self.log.info("List objects match = %s", match)

        actual_object_count = len(match)
        if actual_object_count != object_count:
            errors.append(
                f"Unexpected object count! Expected = {object_count}; "
                f"Actual = {actual_object_count}")

        msg = ("Verify there are two dkeys for every object. Also verify the dkey string "
               "and the size.")
        self.log_step(msg)
        dkey_regex = f"/{uuid_regex}/{object_id_regex}/(.*)"
        actual_dkey_count = 0
        for obj_index in range(object_count):
            component_path = f"[0]/[{obj_index}]"
            cmd_result = ddb_command.list_component(component_path=component_path)
            # Sample output. There are three lines, but a line break is added to fit into
            # the code.
            # Listing contents of 'OBJ: (/[0]/[0])
            #   /a78b65a1-31f4-440b-95e1-b4ead193b3f1/281479271677953.0.0.2'
            # DKEY: (/[0]/[0]/[0])
            #   /a78b65a1-31f4-440b-95e1-b4ead193b3f1/281479271677953.0.0.2/GSWOPOF1EX 0 0
            # DKEY: (/[0]/[0]/[1])
            #   /a78b65a1-31f4-440b-95e1-b4ead193b3f1/281479271677953.0.0.2/GSWOPOF1EX 0 1
            match = re.findall(dkey_regex, cmd_result.joined_stdout)

            actual_dkey_count += len(match)

            # Verify dkey string.
            for idx in range(self.dkey_count):
                actual_dkey = match[idx][1]
                if self.random_dkey not in actual_dkey:
                    msg = (f"Unexpected dkey! obj_i = {obj_index}. Expected = {self.random_dkey}; "
                           f"Actual = {actual_dkey}")
                    errors.append(msg)

        self.log_step("Verify there are two dkeys for every object.")
        expected_dkey_count = object_count * dkey_count
        if actual_dkey_count != expected_dkey_count:
            errors.append(
                f"Unexpected number of dkeys! Expected = {expected_dkey_count}; "
                f"Actual = {actual_dkey_count}")

        self.log_step(
            "Verify there is one akey for every dkey. Also verify the key string and "
            "the size.")
        akey_count = 0
        for obj_index in range(object_count):
            for dkey_index in range(dkey_count):
                component_path = f"[0]/[{obj_index}]/[{dkey_index}]"
                cmd_result = ddb_command.list_component(component_path=component_path)
                ls_out = cmd_result.joined_stdout
                msg = (f"List akeys obj_index = {obj_index}, dkey_index = {dkey_index}, "
                       f"stdout = {ls_out}")
                self.log.info(msg)
                # Output is in the same format as dkey, so use the same regex. There are
                # two lines, but lines breaks are added to fit into the code.
                # Listing contents of 'DKEY: (/[0]/[0]/[0])
                #   /a78b65a1-31f4-440b-95e1-b4ead193b3f1/281479271677953.0.0.2/
                #   GSWOPOF1EX 0 0'
                # AKEY: (/[0]/[0]/[0]/[0])
                #   /a78b65a1-31f4-440b-95e1-b4ead193b3f1/281479271677953.0.0.2/
                #   GSWOPOF1EX 0 0/OOJ2TNAHS7 0 0 0
                match = re.findall(f"{dkey_regex}/(.*)", ls_out)
                akey_count += len(match)

                # Verify akey string. As in dkey, ignore the numbers at the end.
                actual_akey = match[0][2]
                if self.random_akey not in actual_akey:
                    msg = (f"Unexpected akey! obj_index = {obj_index}; dkey_index = {dkey_index}; "
                           f"Expected = {self.random_akey}; Actual = {actual_akey}")
                    errors.append(msg)

        self.log_step("Verify there is one akey for every dkey.")
        if expected_dkey_count != akey_count:
            msg = (f"Unexpected number of akeys! Expected = {expected_dkey_count}; "
                   f"Actual = {akey_count}")
            errors.append(msg)

        if md_on_ssd:
            self.log_step(f"MD-on-SSD: Clean {daos_load_path}")
            self.run_cmd_check_result(command=f"umount {daos_load_path}")
            self.run_cmd_check_result(command=f"rm -rf {daos_load_path}")

        self.log_step("Restart the server for the cleanup.")
        self.get_dmg_command().system_start()

        self.log_step("Reset the container and the pool to prepare for the cleanup.")
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()

        report_errors(test=self, errors=errors)
