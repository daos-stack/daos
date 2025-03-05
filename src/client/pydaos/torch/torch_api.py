#
# (C) Copyright 2024-2025 Google LLC
# (C) Copyright 2024-2025 Enakta Labs Ltd
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
"""
torch module provides implementation for PyTorch Map-Style and Iterable Style Datasets
to access training data on DAOS DFS via POSIX container.

In addition, it provides Checkpoint class to save and load PyTorch model checkpoints.
"""

import io
import math
import os
import stat
from multiprocessing import Process, Queue

from torch.utils.data import Dataset as TorchDataset
from torch.utils.data import IterableDataset as TorchIterableDataset
from torch.utils.data import get_worker_info

from . import DAOS_MAGIC, DaosClient, torch_shim

ITER_BATCH_SIZE = 32
READDIR_BATCH_SIZE = 128
PARALLEL_SCAN_WORKERS = 16
DIR_CACHE_SIZE = 64 * 1024
DEFAULT_CHUNK_SIZE = 64 * 1024 * 1024
DEFAULT_CHUNKS_LIMIT = 1024 // DEFAULT_CHUNK_SIZE


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
    dir_cache_size: int (optional)
        Number of directory object entries to cache in memory.


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
                 readdir_batch_size=READDIR_BATCH_SIZE,
                 dir_cache_size=DIR_CACHE_SIZE):
        super().__init__()

        self._pool = pool
        self._cont = cont
        self._dfs = _Dfs(pool=pool, cont=cont, dir_cache_size=dir_cache_size)
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
    dir_cache_size: int (optional)
        Number of directory object entries to cache in memory.


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
                 batch_size=ITER_BATCH_SIZE,
                 dir_cache_size=DIR_CACHE_SIZE):
        super().__init__()

        self._pool = pool
        self._cont = cont
        self._dfs = _Dfs(pool=pool, cont=cont, dir_cache_size=dir_cache_size)
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


class WriteBuffer(io.BufferedIOBase):
    """
    Class representing stream like write buffer for saving PyTorch model checkpoints to DAOS DFS.

    It provides two ways of writing data:

    In-memory buffer: all data will be written to the buffer
    and flushed to the storage on close() call. To use this mode set transfer_chunk_size to 0.

    Chunked write: data will be written in chunks and saved to the storage in parallel,
    using multiple workers. To use this mode set transfer_chunk_size to non-zero value.

    chunks_limit parameter is used to limit memory usage (only in chunked write mode):
    no more than chunks_limit chunks will be queued for writing to the storage.

    This class is not intended to be used directly: Checkpoint class is the main interface.
    """

    # pylint: disable=too-many-arguments,too-many-instance-attributes
    def __init__(self, dfs, path, mode, open_flags, class_name,
                 file_chunk_size, transfer_chunk_size, chunks_limit, workers):
        super().__init__()

        self._dfs = dfs
        self._path = path
        self._buffer = bytearray()
        # offset is used to track the offset in the file for chunked writes
        self._offset = 0
        # position is used to track how much was written in the buffer
        self._position = 0
        self._closed = False
        self._mode = mode
        self._oflags = open_flags
        self._class_name = class_name
        self._file_chunk_size = file_chunk_size
        self._transfer_chunk_size = transfer_chunk_size

        self._workers = []
        if self._transfer_chunk_size > 0:
            if chunks_limit == 0:
                self._queue = Queue()
            else:
                self._queue = Queue(chunks_limit)

            for _ in range(workers):
                worker = Process(target=self._worker_fn, args=(self._queue,))
                worker.start()
                self._workers.append(worker)

    def _worker_fn(self, queue):
        self._dfs.worker_init()
        while True:
            work = queue.get()
            if work is None:
                break

            (offset, chunk) = work
            self._dfs.write(self._path, self._mode, self._oflags,
                            self._class_name, self._file_chunk_size, offset, chunk)

    def write(self, data):
        """ Writes data to the buffer."""

        if self.closed:
            raise ValueError("I/O operation on closed file")

        # In case of no chunking, we just extend the existing buffer without any limits
        if self._transfer_chunk_size == 0:
            self._buffer.extend(data)
            self._position += len(data)
            return len(data)

        # Creating memoryview to avoid copying the data on chunking
        data = memoryview(data)
        written = len(data)
        while len(data) > 0:
            fit = min(len(data), self._transfer_chunk_size - len(self._buffer))
            chunk = data[:fit]
            self._buffer.extend(chunk)
            self._position += len(chunk)

            if len(self._buffer) == self._transfer_chunk_size:
                self._submit_chunk(self._offset, self._buffer)
                self._offset += len(self._buffer)
                self._buffer = bytearray()

            data = data[len(chunk):]

        return written

    def tell(self):
        """Return current stream position."""
        if self.closed:
            raise ValueError("I/O operation on closed file")
        return self._position

    def close(self):
        """Upload any data left in buffer to storage and close."""
        if self.closed:
            return

        self._flush()
        self._closed = True

        for _ in self._workers:
            self._queue.put(None)
        for worker in self._workers:
            worker.join()

        super().close()

    def _flush(self):
        """Write if anything left and wait for any outstanding transfers"""
        if self.closed:
            raise ValueError("I/O operation on closed file")

        if len(self._buffer) > 0 and self._transfer_chunk_size == 0:
            self._dfs.write(self._path, self._mode, self._oflags,
                            self._class_name, self._file_chunk_size, 0, self._buffer)
            return

        if len(self._buffer) > 0:
            self._submit_chunk(self._offset, self._buffer)
            self._offset += len(self._buffer)

    def _submit_chunk(self, offset, chunk):
        """ Submits chunk for writing to the container.

        This is blocking method, if the queue is bounded (via chunks_limit parameter) and full,
        forcing the caller to wait until some of the chunks are written to the storage.
        """

        self._queue.put((offset, chunk))

    @property
    def closed(self):
        """Return True if the file is closed."""
        return self._closed

    def writable(self):
        """Return True if the file is writable."""
        return True

    def readable(self):
        return False

    def seekable(self):
        """Return True if the file is seekable."""
        return False


class Checkpoint():
    """
    Class representing checkpoint interface for pytorch to save and load
    model's stave over DAOS DFS.

    Attributes
    ----------
    pool : string
        Pool label or UUID string
    cont: string
        Container label or UUID string
    prefix : string (optional)
        Prefix as a directory to store checkpoint files, default is root of the container.
    mode : int (optional)
        File mode to be used for checkpoint files, default is 0o744.
    open_flags : int (optional)
        Open flags to be used for checkpoint files, default is to create a file.
    class_name : string (optional)
        Object class name to be used for checkpoint files, default is OC_UNKNOWN.
    file_chunk_size : int (optional)
        Chunk size to be used for checkpoint files, default is 0.
    transfer_chunk_size : int (optional)
        Chunk size for data buffering/transfer, default is DEFAULT_CHUNK_SIZE = 64MB.
        To disable chunking set it to 0, then all writes go to in memory buffer
        and actual flush to storage will happen on close() call.
    chunks_limits: int (optional)
        Number of chunks to be used for buffering and transfer.
        Setting it to 0 means no limit.
        It's used only when transfer_chunk_size is set to non-zero value and provides the mechanism
        to limit memory usage.
    workers: int (optional)
        Number of workers to be used for parallel chunked writes.
        This parameter is used only when transfer_chunk_size is set to non-zero value.

    Methods
    -------
    reader(file, stream=None):
        Reads the checkpoint file and returns its content as BytesIO object.
        Optionally, the stream can be provided to read the data into it.

    writer(file):
        Returns write buffer to save the checkpoint file.
    """

    # pylint: disable=too-many-arguments,too-many-instance-attributes
    def __init__(self, pool, cont, prefix=os.sep,
                 mode=stat.S_IFREG | stat.S_IRWXU | stat.S_IRGRP | stat.S_IROTH,
                 open_flags=os.O_CREAT | os.O_RDWR,
                 class_name="OC_UNKNOWN",
                 file_chunk_size=0,
                 transfer_chunk_size=DEFAULT_CHUNK_SIZE,
                 chunks_limit=DEFAULT_CHUNKS_LIMIT,
                 workers=4,
                 ):
        self._pool = pool
        self._cont = cont
        self._prefix = prefix
        self._mode = mode
        self._oflags = open_flags
        self._class_name = class_name
        self._file_chunk_size = file_chunk_size
        self._transfer_chunk_size = transfer_chunk_size
        self._chunks_limit = chunks_limit
        self._workers = workers
        self._dfs = _Dfs(pool=pool, cont=cont, rd_only=False)

    def __del__(self):
        """ Cleanups the used resources and connection """

        if self._dfs is None:
            return
        self._dfs.disconnect()
        self._dfs = None

    def reader(self, file, stream=None):
        """
        Reads the checkpoint file and returns its content as BytesIO object.
        Alternatively, the stream can be provided to read the data into it, this might
        be useful for large checkpoints that can't fit into memory.
        """

        if file is None:
            raise ValueError("file is required")

        if stream is None:
            stream = io.BytesIO()

        path = os.path.join(self._prefix, file)
        size = self._dfs.get_file_size(path)

        chunks_limit = self._chunks_limit
        if chunks_limit == 0:
            chunks_limit = size // DEFAULT_CHUNK_SIZE

        # in case the file is smaller than the default chunk size
        if chunks_limit == 0:
            chunks_limit = 1

        chunk_size = self._transfer_chunk_size
        if chunk_size == 0:
            # In case we don't have chunking, we read the file in one go
            chunk_size = size
            chunks_limit = 1

        chunks = [(path, min(chunk_size, size - offset), offset)
                  for offset in range(0, size, chunk_size)]

        for i in range(0, len(chunks), chunks_limit):
            batch = chunks[i:i + chunks_limit]
            for data in self._dfs.batch_read(batch):
                stream.write(data)

        stream.seek(0)
        return stream

    def writer(self, file):
        """ Returns write buffer to save the checkpoint file """

        if file is None:
            raise ValueError("file is required")

        path = os.path.join(self._prefix, file)
        return WriteBuffer(self._dfs, path, self._mode, self._oflags,
                           self._class_name, self._file_chunk_size, self._transfer_chunk_size,
                           self._chunks_limit, self._workers)


class _Dfs():
    """
    Class encapsulating libdfs interface to load PyTorch Dataset
    Should not be used directly.
    """

    def __init__(self, pool=None, cont=None, rd_only=True, dir_cache_size=DIR_CACHE_SIZE):
        if pool is None:
            raise ValueError("pool label or UUID is required")
        if cont is None:
            raise ValueError("container label or UUID is required")

        self._dc = DaosClient()
        (ret, dfs) = torch_shim.torch_connect(DAOS_MAGIC, pool, cont, rd_only, dir_cache_size)
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

    def list_worker_fn(self, in_work, out_dirs, out_files, readdir_batch_size=READDIR_BATCH_SIZE):
        """
        Worker function to scan directory in parallel.
        It expects to receive tuples (path, index) to scan the directory with an anchor index,
        from the `in_work` queue.
        It should emit tuples (scanned, to_scan) to the `out_dirs` queue, where `scanned` is the
        number of scanned directories and `to_scan` is the list of directories to scan in parallel.
        Upon completion it should emit the list of files in the `out_files` queue.
        """

        self.worker_init()

        result = []
        while True:
            work = in_work.get()
            if work is None:
                break

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
            # Even if there are no dirs, we should emit the tuple to notify the main process
            out_dirs.put((1, dirs))

            files = [(os.path.join(path, file), size) for (file, size) in files]
            result.extend(files)

        out_files.put(result)

    def split_dir_for_parallel_scan(self, path):
        """
        Splits dir for parallel readdir.
        It returns list of tuples (dirname, anchor index) to be consumed by worker function
        """

        ret, splits = torch_shim.torch_recommended_dir_split(DAOS_MAGIC, self._dfs, path)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)

        return [(path, idx) for idx in range(0, splits)]

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

        procs = []
        work = Queue()
        dirs = Queue()
        files = Queue()
        for _ in range(workers):
            worker = Process(target=self.list_worker_fn, args=(
                work, dirs, files, readdir_batch_size))
            worker.start()
            procs.append(worker)

        queued = 0
        processed = 0
        for anchored_dir in self.split_dir_for_parallel_scan(path):
            work.put(anchored_dir)
            queued += 1

        while processed < queued:
            (scanned, to_scan) = dirs.get()
            processed += scanned
            for d in to_scan:
                work.put(d)
                queued += 1

        result = []
        for _ in range(workers):
            work.put(None)
            result.extend(files.get())

        for worker in procs:
            worker.join()

        return result

    def read(self, path, size):
        """ This is specialized version of file read, when the file size is known in advance. """

        buf = bytearray(size)
        ret = torch_shim.torch_read(DAOS_MAGIC, self._dfs, path, buf)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)

        return buf

    # pylint: disable=too-many-arguments
    def write(self, path, mode, open_flags, class_name, chunk_size, offset, data):
        """ Writes data to the file """

        ret = torch_shim.torch_write(DAOS_MAGIC, self._dfs, path, mode,
                                     open_flags, class_name, chunk_size, offset, data)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)

    def batch_read(self, items):
        """
        Parallel read of multiple files with their sizes and optional offsets.
        It expects list of tuples (path, size, [offset]) and returns list of buffers
        with data read from the storage.
        The result list is in the same order as the input list.
        """

        to_read = [(item[0], bytearray(item[1]), item[2] if len(item) > 2 else 0) for item in items]
        ret = torch_shim.torch_batch_read(DAOS_MAGIC, self._dfs, to_read)

        if ret != 0:
            raise OSError(ret, os.strerror(ret))

        return [item[1] for item in to_read]

    def worker_init(self):
        """ Tries to reinitialize DAOS DFS for the current process after fork """

        ret = torch_shim.torch_worker_init(DAOS_MAGIC, self._dfs)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), "could not re-initialize DAOS for worker")

    def get_file_size(self, path):
        """ Returns file size by its path """

        ret, size = torch_shim.torch_get_fsize(DAOS_MAGIC, self._dfs, path)
        if ret != 0:
            raise OSError(ret, os.strerror(ret), path)
        return size
