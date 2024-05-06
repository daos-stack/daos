"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import time
from logging import getLogger


class SimpleProfiler():
    """Simple profiler class.

    Counts the number of times a function is called and measure its execution
    time.
    """

    def __init__(self):
        """Initialize a SimpleProfiler object."""
        self._stats = {}
        self._logger = getLogger()

    def clean(self):
        """Clean the metrics collect so far."""
        self._stats = {}

    def run(self, fun, tag, *args, **kwargs):
        """Run a function and update its stats.

        Args:
            fun (function): Function to be executed
            args  (tuple): Argument list
            kwargs (dict): variable-length named arguments
        """
        self._logger.info("Running function: %s()", fun.__name__)

        start_time = time.time()

        ret = fun(*args, **kwargs)

        end_time = time.time()
        elapsed_time = end_time - start_time
        self._logger.info(
            "Execution time: %s", self._pretty_time(elapsed_time))

        if tag not in self._stats:
            self._stats[tag] = [0, []]

        self._stats[tag][0] += 1
        self._stats[tag][1].append(elapsed_time)

        return ret

    def get_stat(self, tag):
        """Retrieve the stats of a function.

        Args:
            tag (str): Tag to be query

        Returns:
            tuple: A tuple of the fastest (max), slowest (min), and average
                execution times.

        """
        data = self._stats.get(tag, [0, []])

        return self._calculate_metrics(data[1])

    def set_logger(self, fun):
        """Assign the function to be used for logging.

        Set the function that will be used to print the elapsed time on each
        function call. If this value is not set, the profiling will be
        performed silently.

        Parameters:
            fun (function): Function to be used for logging.

        """
        self._logger = fun

    def print_stats(self):
        """Print all the stats collected so far.

        If the logger has not been set, the stats will be printed by using the
        built-in print function.
        """
        self._logger.info("{0:20} {1:5} {2:10} {3:10} {4:10}".format(
            "Function Tag", "Hits", "Max", "Min", "Average"))

        for fname, data in list(self._stats.items()):
            max_time, min_time, avg_time = self._calculate_metrics(data[1])
            self._logger.info(
                "{0:20} {1:5} {2:10} {3:10} {4:10}".format(
                    fname,
                    data[0],
                    self._pretty_time(max_time),
                    self._pretty_time(min_time),
                    self._pretty_time(avg_time)))

    @classmethod
    def _pretty_time(cls, ftime):
        """Convert to pretty time string."""
        return time.strftime("%H:%M:%S", time.gmtime(ftime))

    @classmethod
    def _calculate_metrics(cls, data):
        """Calculate the maximum, minimum and average values of a given list."""
        max_time = max(data)
        min_time = min(data)
        avg_time = sum(data) / len(data) if data else 0

        return max_time, min_time, avg_time
