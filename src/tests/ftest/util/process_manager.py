#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from multiprocessing import get_context, current_process
from os import error, name
from command_utils_base import CommandFailure


def run_command(command, queue, method="run", **kwargs):
    """Execute the command's method and place the result on the queue.

    Args:
        command (ExecutableCommand): command whose 'method' method will be called
        queue (multiprocessing.Queue): queue in which to place the object.result
        method (str, optional): the command's method to call. Defaults to "run".
    """
    if hasattr(command, method):
        try:
            queue.put(ProcessResult(current_process().name, result=command.method(**kwargs)))
        except CommandFailure as error:
            if hasattr(command, "result"):
                queue.put(ProcessResult(current_process().name, error, command.result))
            else:
                queue.put(ProcessResult(current_process().name, error=error))
    else:
        queue.put(
            ProcessResult(
                current_process().name, error="Invalid method '{}' for {}".format(method, command)))


def run_method(queue, method, **kwargs):
    """Execute the method and place the results on the queue.

    Args:
        queue (multiprocessing.Queue): queue in which to place the results from the method
        method (object): the method to call.
    """
    try:
        queue.put(ProcessResult(current_process().name, result=method(**kwargs)))
    except CommandFailure as error:
        queue.put(ProcessResult(current_process().name, error=error))


class ProcessResult():
    """An object representing the result of a single multiprocessing process."""

    def __init__(self, name, error=None, result=None):
        """Initialize a ProcessResult object.

        Args:
            name (str): the process name associated with this result
            error (str, optional): the process error. Defaults to None.
            result (object, optional): the process result. Defaults to None.
        """
        self.name = name
        self.error = error
        self.result = result

    def __str__(self) -> str:
        """Get the str representation of this object."""
        return "Process {} {}: {}".format(
            self.name, "completed" if self.error is None else "failed",
            self.result if self.error is None else self.error)


class ProcessManager():
    """A manager fpr multiple multiprocessing processes."""

    def __init__(self):
        """Initialize a ProcessManager object."""
        self._log = getLogger()
        self._context = get_context("spawn")
        self._queue = self._context.Queue()
        self._processes = []

    @property
    def quantity(self):
        """Get the number of managed processes.

        Returns:
            int: number of processes managed by this class
        """
        return len(self._processes)

    def add_command(self, command, method="run", **kwargs):
        """Add a process to be managed by this class.

        Each process job the executes run_command() method with the provided arguments.

        Args:
            command (ExecutableCommand): command whose 'method' method will be called
            method (str, optional): the command's method to call. Defaults to "run".
        """
        self._log.info("Adding process %d for %s.%s", self.quantity + 1, str(command), method)
        kwargs["command"] = command
        kwargs["queue"] = self._queue
        kwargs["method"] = method
        self._processes.append(self._context.Process(target=run_command, **kwargs))

    def add_method(self, method, **kwargs):
        """Add a process to be managed by this class.

        Each process job the executes run_method() method with the provided arguments.

        Args:
            method (str): the method to run.
        """
        self._log.info("Adding process %d for %s", self.quantity + 1, method)
        kwargs["queue"] = self._queue
        kwargs["method"] = method
        self._processes.append(self._context.Process(target=run_method, kwargs=kwargs))

    def start(self):
        """Start each process."""
        self._log.info("Starting %d processes ...", self.quantity)
        self._results = []
        for process in self._processes:
            self._log.info("  Starting process %s", process.name)
            process.start()
        self._log.info("All %d processes started", self.quantity)

    def get_results(self):
        """Get the results from each running process.

        Returns:
            dict: a dictionary of each completed process' ProcessResult indexed by the process name.

        """
        results = {}
        self._log.info("Waiting for %d jobs ...", self.job_qty)
        remaining = self.quantity - len(results)
        while remaining > 0:
            self._log.info("  Waiting for results from %d/%d jobs ...", remaining, self.quantity)
            data = self._queue.get()
            results[data.name] = data
            remaining = self.quantity - len(results)

        self._log.info("All %d jobs complete", self.job_qty)
        return results
