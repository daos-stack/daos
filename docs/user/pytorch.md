# DAOS pytorch interface

PyTorch is fully featured framework for building deep learning models and training them.
It is widely used in the research community and in the industry.
PyTorch allows loading data from various sources and DAOS can be used as a storage backend for training data and models' checkpoints.

[DFS plugin](https://github.com/daos-stack/daos/tree/master/src/client/pydaos/torch) implements PyTorch interfaces for loading data from DAOS: Map and Iterable style datasets.
This allows to use all features of `torch.utils.data.DataLoader` to load data from DAOS POSIX containers, including parallel data loading, batching, shuffling, etc.

## Installation

To install the plugin, you need to have PyTorch installed. Please follow the official [PyTorch installation guide](https://pytorch.org/get-started/).
`pydoas.torch` module comes with DAOS client package. Please refer to DAOS installation guide for your distribution.


## Usage

To use DAOS as a storage backend for PyTorch, you need to have DAOS agent running on the nodes where PyTorch is running and correctly configured ACLs for the container.

Here's an example of how to use Map-style dataset with DAOS directly:

```python
import torch
from torch.utils.data import DataLoader
from pydaos.torch import Dataset

dataset = Dataset(pool='pool', container='container', path='/training/samples')
# That's it, when the Dataset is created, it will connect to DAOS, scan the namaspace of the container
# and will be ready to load data from it.

for i, sample in enumerate(dataset):
    print(f"Sample {i} size: {len(sample)}")
```

To use Dataset with DataLoader, you can pass it directly to DataLoader constructor:

```python

dataloader = DataLoader(dataset,
                        batch_size=4,
                        shuffle=True,
                        num_workers=4,
                        worker_init_fn=dataset.worker_init)

# and use DataLoader as usual
for batch in dataloader:
    print(f"Batch size: {len(batch)}")
```

The only notable difference is that you need to set `worker_init_fn` method of the dataset to correctly initialize the DAOS connection in the worker processes.

## Checkpoints

DAOS can be used to store model checkpoints as well.
PyTorch provides a way to save and load model checkpoints using [torch.save](https://pytorch.org/docs/main/generated/torch.save.html) and [torch.load](https://pytorch.org/docs/main/generated/torch.load.html) functions

`pydaos.torch` provides a way to save and load model checkpoints directly to/from DAOS container (could be same or different container than the one used for data).:

```python
import torch
from pydaos.torch import Checkpoint

# ...

chkp = Checkpoint(pool, cont, prefix='/training/checkpoints')

with chkp.writer('model.pt') as w:
    torch.save(model.state_dict(), w)

# Later, to load the model

with chkp.reader('model.pt') as r:
    torch.load(r)

```

See [pydaos.torch](https://github.com/daos-stack/daos/blob/master/src/client/pydaos/torch/Readme.md) plugin for an example of how to use checkpoints with DLIO benchmark
