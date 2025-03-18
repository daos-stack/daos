"""
  (C) Copyright 2025 Google LLC
  (C) Copyright 2025 Enakta Labs Ltd

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import hashlib

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from io_utilities import DirectoryTreeCommand
from pydaos.torch import Dataset, IterableDataset
from run_utils import run_remote
from torch.utils.data import DataLoader


class PytorchDatasetsTest(TestWithServers):
    """Test Pytorch Map Style Dataset.

    :avocado: recursive
    """

    def test_map_style_dataset(self):
        """Test Map Style Dataset directly without DataLoader

        Test Description: Ensure that the dataset can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchDatasetsTest,test_map_style_dataset
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
        file_max_size = self.params.get("file_max_size", "/run/map_style_dataset/*", 4096)
        readdir_batch_size = self.params.get("readdir_batch_size", "/run/map_style_dataset/*", 5)

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

        expected = self._get_test_files_hashmap(root_dir, self.hostlist_clients)

        dataset = Dataset(pool=pool.identifier,
                          cont=container.identifier,
                          readdir_batch_size=readdir_batch_size,
                          )

        actual = {}
        for _, content in enumerate(dataset):
            h = hashlib.md5(content).hexdigest()  # nosec
            if h not in actual:
                actual[h] = 1
            else:
                actual[h] += 1

        if actual != expected:
            self.fail("dataset did not fetch all samples")

    def test_iterable_dataset(self):
        """Test Iterable Dataset directly without DataLoader

        Test Description: Ensure that the dataset can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchDatasetsTest,test_iterable_dataset
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        height = self.params.get("tree_height", "/run/iterable_dataset/*")
        subdirs = self.params.get("subdirs", "/run/iterable_dataset/*")
        files_per_node = self.params.get("files_per_node", "/run/iterable_dataset/*")
        file_min_size = self.params.get("file_min_size", "/run/iterable_dataset/*", 4096)
        file_max_size = self.params.get("file_max_size", "/run/iterable_dataset/*", 128 * 1024)

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

        expected = self._get_test_files_hashmap(root_dir, self.hostlist_clients)

        dataset = IterableDataset(pool.identifier, container.identifier)

        actual = {}
        for _, content in enumerate(dataset):
            h = hashlib.md5(content).hexdigest()  # nosec
            if h not in actual:
                actual[h] = 1
            else:
                actual[h] += 1

        if actual != expected:
            self.fail("dataset did not fetch all samples")

    def test_map_dataset_with_dataloader(self):
        """Test Map Style Dataset with DataLoader.

        Test Description: Ensure that the DataLoader can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchDatasetsTest,test_map_dataset_with_dataloader
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        height = self.params.get("tree_height", "/run/map_dataset_with_dataloader/*")
        subdirs = self.params.get("subdirs", "/run/map_dataset_with_dataloader/*")
        files_per_node = self.params.get("files_per_node", "/run/map_dataset_with_dataloader/*")

        # DataLoader requires that samples are of the same size
        file_min_size = self.params.get("file_min_size", "/run/map_dataset_with_dataloader/*", 4096)
        file_max_size = self.params.get("file_max_size", "/run/map_dataset_with_dataloader/*", 4096)

        batch_sizes = self.params.get("batch_size", "/run/map_dataset_with_dataloader/*")
        processes = self.params.get("processes", "/run/map_dataset_with_dataloader/*")

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

        expected = self._get_test_files_hashmap(root_dir, self.hostlist_clients)

        dataset = Dataset(pool.identifier, container.identifier)
        for procs in processes:
            for batch_size in batch_sizes:
                self._test_dataloader(dataset, expected, batch_size, procs)

    def test_iterable_dataset_with_dataloader(self):
        """Test Iterable Dataset with DataLoader.

        Test Description: Ensure that the DataLoader can read all the samples that were seeded.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,pytorch
        :avocado: tags=PytorchDatasetsTest,test_iterable_dataset_with_dataloader
        """
        pool = self.get_pool()
        container = self.get_container(pool)
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)

        root_dir = dfuse.mount_dir.value

        height = self.params.get("tree_height", "/run/iterable_dataset_with_dataloader/*")
        subdirs = self.params.get("subdirs", "/run/iterable_dataset_with_dataloader/*")
        files_per_node = self.params.get(
            "files_per_node", "/run/iterable_dataset_with_dataloader/*")

        # DataLoader requires that samples are of the same size
        file_min_size = self.params.get(
            "file_min_size", "/run/iterable_dataset_with_dataloader/*", 4096)
        file_max_size = self.params.get(
            "file_max_size", "/run/iterable_dataset_with_dataloader/*", 4096)

        batch_sizes = self.params.get("batch_size", "/run/iterable_dataset_with_dataloader/*")
        processes = self.params.get("processes", "/run/iterable_dataset_with_dataloader/*")

        self._create_test_files(root_dir, height, subdirs, files_per_node,
                                file_min_size, file_max_size)

        expected = self._get_test_files_hashmap(root_dir, self.hostlist_clients)

        dataset = IterableDataset(pool.identifier, container.identifier)
        for procs in processes:
            for batch_size in batch_sizes:
                self._test_dataloader(dataset, expected, batch_size, procs)

    def _test_dataloader(self, dataset, expected, batch_size, processes):
        """With the given dataset and parameters load all samples using DataLoader
        and check if all expected samples are fetched"""

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
                h = hashlib.md5(content).hexdigest()  # nosec
                if h not in actual:
                    actual[h] = 1
                else:
                    actual[h] += 1

        if actual != expected:
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

    def _get_test_files_hashmap(self, root_dir, hostlist):
        """Map all files in the directory tree to their md5 hash"""

        cmd = f'find {root_dir} -type f -exec md5sum {{}} + '
        result = run_remote(self.log, hostlist, cmd)

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

        return hashes
