# PyTorch module based on DAOS DFS


The module provides pytorch datasource primitive to access training data stored inside DAOS POSIX container.

### Implementation

The module is split into two parts: the interfacing part written in python and shim module for it written in C to interact with `libdfs`.

PyTorch integration requires to implement `torch.utils.data.Dataset` and `torch.utils.data.IterableDataset`.
To implement map style dataset only two methods are required: `__len__()` and `__getitem__()` and optional `__getitems__()`.

During dataset creation the connection to container will be established and its namespace will be scanned to build
a list of files in container with their size. The number of items in that list will be used to implement `__len__()` method.
`__getitem__()` implementation consists of looking up the object by its absolute path and reading its content into the buffer
created on the python side.

The `__getitems__()` method allows requesting multiple samples at once, making this a good case to use DAOS event queues to send and wait on batch items.

By default `Dataset` is single threaded (more like single process in python), `__getitem__()` and `__getitems__()` are regular blocking calls.
If multiprocessing is enabled, `Dataset` provides the `worker_init` method, which worker processes are calling upon their startup.
During this setup the global connection should be reused and the new event queue should be created for calling worker processes.

There's no internal multithreading inside the shim module - it's driven on outside by `torch.utils.DataLoader`.
If `DataLoader` is configured to have 8 readers then 8 event queues are going to be created per each worker process so the performance of individual worker should not be affected by others.


Implementation of `torch.utils.data.IterableDataset` requires to implement `__iter__()` protocol, which can be fully implemented on python side,
based on the building blocks from map style dataset.


### Requirements

Configured and running DAOS agent on the node(s) and correctly set ACLs - the user should have read access to the container.



### Example usage of Map style Dataset

```python
import numpy as np
import torch as t
from pydaos.torch import Dataset as DaosDataset
import matplotlib.pyplot as plt
from io import BytesIO

def transform(data):
    return np.load(BytesIO(data), allow_pickle=True)['x']

ds = DaosDataset(pool="torch", cont="my-dataset", transform_fn=transform)

print(f"Loaded dataset of {len(ds)} items")

figure = plt.figure(figsize=(8, 8))
cols, rows = 3, 3
for i in range(1, cols * rows + 1):
    idx = t.randint(len(ds), size=(1,)).item()
    img = ds[idx]
    figure.add_subplot(rows, cols, i)
    plt.title('sample #{}'.format(idx))
    plt.axis("off")
    plt.imshow(img.squeeze(), cmap="gray")
plt.show()
```


### Checkpoint interface

Torch framework provides a way to save and load model's checkpoints: `torch.save` and `torch.load` functions are used to save and load the model state dictionary.
The `torch.save` function expects a state dictionary object and a file like object `Union[str, PathLike, BinaryIO, IO[bytes]]`.
To implement such interface, `pydaos.torch.WriteBuffer` class is introduced, which is a wrapper around `io.BufferedIOBase` object, behaving like a writable stream.
`WriteBuffer` can operate in two modes: in-memory buffer and chunked buffer. In-memory buffer accumulates data in memory and writes it to the DAOS container when `close()` method is called.
Chunked buffer writes the data to the DAOS container in chunks of fixed size. There are optional parameters to limit number of chunks in-flight and number of worker processes to use.
Implementation of the loader is pretty straightforward - it reads the data from the file with existing API and returns it as a buffer.

For convenience, the `pydoas.torch.Checkpoint` class is provided that manages the DAOS connections and provides `reader` and `writer` methods.


Example of using the checkpointing interface in DLIO benchmark:

```python
import logging
import torch
from pydaos.torch import Checkpoint as DaosCheckpoint

from dlio_benchmark.checkpointing.base_checkpointing import BaseCheckpointing
from dlio_benchmark.utils.utility import Profile
from dlio_benchmark.utils.config import ConfigArguments

from dlio_benchmark.common.constants import MODULE_CHECKPOINT

dlp = Profile(MODULE_CHECKPOINT)


class PyDaosTorchCheckpointing(BaseCheckpointing):
    __instance = None

    @staticmethod
    def get_instance():
        """ Static access method. """
        if PyDaosTorchCheckpointing.__instance is None:
            logging.basicConfig(level=logging.INFO)
            PyDaosTorchCheckpointing.__instance = PyDaosTorchCheckpointing()
        return PyDaosTorchCheckpointing.__instance

    @dlp.log_init
    def __init__(self):
        super().__init__("pt")

        args = ConfigArguments.get_instance()
        prefix = args.checkpoint_folder
        pool = args.checkpoint_daos_pool
        cont = args.checkpoint_daos_cont

        logging.info(f"Checkpointing is set to DAOS pool: {pool}, container: {cont} with prefix: {prefix}")
        self.ckpt = DaosCheckpoint(pool, cont, prefix)

    @dlp.log
    def get_tensor(self, size):
        return torch.randint(high=1, size=(size,), dtype=torch.int8)

    @dlp.log
    def save_state(self, suffix, state):
        name = self.get_name(suffix)
        with self.ckpt.writer(name) as f:
            torch.save(state, f)

    @dlp.log
    def checkpoint(self, epoch, step_number):
        super().checkpoint(epoch, step_number)

    @dlp.log
    def finalize(self):
        super().finalize()
```
