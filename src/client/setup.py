"""
setup.py for packaging pydaos python module.

To use type:

pip install .

or for older systems:

python3 setup.py install

This can be run from either the installed daos packages or from a install directory, however python
requires write access to the directory to install so if installing from rpms then a copy may have to
be made before install.
"""
import os

from setuptools import Extension, find_packages, setup


def append_paths(args):
    """
    Append necessary paths to the headers and DAOS shared libraries
    to build and load C extensions
    """

    prefix_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")

    if os.path.exists(os.path.join(prefix_dir, "include", "daos.h")):
        args["include_dirs"] = [os.path.join(prefix_dir, "include")]
        args["library_dirs"] = [os.path.join(prefix_dir, "lib64")]
        args["runtime_library_dirs"] = args["library_dirs"]

    return args


pydaos_args = {"sources": ["pydaos/pydaos_shim.c"], "libraries": ["daos", "duns"]}
torch_args = {"sources": ["pydaos/torch/torch_shim.c"], "libraries": ["daos", "dfs"]}

pydaos_args = append_paths(pydaos_args)
torch_args = append_paths(torch_args)


setup(
    name="pydaos",
    version="0.3",
    packages=find_packages(),
    description="DAOS interface",
    ext_modules=[Extension("pydaos.pydaos_shim", **pydaos_args),
                 Extension("pydaos.torch.torch_shim", **torch_args)],
)
