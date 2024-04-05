#!/usr/bin/env python3
"""Check if modules are installed and match the versions in utils/cq/requirements.txt."""

import os
import sys
from argparse import ArgumentParser

metadata = None  # pylint: disable=invalid-name
pkg_resources = None  # pylint: disable=invalid-name
Requirement = None  # pylint: disable=invalid-name
try:
    # Only available in python 3.8+, or installed as a pip package in older versions
    from importlib import metadata
except (ModuleNotFoundError, ImportError):
    try:
        # Older package in setuptools, but deprecated.
        import pkg_resources
    except (ModuleNotFoundError, ImportError):
        pass

try:
    # Usually available but technically not builtin
    from packaging.requirements import Requirement
except (ModuleNotFoundError, ImportError):
    try:
        # Older package in setuptools, but deprecated.
        # For our purposes, it's a drop-in replacement
        from pkg_resources import Requirement  # pylint: disable=ungrouped-imports
    except (ModuleNotFoundError, ImportError):
        pass


GITHOOKS_DIR = os.path.dirname(os.path.realpath(__file__))
REQ_FILE_PATH = os.path.realpath(os.path.join(GITHOOKS_DIR, "..", "cq", "requirements.txt"))


def read_reqs():
    """Read the requirements.txt.

    Returns:
        list: list of Requirement objects from requirements.txt
    """
    with open(REQ_FILE_PATH, "r") as req_file:
        return list(map(
            Requirement,
            filter(
                lambda x: not x.startswith("#"),
                req_file.read().splitlines())))


def module_version(module_name):
    """Get the version of a module.

    Args:
        module_name (str): name of the module

    Raises:
        NameError: if both metadata and pkg_resources are not imported

    Returns:
        str: the module version
    """
    if metadata is not None:
        try:
            return metadata.version(module_name)
        except metadata.PackageNotFoundError:
            return None
    if pkg_resources is not None:
        try:
            return pkg_resources.get_distribution(module_name).version
        except pkg_resources.DistributionNotFound:
            return None
    raise NameError("Neither metadata nor pkg_resources is defined")


def check(modules):
    """Check the list of modules against the requirements.txt.

    Args:
        modules (list): module names to check. Empty list to check all

    Returns:
        int: -1 on failure; 1 on warning; 0 on success
    """
    try:
        all_reqs = read_reqs()
    except Exception as err:  # pylint: disable=broad-except
        print(f"Error reading requirements.txt:\n{err}")
        sys.exit(1)
    do_warn = False
    do_fail = False

    all_reqs_names = set(req.name for req in all_reqs)
    modules_to_check = set(modules)

    # Check all
    if len(modules_to_check) == 0:
        modules_to_check = all_reqs_names

    # Don't check modules that aren't in requirements.txt
    # Just print a warning
    for module in modules_to_check - all_reqs_names:
        print(f"WARNING: module {module} not in {REQ_FILE_PATH}")
        modules_to_check.remove(module)

    for req in all_reqs:
        if req.name not in modules_to_check:
            continue
        try:
            installed_version = module_version(req.name)
        except NameError:
            # Fatal
            print(f"ERROR: failed to check version of {req.name}")
            sys.exit(1)
        if not installed_version:
            # Not installed; error
            print(f"WARNING: expected {req}")
            do_fail = True
        elif not req.specifier.contains(installed_version):
            # Installed but incorrect version; warning
            print(f"WARNING: expected {req}")
            do_warn = True

    if do_fail or do_warn:
        print(
            "To improve pre-commit checks, install python packages with "
            + f"python3 -m pip install -r {REQ_FILE_PATH}")
    if do_fail:
        sys.exit(1)
    sys.exit(0)


def main():
    """main execution"""
    parser = ArgumentParser()
    parser.add_argument(
        "modules",
        nargs="*",
        help="module names to check for. omit to check all modules")
    args = parser.parse_args()
    check(args.modules)


if __name__ == "__main__":
    main()
