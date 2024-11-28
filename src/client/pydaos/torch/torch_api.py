#
# (C) Copyright 2024 Google LLC
# (C) Copyright 2024 Enakta Labs Ltd
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
"""
torch module provides implementation for PyTorch Map-Style and Iterable Style Datasets
to access training data on DAOS DFS via POSIX container.
"""

import concurrent
import math
import os
from concurrent.futures import FIRST_COMPLETED, ProcessPoolExecutor

from torch.utils.data import Dataset as TorchDataset
from torch.utils.data import IterableDataset as TorchIterableDataset
from torch.utils.data import get_worker_info

from . import DAOS_MAGIC, torch_shim

ITER_BATCH_SIZE = 32
READDIR_BATCH_SIZE = 128
PARALLEL_SCAN_WORKERS = 16


def transform_fn_default(data):
    """ Noop transformation function """
    return data


class Dataset(TorchDataset):
    """
    Class representing pytorch.Dataset over DAOS POSIX container.
    During the initialization it will scan namespace of the container and build
    a list of objects (samples of the dataset) available to read.

    The samples are accessed by index operator __getitem__ or its optimized version
    __getitems__ that accepts batch of indices and load them in parallel.

    If this Dataset is planned to be used via multiple workers in different processes,
    before accessing the data, workers needs to call worker_init function to re-initialize
    DAOS internals after fork(s).

    Attributes
    ----------
    pool : string
        Pool label or UUID string
    cont : string
        Container label or UUID string
    path : string (optional)
        Path inside the container pointing to dataset samples
    transform_fn : fn (optional)
        Function to transform samples from storage to in-memory representation
    readdir_batch_size: int (optional)
        Number of directory entries to read for each readdir call.


    Methods
    -------
    __len__():
        Returns number of samples Dataset loaded during its initialization.

    __getitem__(index):
        Returns sample by its index.

    __getitems__(indices):
        Returns batch of the items by their indices read in parallel.

    worker_init(worker_id):
        (Re)Initializes worker on the current running process to be able access DAOS Dataset,
        after the fork, which is default way for pytorch.DataLoader to run multiple workers.
        It is recommended to set it as a worker_init_fn parameter of pytorch.DataLoader class.
    """

    # pylint: disable=too-many-arguments
    def __init__(self, pool=None, cont=None, path=None,
                 transform_fn=transform_fn_default,
                 readdir_batch_size=READDIR_BATCH_SIZE):
        super().__init__()

        self._pool = pool
        self._cont = cont
        self._dfs = _Dfs(pool=pool, cont=cont)
        self._transform_fn = transform_fn
        self._readdir_batch_size = readdir_batch_size

        self.objects = self._dfs.parallel_list(path, readdir_batch_size=self._readdir_batch_size)

    def __len__(self):
        """ Returns number of items in this dataset """

        return len(self.objects)

    def __getitem__(self, idx):
        """ Read item by its index """

        obj = self.objects[idx]
        path, size = obj
        return self._transform_fn(self._dfs.read(path, size))

    def __getitems__(self, indices):
        """ Batch read of multiple items in parallel by their indices """

        items = [self.objects[idx] for idx in indices]
        result = self._dfs.batch_read(items)
        return [self._transform_fn(x) for x in result]

    def worker_init(self, worker_id):
        """ Re-initializes DAOS internals after fork """

        if worker_id is None or worker_id < 0:
            return

        # On the initial connect we made local2global() and now we should be able
        # to use that global connection and dfs
        self._dfs.worker_init()

    def __del__(self):
        """ Cleanups the used resources and connection """

        if self._dfs is None:
            return

        self._dfs.disconnect()
        self._dfs = None


# pylint: disable=abstract-method # iterable dataset should implement only __iter__()
class IterableDataset(TorchIterableDataset):
    """
    Class representing pytorch.IterableDataset over DAOS POSIX container.
    During the initialization it will scan namespace of the container and build
    a list of objects (samples of the dataset) available to read.

    The samples are accessed by iterator returned by __iter__ method.

    If this Dataset is planned to be used via multiple workers in different processes,
    before accessing the data, workers needs to call worker_init function to re-initialize
    DAOS internals after fork(s) and to split work between them.

    The typical usage with pytorch.DataLoader would look like the following example:

    import pydaos.torch
    from torch.utils.data import DataLoader

    ds = pydaos.torch.IterableDataset(Pool, Cont)
    dl = DataLoader(ds,
                    batch_size=16,
                    num_workers=4,
                    worker_init_fn=ds.worker_init,
    )

    for i, sample in enumerate(dl):
        print(f"Sample {i}: {sample}")


    Attributes
    ----------
    pool : string
        Pool label or UUID string
    cont : string
        Container label or UUID string
    path : string (optional)
        Path inside the container pointing to dataset samples
    transform_fn : fn (optional)
        Function to transform samples from storage to in-memory representation
    readdir_batch_size: int (optional)
        Number of directory entries to read for each readdir call.
    batch_size: int (optional)
        Number of samples to fetch per iteration.


    Methods
    -------
    __iter__(self):
        Returns an iterator over the dataset.

    worker_init(worker_id):
        (Re)Initializes worker on the current process and setup the working subset of the items
        to load for this worker.
    """

    # pylint: disable=too-many-arguments,too-many-instance-attributes
    def __init__(self, pool=None, cont=None, path=None,
                 transform_fn=transform_fn_default,
                 readdir_batch_size=READDIR_BATCH_SIZE,
                 batch_size=ITER_BATCH_SIZE):
        super().__init__()

        self._pool = pool
        self._cont = cont
        self._dfs = _Dfs(pool=pool, cont=cont)
        self._transform_fn = transform_fn
        self._readdir_batch_size = readdir_batch_size
        self._batch_size = batch_size

        self.objects = self._dfs.parallel_list(path, readdir_batch_size=self._readdir_batch_size)
        self.workset = self.objects

    def __iter__(self):
        """
        Returns the iterator over items.
        It implements lazy batching and returns iterator via yield from construction, so
        there's no need to implement Iterator Protocol.
        """

        batches = (self.workset[i:i + self._batch_size]
                   for i in range(0, len(self.workset), self._batch_size))
        for batch in batches:
            yield from self.__load_batch(batch)

    def worker_init(self, worker_id):
        """
        Re-initializes DAOS internals after fork and split the work between the workers.
        Each of workers receive its share of items to load in self.workset for __iter__ method.
        """

        if worker_id is None or worker_id < 0:
            # Single process should use the full set of objects
            return

        # Split work over workers
        # See https://pytorch.org/docs/stable/data.html#torch.utils.data.IterableDataset
        worker_info = get_worker_info()
        per_worker = int(math.ceil(len(self.objects) / float(worker_info.num_workers)))

        start = worker_id * per_worker
        end = min(start + per_worker, len(self.objects))

        self.workset = self.objects[start:end]

        # On the initial connect we made local2global() and now we should be able
        # to use that global connection and dfs
        self._dfs.worker_init()

    def __del__(self):
        """ Cleanups the used resources and connection """

        if self._dfs is None:
            return

        self._dfs.disconnect()
        self._dfs = None
        self.objects = None
        self.workset = None

    def __load_batch(self, items):
        """ load items in batch and applies data transformation function """

        result = self._dfs.batch_read(items)
        return [self._transform_fn(x) for x in result]


class _Dfs():
    """
    Class encapsulating libdfs interface to load PyTorch Dataset
    Should not be used directly.
    """

    def __init__(self, pool=None, cont=None, rd_only=True):
        if (pool is None or cont is None):
            raise ValueError("invalid pool or container labels")

        (ret, dfs) = torch_shim.torch_connect(DAOS_MAGIC, pool, cont, rd_only)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), f"could not connect to {pool}:{cont}")

        self._dfs = dfs

    def disconnect(self):
        """ disconnects from the container and frees resources """

        if self._dfs is None:
            return

        ret = torch_shim.torch_disconnect(DAOS_MAGIC, self._dfs)
        if ret != 0:
            raise OSError(ret, os.strerror(ret))
        self._dfs = None

    def worker_fn(self, work, readdir_batch_size=READDIR_BATCH_SIZE):
        """
        Reads the directory with indexed anchor.
        Returns separate lists for files and directories, ready to be consumed by other workers.
        """

        (path, index) = work

        dirs = []
        files = []
        ret = torch_shim.torch_list_with_anchor(DAOS_MAGIC, self._dfs,
                                                path, index, files, dirs, readdir_batch_size
                                                )
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)

        dirs = [chunk for d in dirs for chunk in self.split_dir_for_parallel_scan(
            os.path.join(path, d))
        ]

        files = [(os.path.join(path, fname), size) for (fname, size) in files]
        return files, dirs

    def split_dir_for_parallel_scan(self, path):
        """
        Splits dir for parallel readdir.
        It returns list of tuples (dirname, anchor index) to be consumed by worker function
        """

        ret = torch_shim.torch_recommended_dir_split(DAOS_MAGIC, self._dfs, path)
        if ret < 0:
            raise OSError(-ret, os.strerror(-ret), path)

        return [(path, idx) for idx in range(0, ret)]

    def parallel_list(self, path=None,
                      readdir_batch_size=READDIR_BATCH_SIZE,
                      workers=PARALLEL_SCAN_WORKERS):
        """
        Parallel list tries to leverage DAOS ability to read dir in parallel
        by splitting across multiple engines.

        To fully use this feature the container should be configured with directory object classes
        supporting this mode, e.g. OC_SX.
        """
        if path is None:
            path = os.sep

        if not path.startswith(os.sep):
            raise ValueError("relative path is unacceptable")

        result = []
        inprogress = set()
        dirs = self.split_dir_for_parallel_scan(path)
        with ProcessPoolExecutor(max_workers=workers, initializer=self.worker_init) as pool:
            while True:
                batch = dirs[:workers]
                dirs = dirs[len(batch):]

                futures = [pool.submit(self.worker_fn, dir, readdir_batch_size) for dir in batch]

                inprogress.update(futures)
                (complete, incomplete) = concurrent.futures.wait(
                    inprogress, return_when=FIRST_COMPLETED)

                for fut in complete:
                    files, to_process = fut.result()
                    dirs.extend(to_process)
                    result.extend(files)

                inprogress = incomplete
                if len(dirs) == 0 and len(inprogress) == 0:
                    break

        return result

    def read(self, path, size):
        """ This is specialized version of file read, when the file size is known in advance. """

        buf = bytearray(size)
        ret = torch_shim.torch_read(DAOS_MAGIC, self._dfs, path, buf)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)

        return buf

    def batch_read(self, items):
        """ parallel read of multiple files """

        to_read = [(item[0], bytearray(item[1])) for item in items]
        ret = torch_shim.torch_batch_read(DAOS_MAGIC, self._dfs, to_read)

        if ret != 0:
            raise OSError(ret, os.strerror(ret))

        return [item[1] for item in to_read]

    def worker_init(self):
        """ Tries to reinitialize DAOS DFS for the current process after fork """

        ret = torch_shim.torch_worker_init(DAOS_MAGIC, self._dfs)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), "could not re-initialize DAOS for worker")
