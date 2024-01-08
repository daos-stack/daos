"""
setup.py for packaging pydaos python module.

To use type:

pip install .

or for older systems:

python3 setup.py install

If run from within a compiled DAOS source tree this it will detect the
install path automatically, otherwise it'll use the defaults.
"""
import json
import os

from setuptools import Extension, find_packages, setup


def load_conf():
    """Load the build config file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    while file_self != "/":
        new_file = os.path.join(file_self, ".build_vars.json")
        if os.path.exists(new_file):
            with open(new_file, "r", encoding="utf-8") as ofh:
                return json.load(ofh)

        file_self = os.path.dirname(file_self)
    return None


conf = load_conf()

args = {"sources": ["pydaos/pydaos_shim.c"], "libraries": ["daos", "duns"]}

if conf:
    args["include_dirs"] = [os.path.join(conf["PREFIX"], "include")]
    args["library_dirs"] = [os.path.join(conf["PREFIX"], "lib64")]
    args["runtime_library_dirs"] = args["library_dirs"]


setup(
    name="pydaos",
    version="0.3",
    packages=find_packages(),
    description="DAOS interface",
    ext_modules=[Extension("pydaos.pydaos_shim", **args)],
)
