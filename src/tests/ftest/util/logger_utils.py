"""
  (C) Copyright 2020-2022 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import logging

DATE_FORMAT = r"%Y/%m/%d %I:%M:%S"
LOG_FILE_FORMAT = "%(asctime)s %(levelname)-5s %(funcName)30s: %(message)s"


def get_console_handler(log_format, log_level):
    """Get a logging console (stream) handler.

    Args:
        log_format (str): the logging format
        log_level (int): the logging level

    Returns:
        logging.StreamHandler: a logging handler for console messages

    """
    console_handler = logging.StreamHandler()
    console_handler.setLevel(log_level)
    console_handler.setFormatter(logging.Formatter(log_format, datefmt=DATE_FORMAT))
    return console_handler


def get_file_handler(log_file, log_format, log_level):
    """Get a logging file handler.

    Args:
        log_file (str): the file which will contain the log messages
        log_format (str): the logging format
        log_level (int): the logging level

    Returns:
        logging.FileHandler: a logging handler for messages included in the file

    """
    log_handler = logging.FileHandler(log_file, encoding='utf-8')
    log_handler.setLevel(log_level)
    log_handler.setFormatter(logging.Formatter(log_format, datefmt=DATE_FORMAT))
    return log_handler
