#!/usr/bin/env python3 -u
"""
  (C) Copyright 2021-2022 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

from argparse import ArgumentParser, RawDescriptionHelpFormatter
import sys
from launch import get_test_list, set_python_environment


def main():
    """List DAOS functional tests."""
    # Parse the command line arguments
    description = [
        "DAOS functional test lister",
        "",
        "Lists tests by specifying a test tag.  For example:",
        "\tbadconnect  --list pool connect tests that pass NULL ptrs, etc.",
        "\tbadevict    --list pool client evict tests that pass NULL ptrs, "
        "etc.",
        "\tbadexclude  --list pool target exclude tests that pass NULL ptrs, "
        "etc.",
        "\tbadparam    --list tests that pass NULL ptrs, etc.",
        "\tbadquery    --list pool query tests that pass NULL ptrs, etc.",
        "\tmulticreate --list tests that create multiple pools at once",
        "\tmultitarget --list tests that create pools over multiple servers",
        "\tpool        --list all pool related tests",
        "\tpoolconnect --list all pool connection related tests",
        "\tpooldestroy --list all pool destroy related tests",
        "\tpoolevict   --list all client pool eviction related tests",
        "\tpoolinfo    --list all pool info retrieval related tests",
        "\tquick       --list tests that complete quickly, with minimal "
        "resources",
        "",
        "Multiple tags can be specified:",
        "\ttag_a,tag_b --list all tests with both tag_a and tag_b",
        "\ttag_a tag_b --list all tests with either tag_a or tag_b"
    ]
    parser = ArgumentParser(
        prog="list_tests.py",
        formatter_class=RawDescriptionHelpFormatter,
        description="\n".join(description))
    parser.add_argument(
        "tags",
        nargs="*",
        type=str,
        help="test category or file to list")
    args = parser.parse_args()
    print("Arguments: {}".format(args))

    # Setup the user environment
    set_python_environment()

    # Process the tags argument to determine which tests to run
    _, test_list = get_test_list(args.tags)

    # Verify at least one test was requested
    if not test_list:
        print("ERROR: No tests or tags found via {}".format(args.tags))
        sys.exit(1)

    # Display a list of the tests matching the tags
    print("Detected tests:  \n{}".format("  \n".join(test_list)))
    sys.exit(0)


if __name__ == "__main__":
    main()
