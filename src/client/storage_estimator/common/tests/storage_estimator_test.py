'''
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
'''
import pytest
import unittest
import yaml
import os

from storage_estimator.vos_structures import VosObject, AKey, DKey, Container, Containers, VosValue, Overhead, ValType, KeyType, VosValueError
from storage_estimator.explorer import FileSystemExplorer
from storage_estimator.util import ObjectClass
from storage_estimator.parse_csv import ProcessCSV
from .util import FileGenerator


class MockArgs(object):
    def __init__(self, file_oclass="SX", csv_file=""):
        self.dir_oclass = "S1"
        self.file_oclass = file_oclass
        self.verbose = True
        self.csv = [csv_file]
        self.alloc_overhead = 16
        self.file_name_size = 32
        self.num_shards = 1000
        self.meta = ""
        self.scm_cutoff = ""
        self.checksum = ""


class VosValueTestCase(unittest.TestCase):
    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(ValueError) as err:
            value = VosValue()
        assert "size parameter is required" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            value = VosValue(size="rubbish")
        assert "size parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            value = VosValue(size=5, count="rubbish")
        assert "count parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            value = VosValue(
                size=5, count=10, aligned="rubbish")
        assert "aligned parameter must be of type" in str(err.value) # nosec

    @pytest.mark.ut
    def test_default_parameters(self):
        value = VosValue(size=10)
        want = {"size": 10, "count": 1, "aligned": "Yes"}
        got = value.dump()
        assert want == got # nosec

    @pytest.mark.ut
    def test_constructor(self):
        value = VosValue(size=10, aligned="Yes")
        want = {"size": 10, "count": 1, "aligned": "Yes"}
        got = value.dump()
        assert want == got # nosec

        value = VosValue(size=10, aligned="No")
        want = {"size": 10, "count": 1, "aligned": "No"}
        got = value.dump()
        assert want == got # nosec

        value = VosValue(size=10, count=20, aligned="No")
        want = {"size": 10, "count": 20, "aligned": "No"}
        got = value.dump()
        assert want == got # nosec


@pytest.fixture(scope="class")
def vos_test_data(request):
    class VosTestData:
        def create_values(self):
            value1 = VosValue(size=10, aligned="Yes")
            value2 = VosValue(size=20, aligned="No")

            return [value1, value2]

        def create_default_akey(
                self,
                key="A-key 1",
                count=1,
                key_type="hashed",
                value_type="single_value",
                overhead="user"):
            values = self.create_values()
            raw_values = self._dump_values(values)

            if key_type is None:
                key_type = "hashed"

            akey = {
                "count": count,
                "type": key_type,
                "overhead": overhead,
                "value_type": value_type,
                "values": raw_values}

            if key_type == "hashed":
                key_size = len(key.encode("utf-8"))
                akey["size"] = key_size

            return akey

        def _dump_values(self, values):
            raw_values = list()
            for value in values:
                raw_values.append(value.dump())

            return raw_values

        def create_akeys(self):
            akey1 = self.create_default_akey(
                key="A-key 1", value_type="single_value")
            akey2 = self.create_default_akey(key="A-key 2", value_type="array")

            return [akey1, akey2]

        def create_default_dkey(
                self,
                key="D-key 1",
                count=1,
                key_type="hashed",
                overhead="user"):
            akeys = self.create_akeys()
            dkey = {
                "count": count,
                "type": key_type,
                "overhead": overhead,
                "akeys": akeys}

            if key_type == "hashed":
                key_size = len(key.encode("utf-8"))
                dkey["size"] = key_size

            return dkey

        def create_default_object(self, key="Object 1", count=1):
            dkey1 = self.create_default_dkey(key="D-key 1")
            dkey2 = self.create_default_dkey(key="D-key 2")

            vos_object = {
                "targets": 0,
                "count": count,
                "dkeys": [
                    dkey1,
                    dkey2]}

            return vos_object

        def create_default_container(
                self, count=1, csum_size=0, csum_gran=16384):
            self.raw_obj1 = self.create_default_object(count=100)
            self.raw_obj2 = self.create_default_object(count=200)
            vos_container = {
                "count": count,
                "csum_size": csum_size,
                "csum_gran": csum_gran,
                "objects": [
                    self.raw_obj1,
                    self.raw_obj2]}

            return vos_container

        def _create_sb_akey(self, key, size):
            value = VosValue(size=size)
            akey = AKey(
                key=key,
                overhead=Overhead.META,
                value_type=ValType.SINGLE)
            akey.add_value(value)
            return akey

        def get_mock_dfs_superblock_obj(self):
            dkey_sb = DKey(
                key="DFS_SB_METADATA",
                overhead=Overhead.META)
            dkey_sb.add_value(self._create_sb_akey(key="DFS_MAGIC", size=8))
            dkey_sb.add_value(
                self._create_sb_akey(
                    key="DFS_SB_VERSION", size=2))
            dkey_sb.add_value(
                self._create_sb_akey(
                    key="DFS_LAYOUT_VERSION",
                    size=2))
            dkey_sb.add_value(
                self._create_sb_akey(
                    key="DFS_CHUNK_SIZE", size=8))
            dkey_sb.add_value(
                self._create_sb_akey(
                    key="DFS_OBJ_CLASS", size=2))

            inode_value = VosValue(size=64)
            akey_inode = AKey(
                key="DFS_INODE",
                overhead=Overhead.META,
                value_type=ValType.ARRAY)
            akey_inode.add_value(inode_value)
            dkey_root = DKey(key="/")
            dkey_root.add_value(akey_inode)

            superblock_obj = VosObject()
            superblock_obj.add_value(dkey_sb)
            superblock_obj.add_value(dkey_root)

            return superblock_obj

        def process_stats(self, container):
            stats = {
                "objects": 0,
                "dkeys": 0,
                "akeys": 0,
                "values": 0,
                "dkey_size": 0,
                "akey_size": 0,
                "value_size": 0}

            for object in container["objects"]:
                obj_count = object.get("count", 11)
                if obj_count == 0:
                    continue

                stats["objects"] += obj_count

                for dkey in object["dkeys"]:
                    dkey_count = dkey.get("count", 1)
                    if dkey_count == 0:
                        continue

                    total_dkeys = obj_count * dkey_count
                    stats["dkey_size"] += dkey.get("size", 0) * total_dkeys
                    stats["dkeys"] += obj_count * dkey_count

                    for akey in dkey["akeys"]:
                        akey_count = akey.get("count", 1)
                        if akey_count == 0:
                            continue

                        total_akeys = obj_count * dkey_count * akey_count
                        stats["akey_size"] += akey.get("size", 0) * total_akeys
                        stats["akeys"] += total_akeys

                        for value in akey["values"]:
                            value_count = value.get("count", 1)
                            if value_count == 0:
                                continue

                            total_values = obj_count * dkey_count * akey_count * value_count
                            stats["value_size"] += value.get(
                                "size", 0) * total_values
                            stats["values"] += total_akeys

            return stats

    request.cls.test_data = VosTestData()


@pytest.mark.usefixtures("vos_test_data")
class AKeyTestCase(unittest.TestCase):
    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(ValueError) as err:
            akey = AKey()
        assert "value_type parameter is required" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(value_type="rubbish")
        assert "value_type parameter must be of type" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(
                value_type="single_value", count="rubbish")
        assert "count parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(
                value_type="single_value", key_type="rubbish")
        assert "key_type parameter must be of type" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(
                value_type="single_value", overhead="rubbish")
        assert "overhead parameter must be of type" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(
                value_type="single_value", values=["rubbish"])
        assert "must be of type" in str(err.value) # nosec

    @pytest.mark.ut
    def test_constructor(self):
        values = self.test_data.create_values()

        akey = AKey(
            key="A-key 1",
            value_type="single_value",
            values=values)
        want = self.test_data.create_default_akey()
        assert want == akey.dump() # nosec

        akey = AKey(
            key="A-key 1",
            key_type="hashed",
            value_type="single_value",
            values=values)
        want = self.test_data.create_default_akey(
            key="A-key 1", key_type="hashed", value_type="single_value")
        assert want == akey.dump() # nosec

        akey = AKey(
            key_type="integer",
            value_type="single_value",
            values=values)
        want = self.test_data.create_default_akey(
            key_type="integer", value_type="single_value")
        assert want == akey.dump() # nosec

        akey = AKey(
            key_type="integer",
            value_type="single_value",
            count=20,
            values=values)
        want = self.test_data.create_default_akey(
            key_type="integer", value_type="single_value", count=20)
        assert want == akey.dump() # nosec

        akey = AKey(
            key_type="integer",
            value_type="single_value",
            overhead="user",
            values=values)
        want = self.test_data.create_default_akey(
            key_type="integer", value_type="single_value", overhead="user")
        assert want == akey.dump() # nosec

        akey = AKey(
            key_type="integer",
            value_type="single_value",
            overhead="meta",
            values=values)
        want = self.test_data.create_default_akey(
            key_type="integer", value_type="single_value", overhead="meta")
        assert want == akey.dump() # nosec

        akey = AKey(
            key="A-key 1",
            value_type="array",
            overhead="user",
            values=values)
        want = self.test_data.create_default_akey(
            key="A-key 1", value_type="array", overhead="user")
        assert want == akey.dump() # nosec

        akey = AKey(
            key="A-key 1",
            value_type="array",
            overhead="meta",
            values=values)
        want = self.test_data.create_default_akey(
            key="A-key 1", value_type="array", overhead="meta")
        assert want == akey.dump() # nosec

        akey = AKey(
            key="A-key 1",
            value_type="array",
            values=values)
        want = self.test_data.create_default_akey(
            key="A-key 1", key_type=None, value_type="array")
        assert want == akey.dump() # nosec

    @pytest.mark.ut
    def test_add_value(self):
        with pytest.raises(VosValueError) as err:
            akey = AKey(
                key="A-key 1", value_type="single_value")
            akey.dump()
        assert "list of values must not be empty" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = AKey(
                key="A-key 1", value_type="single_value")
            akey.add_value("rubbish")
        assert "must be of type" in str(err.value) # nosec

        akey = AKey(key="A-key 1", value_type="single_value")
        value = VosValue(size=10, aligned="Yes")
        akey.add_value(value)
        value = VosValue(size=20, aligned="No")
        akey.add_value(value)
        want = self.test_data.create_default_akey()
        assert want == akey.dump() # nosec


@pytest.mark.usefixtures("vos_test_data")
class DKeyTestCase(unittest.TestCase):
    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(TypeError) as err:
            akey = DKey(count="rubbish")
        assert "count parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = DKey(key_type="rubbish")
        assert "key_type parameter must be of type" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = DKey(overhead="rubbish")
        assert "overhead parameter must be of type" in str(err.value) # nosec

        with pytest.raises(VosValueError) as err:
            dkey = DKey()
            akey = dkey.dump()
        assert "list of akeys must not be empty" in str(err.value) # nosec

    @pytest.mark.ut
    def test_constructor(self):
        akey1 = AKey(
            key="A-key 1",
            value_type="single_value",
            values=self.test_data.create_values())
        akey2 = AKey(
            key="A-key 2",
            value_type="array",
            values=self.test_data.create_values())

        dkey = DKey(key="D-key 1", akeys=[akey1, akey2])
        want = self.test_data.create_default_dkey()

        assert want == dkey.dump() # nosec

    @pytest.mark.ut
    def test_add_value(self):
        with pytest.raises(VosValueError) as err:
            dkey = DKey(key="D-key 1")
            dkey.dump()
        assert "list of akeys must not be empty" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            dkey = DKey(key="D-key 1")
            dkey.add_value("rubbish")
        assert "must be of type" in str(err.value) # nosec

        dkey = DKey(key="D-key 1")
        akey = AKey(
            key="A-key 1",
            value_type="single_value",
            values=self.test_data.create_values())
        dkey.add_value(akey)
        akey = AKey(
            key="A-key 2",
            value_type="array",
            values=self.test_data.create_values())
        dkey.add_value(akey)

        want = self.test_data.create_default_dkey()

        assert want == dkey.dump() # nosec


@pytest.mark.usefixtures("vos_test_data")
class ObjectTestCase(unittest.TestCase):
    def setUp(self):
        akey1 = AKey(
            key="A-key 1",
            value_type="single_value",
            values=self.test_data.create_values())
        akey2 = AKey(
            key="A-key 2",
            value_type="array",
            values=self.test_data.create_values())
        self.dkey1 = DKey(key="D-key 1", akeys=[akey1, akey2])
        self.dkey2 = DKey(key="D-key 2", akeys=[akey1, akey2])

    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(TypeError) as err:
            akey = VosObject(count="rubbish")
        assert "count parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            akey = VosObject(count=10, dkeys=["rubbish"])
        assert "must be of type" in str(err.value) # nosec

    @pytest.mark.ut
    def test_constructor(self):
        vos_object = VosObject(
            count=100, dkeys=[self.dkey1, self.dkey2])
        want = self.test_data.create_default_object(count=100)
        assert want == vos_object.dump() # nosec

    @pytest.mark.ut
    def test_add_value(self):
        with pytest.raises(VosValueError) as err:
            dkey = VosObject()
            dkey.dump()
        assert "list of dkeys must not be empty" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            dkey = VosObject()
            dkey.add_value("rubbish")
        assert "must be of type" in str(err.value) # nosec

        vos_object = VosObject(count=200)
        vos_object.add_value(self.dkey2)
        vos_object.add_value(self.dkey1)
        want = self.test_data.create_default_object(count=200)
        assert want == vos_object.dump() # nosec


@pytest.mark.usefixtures("vos_test_data")
class ContainerTestCase(unittest.TestCase):
    def setUp(self):
        akey1 = AKey(
            key="A-key 1",
            value_type="single_value",
            values=self.test_data.create_values())
        akey2 = AKey(
            key="A-key 2",
            value_type="array",
            values=self.test_data.create_values())
        self.dkey1 = DKey(key="D-key 1", akeys=[akey1, akey2])
        self.dkey2 = DKey(key="D-key 2", akeys=[akey1, akey2])

        self.vos_object1 = VosObject(
            count=100, dkeys=[self.dkey1, self.dkey2])
        self.vos_object2 = VosObject(
            count=200, dkeys=[self.dkey1, self.dkey2])

    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(TypeError) as err:
            container = Container(count="rubbish")
        assert "count parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            container = Container(csum_size="rubbish")
        # pylint: disable=line-too-long
        assert "csum_size parameter must be of type int" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            container = Container(csum_gran="rubbish")
        assert "csum_gran parameter must be of type int" in str(err.value) # nosec
        # pylint: enable=line-too-long

        with pytest.raises(TypeError) as err:
            container = Container(objects=["rubbish"])
        assert "must be of type" in str(err.value) # nosec

    @pytest.mark.ut
    def test_constructor(self):
        container = Container(
            objects=[self.vos_object1, self.vos_object2])
        want = self.test_data.create_default_container()
        assert want == container.dump() # nosec

        container = Container(
            count=300, csum_size=400, csum_gran=500, objects=[
                self.vos_object1, self.vos_object2])
        want = self.test_data.create_default_container(
            count=300, csum_size=400, csum_gran=500)
        assert want == container.dump() # nosec

    @pytest.mark.ut
    def test_add_value(self):
        container = Container()
        container.add_value(self.vos_object1)
        container.add_value(self.vos_object2)
        want = self.test_data.create_default_container()
        assert want == container.dump() # nosec


@pytest.mark.usefixtures("vos_test_data")
class ContainersTestCase(unittest.TestCase):
    def setUp(self):
        akey1 = AKey(
            key="A-key 1",
            value_type="single_value",
            values=self.test_data.create_values())
        akey2 = AKey(
            key="A-key 2",
            value_type="array",
            values=self.test_data.create_values())
        self.dkey1 = DKey(key="D-key 1", akeys=[akey1, akey2])
        self.dkey2 = DKey(key="D-key 2", akeys=[akey1, akey2])

        self.vos_object1 = VosObject(
            count=100, dkeys=[self.dkey1, self.dkey2])
        self.vos_object2 = VosObject(
            count=200, dkeys=[self.dkey1, self.dkey2])

        self.vos_container1 = Container(
            csum_gran=300, objects=[self.vos_object1, self.vos_object2])
        self.vos_container2 = Container(
            csum_gran=400, objects=[self.vos_object1, self.vos_object2])

    @pytest.mark.ut
    def test_invalid_parameters(self):
        with pytest.raises(VosValueError) as err:
            containers = Containers()
            containers.dump()
        assert "list of containers must not be empty" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            container = Containers(num_shards="rubbish")
        # pylint: disable=line-too-long
        assert "num_shards parameter must be of type int" in str(err.value) # nosec
        # pylint: enable=line-too-long

        with pytest.raises(TypeError) as err:
            container = Containers(containers=["rubbish"])
        assert "must be of type" in str(err.value) # nosec

    @pytest.mark.ut
    def test_constructor(self):
        containers = Containers(
            num_shards=200, containers=[
                self.vos_container1, self.vos_container2])

        raw_container1 = self.test_data.create_default_container(csum_gran=300)
        raw_container2 = self.test_data.create_default_container(csum_gran=400)

        want = {
            "num_shards": 200,
            "containers": [
                raw_container1,
                raw_container2]}
        assert want == containers.dump() # nosec

    @pytest.mark.ut
    def test_add_value(self):
        with pytest.raises(TypeError) as err:
            containers = Containers()
            containers.add_value("rubbish")
        assert "must be of type" in str(err.value) # nosec

        with pytest.raises(TypeError) as err:
            containers = Containers()
            containers.set_num_shards("rubbish")
        # pylint: disable=line-too-long
        assert "num_shards parameter must be of type int" in str(err.value) # nosec
        # pylint: enable=line-too-long

        containers = Containers()
        containers.add_value(self.vos_container1)
        containers.add_value(self.vos_container2)

        containers.set_num_shards(500)

        raw_container1 = self.test_data.create_default_container(csum_gran=300)
        raw_container2 = self.test_data.create_default_container(csum_gran=400)
        want = {
            "num_shards": 500,
            "containers": [
                raw_container1,
                raw_container2]}
        assert want == containers.dump() # nosec


@pytest.mark.usefixtures("vos_test_data")
class FSTestCase(unittest.TestCase):
    def setUp(self):
        test_files = [{"type": "file",
                       "path": "data/deploy/driver.bin",
                       "size": 5767168},
                      {"type": "symlink",
                       "path": "data/deploy/my_file",
                       "dest": "../../specs/very_importan_file.txt"},
                      {"type": "file",
                       "path": "data/secret_plan.txt",
                       "size": 3670016},
                      {"type": "file",
                       "path": "specs/readme.txt",
                       "size": 1572864},
                      {"type": "file",
                       "path": "specs/very_importan_file.txt",
                       "size": 2621440}]
        self.fg = FileGenerator()
        self.fg.crete_mock_fs(test_files)
        self.root_dir = self.fg.get_root()
        current_dir = os.path.dirname(__file__)
        self.test_files = os.path.join(current_dir, "test_files")

    def _create_inode_akey(self, key, size):
        value = VosValue(size=size)
        akey = AKey(key=key,
                    overhead=Overhead.META,
                    value_type=ValType.ARRAY)
        akey.add_value(value)

        return akey

    def _create_dfs_for_explorer(self, args, reference_file):
        oclass = ObjectClass(args)
        fse = FileSystemExplorer(self.root_dir, oclass)
        akey = self._create_inode_akey("DFS_INODE", 64)
        fse.set_dfs_inode(akey)
        fse.set_io_size(131072)
        fse.set_chunk_size(1048576)
        fse.explore()
        dfs = fse.get_dfs()
        container = dfs.get_container()
        dfs_superblock_obj = self.test_data.get_mock_dfs_superblock_obj()
        container.add_value(dfs_superblock_obj)

        test_file = os.path.join(self.test_files, reference_file)
        reference = yaml.safe_load(open(test_file, "r"))
        got = self.test_data.process_stats(container.dump())

        gold_container = {}

        for container in reference.get("containers"):
            gold_container.update(container)

        want = self.test_data.process_stats(gold_container)

        assert got == want # nosec

    @pytest.mark.sx
    def test_create_dfs_sx(self):
        args = MockArgs("SX")
        self._create_dfs_for_explorer(args, "test_data_sx.yaml")

    @pytest.mark.rp3gx
    def test_create_dfs_3gx(self):
        args = MockArgs("RP_3GX")
        self._create_dfs_for_explorer(args, "test_data_3gx.yaml")

    @pytest.mark.ec16p2
    def test_create_dfs_16p2gx(self):
        args = MockArgs("EC_16P2GX")
        self._create_dfs_for_explorer(args, "test_data_16p2gx.yaml")


@pytest.mark.usefixtures("vos_test_data")
class CSVTestCase(unittest.TestCase):
    def setUp(self):
        current_dir = os.path.dirname(__file__)
        self.test_files = os.path.join(current_dir, "test_files")

    def _create_dfs_for_read_csv(self, args, reference_file):
        csv = ProcessCSV(args)
        fse = csv._ingest_csv()
        dfs = fse.get_dfs()
        container = dfs.get_container()
        dfs_superblock_obj = self.test_data.get_mock_dfs_superblock_obj()
        container.add_value(dfs_superblock_obj)

        test_file = os.path.join(self.test_files, reference_file)
        reference = yaml.safe_load(open(test_file, "r"))
        got = self.test_data.process_stats(container.dump())

        gold_container = {}

        for container in reference.get("containers"):
            gold_container.update(container)

        want = self.test_data.process_stats(gold_container)

        assert got == want # nosec

    @pytest.mark.sx
    def test_create_dfs_csv_sx(self):
        test_file = os.path.join(self.test_files, "test_data.csv")
        args = MockArgs("SX", test_file)
        self._create_dfs_for_read_csv(args, "test_data_big_sx.yaml")

    @pytest.mark.rp3gx
    def test_create_dfs_csv_3gx(self):
        test_file = os.path.join(self.test_files, "test_data.csv")
        args = MockArgs("RP_3GX", test_file)
        self._create_dfs_for_read_csv(args, "test_data_big_3gx.yaml")

    @pytest.mark.ec16p2
    def test_create_dfs_csv_16p2gx(self):
        test_file = os.path.join(self.test_files, "test_data.csv")
        args = MockArgs("EC_16P2GX", test_file)
        self._create_dfs_for_read_csv(args, "test_data_big_16p2gx.yaml")


if __name__ == "__main__":
    unittest.main()
