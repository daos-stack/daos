#!/usr/bin/env python3

"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from argparse import ArgumentParser
import logging
import sys

# pylint: disable=import-error,no-name-in-module
from logger_utils import get_console_handler
from core_file import CoreFileProcessing, CoreFileException

# Set up a logger for the console messages
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))


def main():
    """Generate a stacktrace for each core file in the provided directory."""
    parser = ArgumentParser(
        prog="process_core_files.py",
        description="Generate stacktrace files from the core files in the specified directory.")
    parser.add_argument(
        "-d", "--delete",
        action="store_true",
        help="delete the original core files")
    parser.add_argument(
        "directory",
        type=str,
        help="directory containing the core files to process")
    args = parser.parse_args()

    core_file_processing = CoreFileProcessing(logger)
    try:
        core_file_processing.process_core_files(args.directory, args.delete)

    except CoreFileException as error:
        logger.error(str(error))
        sys.exit(1)

    except Exception:       # pylint: disable=broad-except
        logger.error("Unhandled error processing test core files",)
        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":
    main()
