#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from concurrent.futures import ThreadPoolExecutor, as_completed, CancelledError, TimeoutError
from logging import getLogger

from avocado.utils.process import CmdResult


class ThreadManager():
    """Class to manage running any method as multiple threads."""

    def __init__(self, method, timeout=None):
        """Initialize a ThreadManager object with the the method to run as a thread.

        Args:
            method (callable): [description]
            timeout (int, optional): [description]. Defaults to None.
        """
        self.log = getLogger()
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
            self.log.info("Submitting %d threads ...", len(self.job_kwargs))
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

    def check_results(self, results):
        """Display the results from self.run() and indicate if any threads failed.

        Args:
            results (dict): results return from self.run()

        Returns:
            bool: True if any threads failed; false otherwise.

        """
        for key in sorted(results):
            self.log.info("Results from threads that %sED", key)
            for entry in results[key]:
                if isinstance(entry, CmdResult):
                    self.log.info(" command: %s", entry.command)
                    self.log.info(
                        " exit_status: %s, duration: %s, interrupted: %s",
                        entry.exit_status, entry.duration, str(entry.interrupted))
                    self.log.info(" stdout:")
                    for line in entry.stdout_text.splitlines():
                        self.log.info("    %s", line)
                    self.log.info(" stderr:")
                    for line in entry.stderr_text.splitlines():
                        self.log.info("    %s", line)
                else:
                    for line in str(entry).splitlines():
                        self.log.info(" %s", line)
        return len(results["FAIL"]) > 0

    def check_run(self):
        """Run the threads and check thr result.

        Returns:
            bool: True if any threads failed; false otherwise.

        """
        return self.check_results(self.run())
