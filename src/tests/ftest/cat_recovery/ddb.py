"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import ctypes
import re
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithServers
from pydaos.raw import IORequest, DaosObjClass
from general_utils import create_string_buffer, report_errors, run_pcmd
from ddb_utils import DdbCommand

SAMPLE_DKEY = "Sample dkey"
SAMPLE_AKEY = "Sample akey"
SAMPLE_DATA = "Sample data"


class DdbTest(TestWithServers):
    """Test ddb subcommands.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DdbTest object."""
        super().__init__(*args, **kwargs)
        self.ioreqs = []
        self.dkeys = []
        self.akeys = []
        self.data_list = []

    def insert_objects(self, object_count, dkey_count, akey_count):
        """Insert objects, dkeys, akeys, and data into the container.

        Inserted objects: self.ioreqs
        Inserted dkeys: self.dkeys
        Inserted akeys: self.akeys
        Inserted data: self.data_list

        Args:
            object_count (int): Number of objects to insert.
            dkey_count (int): Number of dkeys to insert.
            akey_count (int): Number of akeys to insert.
        """
        self.container.open()

        for obj_index in range(object_count):
            # Insert object.
            self.ioreqs.append(IORequest(
                context=self.context, container=self.container.container, obj=None,
                objtype=DaosObjClass.OC_S1))

            for dkey_index in range(dkey_count):
                # Prepare the dkey to insert into the object.
                dkey_str = " ".join(
                    [SAMPLE_DKEY, str(obj_index), str(dkey_index)]).encode("utf-8")
                self.dkeys.append(
                    create_string_buffer(value=dkey_str, size=len(dkey_str)))

                for akey_index in range(akey_count):
                    # Prepare the akey to insert into the dkey.
                    akey_str = " ".join(
                        [SAMPLE_AKEY, str(obj_index), str(dkey_index),
                         str(akey_index)]).encode("utf-8")
                    self.akeys.append(
                        create_string_buffer(value=akey_str, size=len(akey_str)))

                    # Prepare the data to insert into the akey.
                    data_str = " ".join(
                        [SAMPLE_DATA, str(obj_index), str(dkey_index),
                         str(akey_index)]).encode("utf-8")
                    self.data_list.append(
                        create_string_buffer(value=data_str, size=len(data_str)))
                    c_size = ctypes.c_size_t(ctypes.sizeof(self.data_list[-1]))

                    # Insert dkeys, akeys, and the data.
                    self.ioreqs[-1].single_insert(
                        dkey=self.dkeys[-1], akey=self.akeys[-1],
                        value=self.data_list[-1], size=c_size)

    def test_ls(self):
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
        :avocado: tags=cat_rec
        :avocado: tags=ddb,ddb_ls
        """
        # Create a pool and a container.
        self.add_pool()
        self.add_container(pool=self.pool)

        ddb_command = DdbCommand(
            path=self.bin, mount_point="/mnt/daos", pool_uuid=self.pool.uuid,
            vos_file="vos-0")

        errors = []

        # Insert objects with API.
        object_count = self.params.get("object_count", "/run/*")
        dkey_count = self.params.get("dkey_count", "/run/*")
        akey_count = self.params.get("akey_count", "/run/*")
        self.insert_objects(
            object_count=object_count, dkey_count=dkey_count, akey_count=akey_count)

        # Need to stop the server to use ddb.
        self.get_dmg_command().system_stop()

        # 1. Verify container UUID.
        cmd_result = ddb_command.list_component()
        # Sample output.
        # [0] d4e0c836-17bd-4df3-b255-929732486bab
        match = re.findall(
            r"(\[\d+\])\s+([a-f0-9\-]+)", cmd_result.stdout.decode("utf-8"))
        self.log.info("List container match = %s", match)

        actual_uuid = match[0][1].lower()
        expected_uuid = self.container.uuid.lower()
        if actual_uuid != expected_uuid:
            msg = "Unexpected container UUID! Expected = {}; Actual = {}".format(
                expected_uuid, actual_uuid)
            errors.append(msg)

        # 2. Verify object count in the container.
        cmd_result = ddb_command.list_component(component_path="[0]")
        self.log.debug("## List objects = %s", cmd_result.stdout.decode("utf-8"))
        # Sample output.
        # /d4e0c836-17bd-4df3-b255-929732486bab/
        # [0] '281479271677953.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [1] '281479271677954.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [2] '281479271677955.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [3] '281479271677956.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        # [4] '281479271677957.0' (type: DAOS_OT_MULTI_HASHED, groups: 1)
        match = re.findall(
            r"(\[\d+\])\s+\'(\d+\.\d+)\'", cmd_result.stdout.decode("utf-8"))
        self.log.info("List objects match = %s", match)

        actual_object_count = len(match)
        expected_object_count = self.params.get("object_count", "/run/*")
        if actual_object_count != expected_object_count:
            msg = "Unexpected object count! Expected = {}; Actual = {}".format(
                expected_object_count, actual_object_count)
            errors.append(msg)

        # 3. Verify there are two dkeys for every object. Also verify the dkey string and
        # the size.
        dkey_akey_regex = r"(\[\d+\])\s+\'(.+)\'\s+\((\d+)\)"
        actual_dkey_count = 0
        for obj_index in range(object_count):
            component_path = "[0]/[{}]".format(obj_index)
            cmd_result = ddb_command.list_component(component_path=component_path)
            self.log.debug("## List dkeys %d = %s", obj_index, cmd_result.stdout)
            # Sample output.
            # /d4e0c836-17bd-4df3-b255-929732486bab/281479271677953.0.0/
            # [0] 'Sample dkey 0 0' (15)
            # [1] 'Sample dkey 0 1' (15)
            match = re.findall(dkey_akey_regex, cmd_result.stdout.decode("utf-8"))

            actual_dkey_count += len(match)

            # Verify dkey string.
            actual_dkey_1 = " ".join(match[0][1].split())
            actual_dkey_2 = " ".join(match[1][1].split())
            # We're not testing the numbers in the string because it's not deterministic.
            if SAMPLE_DKEY not in actual_dkey_1:
                msg = ("Unexpected dkey! obj_i = {}. Expected = {}; "
                       "Actual = {}").format(obj_index, SAMPLE_DKEY, actual_dkey_1)
            if SAMPLE_DKEY not in actual_dkey_2:
                msg = ("Unexpected dkey! obj_i = {}. Expected = {}; "
                       "Actual = {}").format(obj_index, SAMPLE_DKEY, actual_dkey_2)

            # Verify the dkey size field.
            for dkey in match:
                dkey_string = dkey[1]
                dkey_size = int(dkey[2])
                if len(dkey_string) != dkey_size:
                    msg = ("Wrong dkey size! obj_index = {}. String = {}; "
                           "Size = {}").format(obj_index, dkey_string, dkey_size)
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
                msg = "List akeys obj_index = {}, dkey_index = {}, stdout = {}".format(
                    obj_index, dkey_index, cmd_result.stdout)
                self.log.info(msg)
                # Output is in the same format as dkey, so use the same regex.
                # /d4e0c836-17bd-4df3-b255-929732486bab/281479271677954.0.0/'
                # Sample dkey 1 0'/
                # [0] 'Sample akey 1 0 0' (17)
                match = re.findall(dkey_akey_regex, cmd_result.stdout.decode("utf-8"))

                akey_count += len(match)

                # Verify akey string. As in dkey, ignore the numbers at the end.
                actual_akey = " ".join(match[0][1].split())
                if SAMPLE_AKEY not in actual_akey:
                    msg = ("Unexpected akey! obj_index = {}; dkey_index = {}; "
                           "Expected = {}; Actual = {}").format(
                               obj_index, dkey_index, SAMPLE_AKEY, actual_akey)
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
                path_stat = ("akey data obj_index = {}, dkey_index = {}, "
                             "stdout = {}").format(
                                 obj_index, dkey_index, cmd_result.stdout)
                self.log.info(path_stat)
                # Sample output.
                # [0] Single Value (Length: 17 bytes)
                match = re.findall(
                    r"Length:\s+(\d+)\s+bytes", cmd_result.stdout.decode("utf-8"))
                data_length = int(match[0])
                expected_data_length = len(SAMPLE_DATA) + 6
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

    def test_rm(self):
        """Test rm.

        1. Create a pool and a container. Insert objects, dkeys, and akeys.
        2. Stop the server to use ddb.
        3. Call ddb rm to remove the akey.
        4. Restart the server to use the API.
        5. Reset the object, container, and pool to use the API after server restart.
        6. Call list_akey() in pydaos API to verify that the akey was removed.
        7. Stop the server to use ddb.
        8. Call ddb rm to remove the dkey.
        9. Restart the server to use the API.
        10. Reset the object, container, and pool to use the API after server restart.
        11. Call list_dkey() in pydaos API to verify that the dkey was removed.
        12. Stop the server to use ddb.
        13. Call ddb rm to remove the object.
        14. Restart the server to use daos command.
        15. Reset the container and pool so that cleanup works.
        16. Call "daos container list-objects <pool_uuid> <cont_uuid>" to verify that the
        object was removed.

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=cat_rec
        :avocado: tags=ddb,ddb_rm
        """
        # 1. Create a pool and a container. Insert objects, dkeys, and akeys.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # Insert one object with one dkey with API.
        self.insert_objects(object_count=1, dkey_count=1, akey_count=2)

        # For debugging/reference, check that the dkey and the akey we just inserted are
        # returned from the API.
        akeys = self.ioreqs[0].list_akey(dkey=self.dkeys[0])
        self.log.info("akeys from API (before) = %s", akeys)
        dkeys = self.ioreqs[0].list_dkey()
        self.log.info("dkeys from API (before) = %s", dkeys)

        # For debugging/reference, check that the object was inserted using daos command.
        obj_list = self.get_daos_command().container_list_objects(
            pool=self.pool.uuid, cont=self.container.uuid)
        self.log.info("Object list (before) = %s", obj_list.stdout.decode("utf-8"))

        # 2. Need to stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 3. Call ddb rm to remove the akey.
        ddb_command = DdbCommand(
            path=self.bin, mount_point="/mnt/daos", pool_uuid=self.pool.uuid,
            vos_file="vos-0")
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]/[0]")
        self.log.info("rm akey stdout = %s", cmd_result.stdout)

        # 4. Restart the server to use the API.
        dmg_command.system_start()

        # 5. Reset the object, container, and pool to use the API after server restart.
        self.ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        self.ioreqs[0].obj.open()

        # 6. Call list_akey() in pydaos API to verify that the akey was removed.
        akeys = self.ioreqs[0].list_akey(dkey=self.dkeys[0])
        self.log.info("akeys from API (after) = %s", akeys)

        errors = []
        expected_len = len(self.akeys) - 1
        actual_len = len(akeys)
        if actual_len != expected_len:
            msg = ("Unexpected number of akeys after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        # 7. Stop the server to use ddb.
        dmg_command.system_stop()

        # 8. Call ddb rm to remove the dkey.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]/[0]")
        self.log.info("rm dkey stdout = %s", cmd_result.stdout)

        # 9. Restart the server to use the API.
        dmg_command.system_start()

        # 10. Reset the object, container, and pool to use the API after server restart.
        self.ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        self.ioreqs[0].obj.open()

        # 11. Call list_dkey() in pydaos API to verify that the dkey was removed.
        dkeys = self.ioreqs[0].list_dkey()
        self.log.info("dkeys from API (after) = %s", dkeys)

        expected_len = len(self.dkeys) - 1
        actual_len = len(dkeys)
        if actual_len != expected_len:
            msg = ("Unexpected number of dkeys after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        # 12. Stop the server to use ddb.
        dmg_command.system_stop()

        # 13. Call ddb rm to remove the object.
        cmd_result = ddb_command.remove_component(component_path="[0]/[0]")
        self.log.info("rm object stdout = %s", cmd_result.stdout)

        # 14. Restart the server to use daos command.
        dmg_command.system_start()

        # 15. Reset the container and pool so that cleanup works.
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()

        # 16. Call "daos container list-objects <pool_uuid> <cont_uuid>" to verify that
        # the object was removed.
        obj_list = self.get_daos_command().container_list_objects(
            pool=self.pool.uuid, cont=self.container.uuid)
        self.log.info("Object list (after) = %s", obj_list.stdout.decode("utf-8"))

        expected_len = len(self.ioreqs) - 1
        actual_len = len(obj_list)
        if actual_len != expected_len:
            msg = ("Unexpected number of objects after ddb rm! "
                   "Expected = {}; Actual = {}").format(expected_len, actual_len)
            errors.append(msg)

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")

    def test_load(self):
        """Test ddb load.

        1. Create a pool and a container.
        2. Insert one object with one dkey with API.
        3. Stop the server to use ddb.
        4. Find the vos file name. e.g., vos-0.
        5. Load new data into [0]/[0]/[0]/[0]
        6. Restart the server.
        7. Reset the object, container, and pool to use the API.
        8. Verify the data in the akey with single_fetch().

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=cat_rec
        :avocado: tags=ddb,ddb_load
        """
        # 1. Create a pool and a container.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # 2. Insert one object with one dkey with API.
        self.insert_objects(object_count=1, dkey_count=1, akey_count=1)

        # For debugging/reference, call single_fetch and get the data just inserted.
        data_size = len(self.data_list[0])
        # Pass in data_size + 1 to single_fetch to avoid the no-space error.
        data_size += 1
        data = self.ioreqs[0].single_fetch(
            dkey=self.dkeys[0], akey=self.akeys[0], size=data_size)
        self.log.info("data (before) = %s", data.value.decode('utf-8'))

        # 3. Stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 4. Find the vos file name.
        hosts = NodeSet(self.hostlist_servers[0])
        vos_path = os.path.join("/mnt/daos", self.pool.uuid.lower())
        command = " ".join(["sudo", "ls", vos_path])
        cmd_out = run_pcmd(hosts=hosts, command=command)
        self.log.debug("## sudo ls /mnt/daos/<uuid> output = %s", cmd_out[0]["stdout"])

        vos_file = None
        for file in cmd_out[0]["stdout"]:
            if "vos" in file:
                vos_file = file
                break
        if not vos_file:
            self.fail(
                "vos file wasn't found in /mnt/daos/{}".format(self.pool.uuid.lower()))
        else:
            self.log.debug("## vos_file: %s", vos_file)
        ddb_command = DdbCommand(
            path=self.bin, mount_point="/mnt/daos", pool_uuid=self.pool.uuid,
            vos_file=vos_file)

        # 5. Load new data into [0]/[0]/[0]/[0]
        load_file_path = os.path.join(self.test_dir, "new_data.txt")
        new_data = "New akey data 0123456789"
        with open(load_file_path, "w") as file:
            file.write(new_data)

        ddb_command.load(component_path="[0]/[0]/[0]/[0]", load_file_path=load_file_path)

        # 6. Restart the server.
        dmg_command.system_start()

        # 7. Reset the object, container, and pool to use the API after server restart.
        self.ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        self.ioreqs[0].obj.open()

        # 8. Verify the data in the akey with single_fetch().
        data_size = len(new_data)
        data_size += 1
        data = self.ioreqs[0].single_fetch(
            dkey=self.dkeys[0], akey=self.akeys[0], size=data_size)
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

    def test_dump_value(self):
        """Test ddb dump_value.

        1. Create a pool and a container.
        2. Insert one object with one dkey with API.
        3. Stop the server to use ddb.
        4. Dump the two akeys to files.
        5. Verify the content of the files.
        6. Restart the server for the cleanup.
        7. Reset the object, container, and pool to prepare for the cleanup.

        :avocado: tags=all,weekly_regression
        :avocado: tags=vm
        :avocado: tags=cat_rec
        :avocado: tags=ddb,ddb_dump_value
        """
        # 1. Create a pool and a container.
        self.add_pool(connect=True)
        self.add_container(pool=self.pool)

        # 2. Insert one object with one dkey with API.
        self.insert_objects(object_count=1, dkey_count=1, akey_count=2)

        # 3. Stop the server to use ddb.
        dmg_command = self.get_dmg_command()
        dmg_command.system_stop()

        # 4. Dump the two akeys to files.
        ddb_command = DdbCommand(
            path=self.bin, mount_point="/mnt/daos", pool_uuid=self.pool.uuid,
            vos_file="vos-0")

        akey1_file_path = os.path.join(self.test_dir, "akey1.txt")
        ddb_command.dump_value(
            component_path="[0]/[0]/[0]/[0]", out_file_path=akey1_file_path)
        akey2_file_path = os.path.join(self.test_dir, "akey2.txt")
        ddb_command.dump_value(
            component_path="[0]/[0]/[0]/[1]", out_file_path=akey2_file_path)

        # 5. Verify the content of the files.
        actual_akey1_data = None
        with open(akey1_file_path, "r") as file:
            actual_akey1_data = file.readlines()[0]
        actual_akey2_data = None
        with open(akey2_file_path, "r") as file:
            actual_akey2_data = file.readlines()[0]

        errors = []
        str_data_list = []
        # Convert the data to string.
        for data in self.data_list:
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

        # 6. Restart the server for the cleanup.
        dmg_command.system_start()

        # 7. Reset the object, container, and pool to prepare for the cleanup.
        self.ioreqs[0].obj.close()
        self.container.close()
        self.pool.disconnect()
        self.pool.connect()
        self.container.open()
        self.ioreqs[0].obj.open()

        self.log.info("##### Errors #####")
        report_errors(test=self, errors=errors)
        self.log.info("##################")
