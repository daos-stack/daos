"""
  (C) Copyright 2025 Intel Corporation.
  (C) Copyright 2025 Google LLC

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import hashlib

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from io_utilities import DirectoryTreeCommand
from pydaos.torch import Dataset
from run_utils import run_remote
from torch.utils.data import DataLoader


class PytorchMapStyleDatasetTest(TestWithServers):
    """Test Pytorch Map Style Dataset.

    :avocado: recursive
    """

    def test_map_style_dataset(self):
        """Test Map Style Dataset directly without DataLoader

        Test Description: Ensure that the dataset can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchMapStyleDatasetTest,test_map_style_dataset
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        height = self.params.get("tree_height", "/run/map_style_dataset/*")
        subdirs = self.params.get("subdirs", "/run/map_style_dataset/*")
        files_per_node = self.params.get("files_per_node", "/run/map_style_dataset/*")
        file_min_size = self.params.get("file_min_size", "/run/map_style_dataset/*", 4096)
        file_max_size = self.params.get("file_max_size", "/run/map_style_dataset/*", 128 * 1024)

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

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

    def test_dataloader(self):
        """Test Map Style Dataset with DataLoader.

        Test Description: Ensure that the DataLoader can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchMapStyleDatasetTest,test_dataloader
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        height = self.params.get("tree_height", "/run/dataloader/*")
        subdirs = self.params.get("subdirs", "/run/dataloader/*")
        files_per_node = self.params.get("files_per_node", "/run/dataloader/*")

        # DataLoader requires that samples are of the same size
        file_min_size = self.params.get("file_min_size", "/run/dataloader/*", 4096)
        file_max_size = self.params.get("file_max_size", "/run/dataloader/*", 4096)

        batch_sizes = self.params.get("batch_size", "/run/dataloader/*")
        processes = self.params.get("processes", "/run/dataloader/*")

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

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

        for procs in processes:
            for batch_size in batch_sizes:
                self._test_dataloader(pool, container, hashes, batch_size, procs)

    def _test_dataloader(self, pool, container, hashes, batch_size, processes):
        dataset = Dataset(pool.identifier, container.identifier)
        loader = DataLoader(dataset,
                            batch_size=batch_size,
                            num_workers=processes,
                            # no collation, otherwise tensors are returned
                            collate_fn=lambda x: x,
                            worker_init_fn=dataset.worker_init,
                            drop_last=False)

        actual = {}
        for batch in loader:
            for content in batch:
                h = hashlib.md5(content).hexdigest()
                if h not in actual:
                    actual[h] = 1
                else:
                    actual[h] += 1

        if hashes != actual:
            self.fail(
                f"DataLoader with nproc={processes} and bs={batch_size} did not fetch all samples")

    def _create_test_files(self, path, height, subdirs, files_per_node, min_size, max_size):
        """Create a directory tree"""
        dir_tree = DirectoryTreeCommand(self.hostlist_clients)
        dir_tree.path.value = path
        dir_tree.height.value = height
        dir_tree.subdirs.value = subdirs
        dir_tree.files.value = files_per_node
        dir_tree.prefix.value = "samples"
        dir_tree.needles.value = 0
        dir_tree.file_size_min.value = min_size
        dir_tree.file_size_max.value = max_size

        self.log.info("Populating: %s", path)
        result = dir_tree.run()
        if not result.passed:
            self.fail(
                f"Error running '{dir_tree.command}' for '{path}' on {result.failed_hosts}")
