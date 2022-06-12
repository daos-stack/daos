#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from concurrent.futures import ThreadPoolExecutor, as_completed, TimeoutError
from logging import getLogger

from avocado.utils.process import CmdResult


class ThreadResult():
    """Class containing the results of a method executed by the ThreadManager class."""

    def __init__(self, id, passed, args, result):
        """Initialize a ThreadResult object.

        Args:
            id (int): the thread id for this result
            passed (bool): whether the thread completed or raised an exception
            args (dict): the arguments passed to the thread method
            result (object): the object returned by the thread method
        """
        self.id = id
        self.passed = passed
        self.args = args
        self.result = result

    def __str__(self):
        """Return the string respresentation of this object.

        Returns:
            str: the string respresentation of this object

        """
        def get_result(result):
            """Get the result information to display.

            Args:
                result (object): the result returned by the thread

            Returns:
                list: each line of the result information

            """
            data = []
            if isinstance(result, CmdResult):
                data.append("    command:     {}".format(result.command))
                data.append("    exit_status: {}".format(result.exit_status))
                data.append("    duration:    {}".format(result.duration))
                data.append("    interrupted: {}".format(result.interrupted))
                data.append("    stdout:")
                for line in result.stdout_text.splitlines():
                    data.append("      {}".format(line))
                data.append("    stderr:")
                for line in result.stderr_text.splitlines():
                    data.append("      {}".format(line))
            else:
                for line in str(result).splitlines():
                    data.append("    {}".format(line))
            return data

        info = ["Thread {} results:".format(self.id), "  args: {}".format(self.args), "  result:"]
        if isinstance(self.result, list):
            for this_result in self.result:
                info.extend(get_result(this_result))
        else:
            info.extend(get_result(self.result))
        return "\n".join(info)


class ThreadManager():
    """Class to manage running any method as multiple threads."""

    def __init__(self, method, timeout=None):
        """Initialize a ThreadManager object with the the method to run as a thread.

        Args:
            method (callable): python method to execute in each thread
            timeout (int, optional): timeout for all thread execution. Defaults to None.
        """
        self.log = getLogger()
        self.method = method
        self.timeout = timeout
        self.job_kwargs = []
        self.futures = {}

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
            list: a list of ThreadResult objects containing the results of each method.

        """
        results = []
        with ThreadPoolExecutor() as thread_executor:
            self.log.info("Submitting %d threads ...", len(self.job_kwargs))
            # Keep track of thread ids by assigning an index to each Future object
            futures = {
                thread_executor.submit(self.method, **kwargs): index
                for index, kwargs in enumerate(self.job_kwargs)}
            try:
                for future in as_completed(futures, self.timeout):
                    id = futures[future]
                    try:
                        results.append(
                            ThreadResult(id, True, self.job_kwargs[id], future.result()))
                        self.log.info("Thread %d passed: %s", id, results[-1])
                    except Exception as error:
                        results.append(ThreadResult(id, False, self.job_kwargs[id], str(error)))
                        self.log.info("Thread %d failed: %s", id, results[-1])
            except TimeoutError as error:
                for future in futures:
                    if not future.done():
                        # pylint: disable-next=invalid-sequence-index
                        results.append(ThreadResult(id, False, self.job_kwargs[id], str(error)))
                        self.log.info("Thread %d timed out: %s", id, results[-1])
        return results

    def check(self, results):
        """Display the results from self.run() and indicate if any threads failed.

        Args:
            results (list): a list of ThreadResults from self.run()

        Returns:
            int: number of threads that failed

        """
        failed = []
        self.log.info("Results from threads that passed:")
        for result in results:
            if result.passed:
                self.log.info(str(result))
            else:
                failed.append(result)
        if failed:
            self.log.info("Results from threads that failed:")
            for result in failed:
                self.log.info(str(result))
        return len(failed)

    def check_run(self):
        """Run the threads and check thr result.

        Returns:
            int: number of threads that failed

        """
        return self.check(self.run())
