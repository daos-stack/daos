#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from concurrent.futures import ThreadPoolExecutor, as_completed, CancelledError, TimeoutError


class ThreadManager():
    """Class to manage running any method as multiple threads."""

    def __init__(self, method, timeout=None):
        """Initialize a ThreadManager object with the the method to run as a thread.

        Args:
            method (callable): [description]
            timeout (int, optional): [description]. Defaults to None.
        """
        self.method = method
        self.timeout = timeout
        self.job_kwargs = []

    @property
    def qty(self):
        """Get the number of threads.

        Returns:
            int: number of threads

        """
        return len(self.job_kwargs)

    def add(self, **kwargs):
        """Add a thread to run by specifying the keyword arguments for the thread method."""
        self.job_kwargs.append(kwargs)

    def run(self):
        """Asynchronously run the method as a thread for each set of method arguments.

        Returns:
            dict: a dictionary of lists of results returned by each thread under either a "PASS" or
                "FAIL" key

        """
        results = {"PASS": [], "FAIL": []}
        with ThreadPoolExecutor() as thread_executor:
            futures = {thread_executor.submit(self.method, **kwargs) for kwargs in self.job_kwargs}
            for future in as_completed(futures, self.timeout):
                try:
                    results["PASS"].append(future.result())
                except CancelledError as error:
                    results["FAIL"].append("{} was cancelled: {}".format(future, error))
                except TimeoutError as error:
                    results["FAIL"].append("{} timed out: {}".format(future, error))
                except Exception as error:
                    results["FAIL"].append("{} failed with an exception: {}".format(future, error))
        return results
