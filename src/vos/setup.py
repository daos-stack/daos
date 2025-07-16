"""
setup.py for packaging storage estimation tool.

To use type:

pip install .
"""
from setuptools import find_packages, setup

setup(
    name="storage_estimator",
    version="0.1",
    packages=find_packages(),
    description="DAOS storage estimation tools"
)
