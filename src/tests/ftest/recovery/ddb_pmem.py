"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ctypes
import os

from apricot import TestWithServers
from ddb_utils import DdbCommand
from exception_utils import CommandFailure
from file_utils import distribute_files
from general_utils import (DaosTestError, create_string_buffer, get_random_string, report_errors,
                           run_command)
from pydaos.raw import DaosObjClass, IORequest
from run_utils import get_clush_command


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


def copy_remote_to_local(remote_file_path, test_dir, remote):
    """Copy the given file from the server node to the local test node and retrieve
    the original name.

    Args:
        remote_file_path (str): File path to copy to local.
        test_dir (str): Test directory. Usually self.test_dir.
        remote (str): Remote hostname to copy file from.
    """
    # Use clush --rcopy to copy the file from the remote server node to the local test
    # node. clush will append .<server_hostname> to the file when copying.
    args = f"--rcopy {remote_file_path} --dest {test_dir}"
    clush_command = get_clush_command(hosts=remote, args=args, timeout=60)
    try:
        run_command(command=clush_command, timeout=None)
    except DaosTestError as error:
        raise DaosTestError(
            f"ERROR: Copying {remote_file_path} from {remote}: {error}") from error

    # Remove the appended .<server_hostname> from the copied file.
    current_file_path = "".join([remote_file_path, ".", remote])
    mv_command = f"mv {current_file_path} {remote_file_path}"
    try:
        run_command(command=mv_command)
    except DaosTestError as error:
        raise DaosTestError(
            f"ERROR: Moving {current_file_path} to {remote_file_path}: {error}") from error


class DdbPMEMTest(TestWithServers):
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

    def test_recovery_ddb_rm(self):
        """Test rm.

        1. Create a pool and a container. Insert objects, dkeys, and akeys.
        2. Stop the server to use ddb.
        3. Find the vos file name. e.g., /mnt/daos0/<pool_uuid>/vos-0.
        4. Call ddb rm to remove the akey.
        5. Restart the server to use the API.
        6. Reset the object, container, and pool to use the API after server restart.
        7. Call list_akey() in pydaos API to verify that the akey was removed.
        8. Stop the server to use ddb.
        9. Call ddb rm to remove the dkey.
        10. Restart the server to use the API.
        11. Reset the object, container, and pool to use the API after server restart.
        12. Call list_dkey() in pydaos API to verify that the dkey was removed.
        13. Stop the server to use ddb.
        14. Call ddb rm to remove the object.
        15. Restart the server to use daos command.
        16. Reset the container and pool so that cleanup works.
        17. Call "daos container list-objects <pool_uuid> <cont_uuid>" to verify that the
        object was removed.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=DdbPMEMTest,ddb_cmd,test_recovery_ddb_rm
        """
        # 1. Create a pool and a container. Insert objects, dkeys, and akeys.
        pool = self.get_pool(connect=True)
        container = self.get_container(pool)

        # Insert one object with one dkey and one akey with API.
        obj_dataset = insert_objects(
            context=self.context, container=container, object_count=1,
            dkey_count=1, akey_count=2, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)
        ioreqs = obj_dataset[0]
        dkeys_inserted = obj_dataset[1]
        akeys_inserted = obj_dataset[2]

        # For debugging/reference, check that the dkey and the akey we just inserted are
        # returned from the API.
        akeys_api = ioreqs[0].list_akey(dkey=dkeys_inserted[0])
        self.log.info("akeys from API (before) = %s", akeys_api)
        dkeys_api = ioreqs[0].list_dkey()
        self.log.info("dkeys from API (before) = %s", dkeys_api)

        # For debugging/reference, check that the object was inserted using daos command.
        list_obj_out = self.get_daos_command().container_list_objects(
            pool=pool.identifier, cont=container.uuid)
        self.log.info("Object list (before) = %s", list_obj_out["response"])

        # 2. Need to stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 3. Find the vos file name.
        vos_paths = self.server_managers[0].get_vos_files(pool)
        if not vos_paths:
            self.fail(f"vos file wasn't found in {self.server_managers[0].get_vos_path(pool)}")
        ddb_command = DdbCommand(self.server_managers[0].hosts[0:1], self.bin, vos_paths[0])

        # 4. Call ddb rm to remove the akey.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]/[0]")
        self.log.info("rm akey stdout = %s", cmd_result.joined_stdout)

        # 5. Restart the server to use the API.
        dmg_command.system_start()

        # 6. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()
        ioreqs[0].obj.open()

        # 7. Call list_akey() in pydaos API to verify that the akey was removed.
        akeys_api = ioreqs[0].list_akey(dkey=dkeys_inserted[0])
        self.log.info("akeys from API (after) = %s", akeys_api)

        errors = []
        expected_len = len(akeys_inserted) - 1
        actual_len = len(akeys_api)
        if actual_len != expected_len:
            msg = (f"Unexpected number of akeys after ddb rm! Expected = {expected_len}; "
                   f"Actual = {actual_len}")
            errors.append(msg)

        # 8. Stop the server to use ddb.
        dmg_command.system_stop()

        # 9. Call ddb rm to remove the dkey.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]")
        self.log.info("rm dkey stdout = %s", cmd_result.joined_stdout)

        # 10. Restart the server to use the API.
        dmg_command.system_start()

        # 11. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()
        ioreqs[0].obj.open()

        # 12. Call list_dkey() in pydaos API to verify that the dkey was removed.
        dkeys_api = ioreqs[0].list_dkey()
        self.log.info("dkeys from API (after) = %s", dkeys_api)

        expected_len = len(dkeys_inserted) - 1
        actual_len = len(dkeys_api)
        if actual_len != expected_len:
            msg = (f"Unexpected number of dkeys after ddb rm! Expected = {expected_len}; "
                   f"Actual = {actual_len}")
            errors.append(msg)

        # 13. Stop the server to use ddb.
        dmg_command.system_stop()

        # 14. Call ddb rm to remove the object.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]")
        self.log.info("rm object stdout = %s", cmd_result.joined_stdout)

        # 15. Restart the server to use daos command.
        dmg_command.system_start()

        # 16. Reset the container and pool so that cleanup works.
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()

        # 17. Call "daos container list-objects <pool_uuid> <cont_uuid>" to verify that
        # the object was removed.
        list_obj_out = self.get_daos_command().container_list_objects(
            pool=pool.identifier, cont=container.uuid)
        obj_list = list_obj_out["response"]
        self.log.info("Object list (after) = %s", obj_list)

        expected_len = len(ioreqs) - 1
        if obj_list:
            actual_len = len(obj_list)
        else:
            actual_len = 0
        if actual_len != expected_len:
            msg = (f"Unexpected number of objects after ddb rm! Expected = {expected_len}; "
                   f"Actual = {actual_len}")
            errors.append(msg)

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_recovery_ddb_load(self):
        """Test ddb value_load.

        1. Create a pool and a container.
        2. Insert one object with one dkey with the API.
        3. Stop the server to use ddb.
        4. Find the vos file name. e.g., /mnt/daos0/<pool_uuid>/vos-0.
        5. Load new data into [0]/[0]/[0]/[0]
        6. Restart the server.
        7. Reset the object, container, and pool to use the API.
        8. Verify the data in the akey with single_fetch().

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=DdbPMEMTest,ddb_cmd,test_recovery_ddb_load
        """
        # 1. Create a pool and a container.
        pool = self.get_pool(connect=True)
        container = self.get_container(pool)

        # 2. Insert one object with one dkey with API.
        obj_dataset = insert_objects(
            context=self.context, container=container, object_count=1,
            dkey_count=1, akey_count=1, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)
        ioreqs = obj_dataset[0]
        dkeys_inserted = obj_dataset[1]
        akeys_inserted = obj_dataset[2]
        data_list = obj_dataset[3]

        # For debugging/reference, call single_fetch and get the data just inserted.
        # Pass in size + 1 to single_fetch to avoid the no-space error.
        data_size = len(data_list[0]) + 1
        data = ioreqs[0].single_fetch(
            dkey=dkeys_inserted[0], akey=akeys_inserted[0], size=data_size)
        self.log.info("data (before) = %s", data.value.decode('utf-8'))

        # 3. Stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 4. Find the vos file name.
        host = self.server_managers[0].hosts[0:1]
        vos_paths = self.server_managers[0].get_vos_files(pool)
        if not vos_paths:
            self.fail(f"vos file wasn't found in {self.server_managers[0].get_vos_path(pool)}")
        ddb_command = DdbCommand(host, self.bin, vos_paths[0])

        # 5. Load new data into [0]/[0]/[0]/[0]
        # Create a file in test node.
        load_file_path = os.path.join(self.test_dir, "new_data.txt")
        new_data = "New akey data 0123456789"
        with open(load_file_path, "w", encoding="utf-8") as file:
            file.write(new_data)

        # Copy the created file to server node.
        result = distribute_files(self.log, host, load_file_path, load_file_path, False)
        if not result.passed:
            raise CommandFailure(f"ERROR: Copying new_data.txt to {result.failed_hosts}")

        # The file with the new data is ready. Run ddb load.
        ddb_command.value_load(component_path="[0]/[0]/[0]/[0]", load_file_path=load_file_path)

        # 6. Restart the server.
        dmg_command.system_start()

        # 7. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()
        ioreqs[0].obj.open()

        # 8. Verify the data in the akey with single_fetch().
        data_size = len(new_data) + 1
        data = ioreqs[0].single_fetch(
            dkey=dkeys_inserted[0], akey=akeys_inserted[0], size=data_size)
        actual_data = data.value.decode('utf-8')
        self.log.info("data (after) = %s", actual_data)

        errors = []
        if new_data != actual_data:
            msg = f"ddb load failed! Expected = {new_data}; Actual = {actual_data}"
            errors.append(msg)

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_recovery_ddb_dump_value(self):
        """Test ddb dump_value.

        1. Create a pool and a container.
        2. Insert one object with one dkey with API.
        3. Stop the server to use ddb.
        4. Find the vos file name. e.g., /mnt/daos0/<pool_uuid>/vos-0.
        5. Dump the two akeys to files.
        6. Verify the content of the files.
        7. Restart the server for the cleanup.
        8. Reset the object, container, and pool to prepare for the cleanup.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=DdbPMEMTest,ddb_cmd,test_recovery_ddb_dump_value
        """
        # 1. Create a pool and a container.
        pool = self.get_pool(connect=True)
        container = self.get_container(pool)

        # 2. Insert one object with one dkey with API.
        obj_dataset = insert_objects(
            context=self.context, container=container, object_count=1,
            dkey_count=1, akey_count=2, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)
        ioreqs = obj_dataset[0]
        data_list = obj_dataset[3]

        # 3. Stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 4. Find the vos file name.
        vos_paths = self.server_managers[0].get_vos_files(pool)
        if not vos_paths:
            self.fail(f"vos file wasn't found in {self.server_managers[0].get_vos_path(pool)}")
        ddb_command = DdbCommand(self.server_managers[0].hosts[0:1], self.bin, vos_paths[0])

        # 5. Dump the two akeys to files.
        akey1_file_path = os.path.join(self.test_dir, "akey1.txt")
        ddb_command.value_dump(
            component_path="[0]/[0]/[0]/[0]", out_file_path=akey1_file_path)
        akey2_file_path = os.path.join(self.test_dir, "akey2.txt")
        ddb_command.value_dump(
            component_path="[0]/[0]/[0]/[1]", out_file_path=akey2_file_path)

        # Copy them from remote server node to local test node.
        copy_remote_to_local(
            remote_file_path=akey1_file_path, test_dir=self.test_dir,
            remote=self.hostlist_servers[0])
        copy_remote_to_local(
            remote_file_path=akey2_file_path, test_dir=self.test_dir,
            remote=self.hostlist_servers[0])

        # 6. Verify the content of the files.
        actual_akey1_data = None
        with open(akey1_file_path, "r", encoding="utf-8") as file:
            actual_akey1_data = file.readlines()[0]
        actual_akey2_data = None
        with open(akey2_file_path, "r", encoding="utf-8") as file:
            actual_akey2_data = file.readlines()[0]

        errors = []
        str_data_list = []
        # Convert the data to string.
        for data in data_list:
            str_data_list.append(data.value.decode("utf-8"))
        # Verify that we were able to obtain the data and akey1 and akey2 aren't the same.
        if actual_akey1_data is None or actual_akey2_data is None or \
                actual_akey1_data == actual_akey2_data:
            msg = (f"Invalid dumped value! Dumped akey1 data = {actual_akey1_data}; "
                   f"Dumped akey2 data = {actual_akey2_data}")
            errors.append(msg)
        # Verify that the data we obtained with ddb are the ones we wrote. The order isn't
        # deterministic, so check with "in".
        if actual_akey1_data not in str_data_list or \
                actual_akey2_data not in str_data_list:
            msg = (f"Unexpected dumped value! Dumped akey data 1 = {actual_akey1_data}; Dumped "
                   f"akey data 2 = {actual_akey2_data}; Expected data list = {str_data_list}")
            errors.append(msg)

        # 7. Restart the server for the cleanup.
        dmg_command.system_start()

        # 8. Reset the object, container, and pool to prepare for the cleanup.
        ioreqs[0].obj.close()
        container.close()
        pool.disconnect()
        pool.connect()
        container.open()
        ioreqs[0].obj.open()

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")
