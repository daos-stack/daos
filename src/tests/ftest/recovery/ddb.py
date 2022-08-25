"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from general_utils import report_errors, run_pcmd, insert_objects, \
    distribute_files, DaosTestError, get_random_string, copy_remote_to_local
from ddb_utils import DdbCommand
from exception_utils import CommandFailure


class DdbTest(TestWithServers):
    """Test ddb subcommands.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DdbTest object."""
        super().__init__(*args, **kwargs)
        # Generate random keys and data to insert into the object.
        self.random_dkey = get_random_string(10)
        self.random_akey = get_random_string(10)
        self.random_data = get_random_string(10)

    def get_vos_file_path(self):
        """Get the VOS file path.

        If there are multiple VOS files, returns the first file obtained by "ls".

        Returns:
            str: VOS file path such as /mnt/daos/<pool_uuid>/vos-0

        """
        hosts = NodeSet(self.hostlist_servers[0])
        vos_path = os.path.join("/mnt/daos", self.pool.uuid.lower())
        command = " ".join(["sudo", "ls", vos_path])
        cmd_out = run_pcmd(hosts=hosts, command=command)

        # return vos_file
        for file in cmd_out[0]["stdout"]:
            # Assume the VOS file has "vos" in the file name.
            if "vos" in file:
                self.log.info("vos_file: %s", file)
                return file

        self.fail("vos file wasn't found in /mnt/daos/{}".format(self.pool.uuid.lower()))

        return None  # to appease pylint

    def test_recovery_ddb_ls(self):
        """Test ddb ls.

        1. Verify container UUID.
        2. Verify object count in the container.
        3. Verify there are two dkeys for every object. Also verify the dkey string and
        the size.
        4. Verify there is one akey for every dkey. Also verify the key string and the
        size.
        5. Verify data length in every akey.
        6. Restart the server for the cleanup.
        7. Reset the container and the pool to prepare for the cleanup.

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=ddb,test_recovery_ddb_ls
        """
        # Create a pool and a container.
        self.add_pool()
        self.add_container(pool=self.pool)

        # Find the vos file name. e.g., /mnt/daos/<pool_uuid>/vos-0.
        ddb_command = DdbCommand(
            server_host=NodeSet(self.hostlist_servers[0]), path=self.bin,
            mount_point="/mnt/daos", pool_uuid=self.pool.uuid,
            vos_file=self.get_vos_file_path())

        errors = []

        # Insert objects with API.
        object_count = 5
        dkey_count = 2
        akey_count = 1
        insert_objects(
            context=self.context, container=self.container, object_count=5,
            dkey_count=2, akey_count=1, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)

        # Need to stop the server to use ddb.
        self.get_dmg_command().system_stop()

        # 1. Verify container UUID.
        cmd_result = ddb_command.list_component()
        # Sample output.
        # [0] d4e0c836-17bd-4df3-b255-929732486bab
        # stdout is a list which contains each line as separate element. Concatenate them
        # to single string so that we can apply regex.
        ls_out = "\n".join(cmd_result[0]["stdout"])
        match = re.search(r"\[\d+\]\s+([a-f0-9\-]+)", ls_out)
        self.log.info("Container UUID from ddb ls = %s", match.group(1))

        actual_uuid = match.group(1).lower()
        expected_uuid = self.container.uuid.lower()
        if actual_uuid != expected_uuid:
            msg = "Unexpected container UUID! Expected = {}; Actual = {}".format(
                expected_uuid, actual_uuid)
            errors.append(msg)

        # 2. Verify object count in the container.
        cmd_result = ddb_command.list_component(component_path="[0]")
        # Sample output.
        # /d4e0c836-17bd-4df3-b255-929732486bab/
        # [0] '281479271677953.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [1] '281479271677954.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [2] '281479271677955.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [3] '281479271677956.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [4] '281479271677957.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        ls_out = "\n".join(cmd_result[0]["stdout"])
        match = re.findall(r"(\[\d+\])\s+\'(\d+\.\d+)\'", ls_out)
        self.log.info("List objects match = %s", match)

        actual_object_count = len(match)
        expected_object_count = self.params.get("object_count", "/run/*")
        if actual_object_count != expected_object_count:
            errors.append(
                "Unexpected object count! Expected = {}; Actual = {}".format(
                    expected_object_count, actual_object_count))

        # 3. Verify there are two dkeys for every object. Also verify the dkey string and
        # the size.
        dkey_akey_regex = r"(\[\d+\])\s+\'(.+)\'\s+\((\d+)\)"
        actual_dkey_count = 0
        for obj_index in range(object_count):
            component_path = "[0]/[{}]".format(obj_index)
            cmd_result = ddb_command.list_component(component_path=component_path)
            ls_out = "\n".join(cmd_result[0]["stdout"])
            # Sample output.
            # /d4e0c836-17bd-4df3-b255-929732486bab/281479271677953.0.0/
            # [0] 'Sample dkey 0 0' (15)
            # [1] 'Sample dkey 0 1' (15)
            match = re.findall(dkey_akey_regex, ls_out)

            actual_dkey_count += len(match)

            # Verify dkey string.
            actual_dkey_1 = " ".join(match[0][1].split())
            actual_dkey_2 = " ".join(match[1][1].split())
            # We're not testing the numbers in the string because it's not deterministic.
            if self.random_dkey not in actual_dkey_1:
                msg = ("Unexpected dkey! obj_i = {}. Expected = {}; "
                       "Actual = {}").format(obj_index, self.random_dkey, actual_dkey_1)
            if self.random_dkey not in actual_dkey_2:
                msg = ("Unexpected dkey! obj_i = {}. Expected = {}; "
                       "Actual = {}").format(obj_index, self.random_dkey, actual_dkey_2)

            # Verify the dkey size field.
            for dkey in match:
                dkey_string = dkey[1]
                # int(dkey[2]) is the dkey size; avoid pylint error.
                if len(dkey_string) != int(dkey[2]):
                    msg = ("Wrong dkey size! obj_index = {}. String = {}; "
                           "Size = {}").format(obj_index, dkey_string, int(dkey[2]))
                    errors.append(msg)

        # Verify there are two dkeys for every object.
        expected_dkey_count = object_count * dkey_count
        if actual_dkey_count != expected_dkey_count:
            msg = "Unexpected number of dkeys! Expected = {}; Actual = {}".format(
                expected_dkey_count, actual_dkey_count)
            errors.append(msg)

        # 4. Verify there is one akey for every dkey. Also verify the key string and the
        # size.
        akey_count = 0
        for obj_index in range(object_count):
            for dkey_index in range(dkey_count):
                component_path = "[0]/[{}]/[{}]".format(obj_index, dkey_index)
                cmd_result = ddb_command.list_component(component_path=component_path)
                ls_out = "\n".join(cmd_result[0]["stdout"])
                msg = "List akeys obj_index = {}, dkey_index = {}, stdout = {}".format(
                    obj_index, dkey_index, ls_out)
                self.log.info(msg)
                # Output is in the same format as dkey, so use the same regex.
                # /d4e0c836-17bd-4df3-b255-929732486bab/281479271677954.0.0/'
                # Sample dkey 1 0'/
                # [0] 'Sample akey 1 0 0' (17)
                match = re.findall(dkey_akey_regex, ls_out)

                akey_count += len(match)

                # Verify akey string. As in dkey, ignore the numbers at the end.
                actual_akey = " ".join(match[0][1].split())
                if self.random_akey not in actual_akey:
                    msg = ("Unexpected akey! obj_index = {}; dkey_index = {}; "
                           "Expected = {}; Actual = {}").format(
                               obj_index, dkey_index, self.random_akey, actual_akey)
                    errors.append(msg)

                # Verify akey size.
                akey_string = match[0][1]
                akey_size = int(match[0][2])
                if len(akey_string) != akey_size:
                    msg = ("Wrong akey size! obj_index = {}; dkey_index = {}; "
                           "akey = {}; akey size = {}").format(
                               obj_index, dkey_index, akey_string, akey_size)
                    errors.append(msg)

        # Verify there is one akey for every dkey.
        if expected_dkey_count != akey_count:
            msg = "Unexpected number of akeys! Expected = {}; Actual = {}".format(
                expected_dkey_count, akey_count)
            errors.append(msg)

        # 5. Verify data length in every akey.
        for obj_index in range(object_count):
            for dkey_index in range(dkey_count):
                component_path = "[0]/[{}]/[{}]/[0]".format(obj_index, dkey_index)
                cmd_result = ddb_command.list_component(component_path=component_path)
                ls_out = "\n".join(cmd_result[0]["stdout"])
                path_stat = ("akey data obj_index = {}, dkey_index = {}, "
                             "stdout = {}").format(obj_index, dkey_index, ls_out)
                self.log.info(path_stat)
                # Sample output.
                # [0] Single Value (Length: 17 bytes)
                match = re.findall(r"Length:\s+(\d+)\s+bytes", ls_out)
                data_length = int(match[0])
                expected_data_length = len(self.random_data) + 6
                if data_length != expected_data_length:
                    msg = "Wrong data length! {}; Expected = {}; Actual = {}".format(
                        path_stat, expected_data_length, data_length)
                    errors.append(msg)

        # 6. Restart the server for the cleanup.
        self.get_dmg_command().system_start()

        # 7. Reset the container and the pool to prepare for the cleanup.
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        self.get_dmg_command().system_start()

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_recovery_ddb_rm(self):
        """Test rm.

        1. Create a pool and a container. Insert objects, dkeys, and akeys.
        2. Stop the server to use ddb.
        3. Find the vos file name. e.g., /mnt/daos/<pool_uuid>/vos-0.
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

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=ddb,test_recovery_ddb_rm
        """
        # 1. Create a pool and a container. Insert objects, dkeys, and akeys.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # Insert one object with one dkey and one akey with API.
        obj_dataset = insert_objects(
            context=self.context, container=self.container, object_count=1,
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
            pool=self.pool.identifier, cont=self.container.uuid)
        self.log.info("Object list (before) = %s", list_obj_out["response"])

        # 2. Need to stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 3. Find the vos file name.
        vos_file = self.get_vos_file_path()
        host = NodeSet(self.hostlist_servers[0])
        ddb_command = DdbCommand(
            server_host=host, path=self.bin, mount_point="/mnt/daos",
            pool_uuid=self.pool.uuid, vos_file=vos_file)

        # 4. Call ddb rm to remove the akey.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]/[0]")
        self.log.info("rm akey stdout = %s", cmd_result[0]["stdout"])

        # 5. Restart the server to use the API.
        dmg_command.system_start()

        # 6. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        ioreqs[0].obj.open()

        # 7. Call list_akey() in pydaos API to verify that the akey was removed.
        akeys_api = ioreqs[0].list_akey(dkey=dkeys_inserted[0])
        self.log.info("akeys from API (after) = %s", akeys_api)

        errors = []
        expected_len = len(akeys_inserted) - 1
        actual_len = len(akeys_api)
        if actual_len != expected_len:
            msg = ("Unexpected number of akeys after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        # 8. Stop the server to use ddb.
        dmg_command.system_stop()

        # 9. Call ddb rm to remove the dkey.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]")
        self.log.info("rm dkey stdout = %s", cmd_result[0]["stdout"])

        # 10. Restart the server to use the API.
        dmg_command.system_start()

        # 11. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        ioreqs[0].obj.open()

        # 12. Call list_dkey() in pydaos API to verify that the dkey was removed.
        dkeys_api = ioreqs[0].list_dkey()
        self.log.info("dkeys from API (after) = %s", dkeys_api)

        expected_len = len(dkeys_inserted) - 1
        actual_len = len(dkeys_api)
        if actual_len != expected_len:
            msg = ("Unexpected number of dkeys after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        # 13. Stop the server to use ddb.
        dmg_command.system_stop()

        # 14. Call ddb rm to remove the object.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]")
        self.log.info("rm object stdout = %s", cmd_result[0]["stdout"])

        # 15. Restart the server to use daos command.
        dmg_command.system_start()

        # 16. Reset the container and pool so that cleanup works.
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()

        # 17. Call "daos container list-objects <pool_uuid> <cont_uuid>" to verify that
        # the object was removed.
        list_obj_out = self.get_daos_command().container_list_objects(
            pool=self.pool.identifier, cont=self.container.uuid)
        obj_list = list_obj_out["response"]
        self.log.info("Object list (after) = %s", obj_list)

        expected_len = len(ioreqs) - 1
        if obj_list:
            actual_len = len(obj_list)
        else:
            actual_len = 0
        if actual_len != expected_len:
            msg = ("Unexpected number of objects after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_recovery_ddb_load(self):
        """Test ddb load.

        1. Create a pool and a container.
        2. Insert one object with one dkey with the API.
        3. Stop the server to use ddb.
        4. Find the vos file name. e.g., /mnt/daos/<pool_uuid>/vos-0.
        5. Load new data into [0]/[0]/[0]/[0]
        6. Restart the server.
        7. Reset the object, container, and pool to use the API.
        8. Verify the data in the akey with single_fetch().

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=ddb,test_recovery_ddb_load
        """
        # 1. Create a pool and a container.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # 2. Insert one object with one dkey with API.
        obj_dataset = insert_objects(
            context=self.context, container=self.container, object_count=1,
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
        vos_file = self.get_vos_file_path()
        host = NodeSet(self.hostlist_servers[0])
        ddb_command = DdbCommand(
            server_host=host, path=self.bin, mount_point="/mnt/daos",
            pool_uuid=self.pool.uuid, vos_file=vos_file)

        # 5. Load new data into [0]/[0]/[0]/[0]
        # Create a file in test node.
        load_file_path = os.path.join(self.test_dir, "new_data.txt")
        new_data = "New akey data 0123456789"
        with open(load_file_path, "w") as file:
            file.write(new_data)

        # Copy the created file to server node.
        try:
            distribute_files(
                hosts=host, source=load_file_path, destination=load_file_path,
                mkdir=False)
        except DaosTestError as error:
            raise CommandFailure(
                "ERROR: Copying new_data.txt to {}: {}".format(host, error)) from error

        # The file with the new data is ready. Run ddb load.
        ddb_command.load(component_path="[0]/[0]/[0]/[0]", load_file_path=load_file_path)

        # 6. Restart the server.
        dmg_command.system_start()

        # 7. Reset the object, container, and pool to use the API after server restart.
        ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        ioreqs[0].obj.open()

        # 8. Verify the data in the akey with single_fetch().
        data_size = len(new_data) + 1
        data = ioreqs[0].single_fetch(
            dkey=dkeys_inserted[0], akey=akeys_inserted[0], size=data_size)
        actual_data = data.value.decode('utf-8')
        self.log.info("data (after) = %s", actual_data)

        errors = []
        if new_data != actual_data:
            msg = "ddb load failed! Expected = {}; Actual = {}".format(
                new_data, actual_data)
            errors.append(msg)

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_recovery_ddb_dump_value(self):
        """Test ddb dump_value.

        1. Create a pool and a container.
        2. Insert one object with one dkey with API.
        3. Stop the server to use ddb.
        4. Find the vos file name. e.g., /mnt/daos/<pool_uuid>/vos-0.
        5. Dump the two akeys to files.
        6. Verify the content of the files.
        7. Restart the server for the cleanup.
        8. Reset the object, container, and pool to prepare for the cleanup.

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=recovery
        :avocado: tags=ddb,test_recovery_ddb_dump_value
        """
        # 1. Create a pool and a container.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # 2. Insert one object with one dkey with API.
        obj_dataset = insert_objects(
            context=self.context, container=self.container, object_count=1,
            dkey_count=1, akey_count=2, base_dkey=self.random_dkey,
            base_akey=self.random_akey, base_data=self.random_data)
        ioreqs = obj_dataset[0]
        data_list = obj_dataset[3]

        # 3. Stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 4. Find the vos file name.
        vos_file = self.get_vos_file_path()
        host = NodeSet(self.hostlist_servers[0])
        ddb_command = DdbCommand(
            server_host=host, path=self.bin, mount_point="/mnt/daos",
            pool_uuid=self.pool.uuid, vos_file=vos_file)

        # 5. Dump the two akeys to files.
        akey1_file_path = os.path.join(self.test_dir, "akey1.txt")
        ddb_command.dump_value(
            component_path="[0]/[0]/[0]/[0]", out_file_path=akey1_file_path)
        akey2_file_path = os.path.join(self.test_dir, "akey2.txt")
        ddb_command.dump_value(
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
        with open(akey1_file_path, "r") as file:
            actual_akey1_data = file.readlines()[0]
        actual_akey2_data = None
        with open(akey2_file_path, "r") as file:
            actual_akey2_data = file.readlines()[0]

        errors = []
        str_data_list = []
        # Convert the data to string.
        for data in data_list:
            str_data_list.append(data.value.decode("utf-8"))
        # Verify that we were able to obtain the data and akey1 and akey2 aren't the same.
        if actual_akey1_data is None or actual_akey2_data is None or \
                actual_akey1_data == actual_akey2_data:
            msg = ("Invalid dumped value! Dumped akey1 data = {}; "
                   "Dumped akey2 data = {}").format(actual_akey1_data, actual_akey2_data)
            errors.append(msg)
        # Verify that the data we obtained with ddb are the ones we wrote. The order isn't
        # deterministic, so check with "in".
        if actual_akey1_data not in str_data_list or \
                actual_akey2_data not in str_data_list:
            msg = ("Unexpected dumped value! Dumped akey data 1 = {}; "
                   "Dumped akey data 2 = {}; Expected data list = {}").format(
                       actual_akey1_data, actual_akey2_data, str_data_list)
            errors.append(msg)

        # 7. Restart the server for the cleanup.
        dmg_command.system_start()

        # 8. Reset the object, container, and pool to prepare for the cleanup.
        ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        ioreqs[0].obj.open()

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")
