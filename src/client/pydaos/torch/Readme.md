# PyTorch module based on DAOS DFS


The module provides pytorch datasource primitive to access training data stored inside DAOS POSIX container.


### Requirements

Configured and running DAOS agent on the node(s) and correctly set ACLs - the user should have read access to the container.



### Example of usage Map style Dataset

```python
import numpy as np
import torch as t
from pydaos.torch import Dataset as DaosDataset
import matplotlib.pyplot as plt
from io import BytesIO

def transform(data):
    return np.load(BytesIO(data), allow_pickle=True)['x']

ds = DaosDataset(pool="torch", cont="my-dataset", transform_fn=transform)

print(f"Loaded dataset of {len(ds)} itmes")

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
