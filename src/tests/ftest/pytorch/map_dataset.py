"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import hashlib
import string
import uuid

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from exception_utils import CommandFailure
from general_utils import get_random_string, report_errors
from dfuse_utils import get_dfuse, start_dfuse
from io_utilities import DirectoryTreeCommand
from run_utils import run_remote


class PytorchMapStyleDatasetTest(TestWithServers):
    """Test container create and destroy operations with labels.

    :avocado: recursive
    """

    def test_pytorch_map_dataset_readback_all(self):
        """Test Map Style Dataset

        Test Description: creates bunch of sample files and try to read them all back

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pytorch
        :avocado: tags=PytorchMapStyleDatasetTest,test_pytorch_map_dataset_readback_all
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        #TODO: read these from the test params
        self._create_test_files(root_dir, 3, 3, 3)

        cmd = f'find {root_dir} -type f -exec md5sum {{}} + '
        result = run_remote(self.log, self.hostlist_clients, cmd)

        if not result.passed:
            self.fail(f'"{cmd}" failed on {result.failed_hosts}')

        hashes = {}
        for line in result.output[0].stdout:
            parts = line.split()
            if len(parts) != 2:
                self.fail(f'unexpected result from md5sum: {line}')
            h = parts[0]
            if h not in hashes:
                hashes[h] = 1
            else:
                hashes[h] += 1


        # TODO: When module is imported, there is no connection yet to the agent
        from pydaos.torch import Dataset
        dataset = Dataset(pool.identifier, container.identifier)

        actual = {}
        for _, content in enumerate(dataset):
            h = hashlib.md5(content).hexdigest()
            if h not in actual:
                actual[h] = 1
            else:
                actual[h] += 1

        if hashes != actual:
            self.fail("dataset did not fetch all samples")


    def _create_test_files(self, path, height, subdirs, files_per_node, min_size=4096, max_size=128*1024):
        """Create a directory tree"""
        dir_tree = DirectoryTreeCommand(self.hostlist_clients)
        dir_tree.path.value = path
        dir_tree.height.value = height
        dir_tree.subdirs.value = subdirs
        dir_tree.files.value = files_per_node
        dir_tree.prefix.value = "samples"
        dir_tree.file_size_min.value = min_size
        dir_tree.file_size_max.value = max_size

        self.log.info("Populating: %s", path)
        result = dir_tree.run()
        if not result.passed:
            self.fail(
                f"Error running '{dir_tree.command}' for '{path}' on {result.failed_hosts}")

