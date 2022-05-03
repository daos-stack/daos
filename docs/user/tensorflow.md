# DAOS Tensorflow-IO

Tensorflow-IO is an open-source Python sub-library of the Tensorflow framework
which offers a wide range of file systems and formats (e.g., HDFS, HTTP) otherwise unavailable
in Tensorflow's built-in support.

For a complete look at the functionalities of the Tensorflow-IO library,
visit the official [Tensorflow Documentation](https://www.tensorflow.org/api_docs/python/tf/io/gfile).

[The DFS Plugin](https://github.com/daos-stack/tensorflow-io-daos/tree/devel/tensorflow_io/core/filesystems/dfs) supports
many of Tensorflow-IO API functionalities and adds support to the DAOS FileSystem.

This constitutes several operations, including reading and writing datasets
on the DAOS filesystem for AI workloads that use Tensorflow as a framework.

## Supported API

Tensorflow-IO offers users several operations for loading data and manipulating
file-systems. These operations include :

- FileSystem Operations, e.g., creation and deletion of files, querying files directories etc.
- File-specific operations for:
  - RandomAccessFiles
  - WritableFiles
  - ReadOnlyMemoryRegion (which is left unimplemented in the case of the DFS plugin)

The DFS Plugin translates the key operations offered by Tensorflow IO to their DAOS Filesystem equivalent while utilizing
DAOS’s underlying functionalities and features ensure high I/O bandwidth for its users.

## Setup

To utilize the DFS Plugin, the Tensorflow-IO library will need to be
built from [source](https://github.com/daos-stack/tensorflow-io-daos/tree/devel).

### Prerequisites

Assuming you are in a terminal in the repository root directory:

- Install the latest versions of the following dependencies by running
  - Centos 8

      ```bash
      yum install -y python3 python3-devel gcc gcc-c++ git unzip which make
      ```

  - Ubuntu 20.04

       ```bash
       sudo apt-get -y -qq update 
       sudo apt-get -y -qq install gcc g++ git unzip curl python3-pip
       ```

- Download the Bazel installer

  ```bash
    curl -sSOL https://github.com/bazelbuild/bazel/releases/download/\$(cat .bazelversion)/bazel-\$(cat .bazelversion)-installer-linux-x86_64.sh
  ```

- Install Bazel

  ```bash
  bash -x -e bazel-$(cat .bazelversion)-installer-linux-x86_64.sh
  ```

- Update Pip and install pytest

  ```bash
  python3 -m pip install -U pip
  python3 -m pip install pytest
  ```

### Building

Assuming you are in a terminal in the repository root directory:

- Configure and install tensorflow (the current version should be tensorflow2.6.2)

  ```bash
  $ ./configure.sh
  ## Set python3 as default.
  $ ln -s /usr/bin/python3 /usr/bin/python
  ```

- At this point, all libraries and dependencies should be installed.
  - Make sure the environment variable --LIBRARY_PATH-- includes the paths to all daos libraries
  - Make sure the environment variable --LD_LIBRARY_PATH-- includes the paths to:
    - All daos libraries
    - The tensorflow framework (libtensorflow and libtensorflow_framework)
  - If not, find the required libraries and add their paths to the environment variable

      ```bash
      export LD_LIBRARY_PATH="<path-to-library>:$LD_LIBARY_PATH"
      ```

  - Make sure the environment variable --CPLUS_INCLUDE_PATH-- and --C_INCLUDE_PATH-- includes the paths to:
    - The tensorflow headers (usually in /usr/local/lib64/python3.6/site-packages/tensorflow/include)
  - If not, find the required headers and add their paths to the environment variable

      ```bash
      export CPLUS_INCLUDE_PATH="<path-to-headers>:$CPLUS_INCLUDE_PATH"
      export C_INCLUDE_PATH=$CPLUS_INCLUDE_PATH:$C_INCLUDE_PATH
      ```

    - Build the project using Bazel

        ```bash
        bazel build --action_env=LIBRARY_PATH=$LIBRARY_PATH -s --verbose_failures //tensorflow_io/... //tensorflow_io_gcs_filesystem/...
        ```

        This should take a few minutes.

        Note that sandboxing may result in build failures when using
        Docker Containers for DAOS due to mounting issues, if that’s the case,
        add ----spawn_strategy=standalone-- to the above build command to
        bypass sandboxing. (When disabling sandbox, an error may be thrown for
        an undefined type z_crc_t due to a conflict in header files.
        In that case, find the crypt.h file in the bazel cache in subdirectory
        /external/zlib/contrib/minizip/crypt.h and add the following line to the
        file --typedef unsigned long z_crc_t;-- then re-build).

### Testing

Assuming you are in a terminal in the repository root directory:

- Run the following command for the simple serial test to validate the build. Note that any tests need to be run with the TFIO_DATAPATH flag to specify the location of the binaries.

  ```bash
  TFIO_DATAPATH=bazel-bin python3 -m pytest -s -v tests/test_serialization.py
  ```

- Run the following commands to run the dfs plugin test:

  ```bash
  # To create the required pool and container and export required env variables for the dfs tests.
  $ source tests/test_dfs/dfs_init.sh
  # To run dfs tests
  $ TFIO_DATAPATH=bazel-bin python3 -m pytest -s -v tests/test_dfs.py
  # For Cleanup, deletes pools and containers created for test.
  $ bash ./tests/test_dfs/dfs_cleanup.sh
  ```

## User Guide

To use the Tensorflow-IO Library, you'll need to import the required packages
as follows:

```python
import tensorflow as tf
import tensorflow_io as tfio
```

To use the DFS Plugin, all that needs to be done is to supply the paths of the required
files/directories in the form of a DFS URI:

```sh
dfs://<Pool-Label-or-UUID>/<Cont-Label-Or-UUID>/<Path>
# OR
dfs://Path, where Path includes the path to the DAOS container
```

```python
filename = "dfs://POOL_LABEL/CONT_LABEL/FILE_NAME.ext"
```

A range of operations can be performed on files and directories stored in a specific
container.

```python
with tf.io.gfile.GFile(filename, "w") as new_file:
    new_file.write("Hello World")

data = ""
with tf.io.gfile.Gfile(filename, "r") as read_file:
    data = read_file.read()
```

An example of using Tensorflow-IO's DFS Plugin to load and train a model on the MNIST Dataset
can be found [here](https://github.com/daos-stack/tensorflow-io-daos/blob/devel/docs/tutorials/daos.ipynb).
