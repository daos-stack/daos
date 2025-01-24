"""
(C) Copyright 2021-2024 Intel Corporation.
(C) Copyright 2025 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading

from ior_test_base import IorTestBase
from scrubber_utils import ScrubberUtils


class TestWithScrubber(IorTestBase):
    """Test with scrubber enabled.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super().__init__(*args, **kwargs)
        self.scrubber = None

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.scrubber = ScrubberUtils(self.get_dmg_command(), self.server_managers[0].hosts)
        self.pool = None
        self.container = None

    def verify_scrubber_metrics_value(self, initial_metrics, final_metrics):
        """Compare the initial metrics value to final value after IO data.

        The counters should increase from initial value.

        Args:
            initial_metrics (dict): Initial metrics dictionary before testing.
            final_metrics (dict): Final metrics dictionary after testing is complete.

        Returns:
            status (bool): True :  if all metric value changed.
            False: if any metrics value doesn't increment or change.
        """
        self.log.info("Verifying the scrubber metrics values")
        status = True
        self.log.info("Initial Metrics Information")
        self.log.info("===========================")
        self.log.info(initial_metrics)
        self.log.info("Final Metrics Information")
        self.log.info("=========================")
        self.log.info(final_metrics)
        # If both initial and final metrics are same, return false.
        if initial_metrics == final_metrics:
            status = False
        return status

    def create_pool_cont_with_scrubber(self, pool_prop=None, cont_prop=None):
        """Create a pool with container with scrubber enabled.

        Args:
            pool_prop (str, optional): pool properties string.
                Defaults to None.
            cont_prop (str, optional): container properties string.
                Defaults to None.
        """
        # If pool_prop is None, don't use anything from YAML file.
        # If cont_prop is None, don't use  the information from YAML file.
        # Use some of the default values provided in scrubber test base file.
        # Testing scenario : Create a pool and container without properties
        # and update them at runtime.
        if pool_prop is None:
            # Create without properties and set at runtime below
            self.add_pool(properties=None)
        else:
            # Create with properties
            self.add_pool()
        if pool_prop is None:
            pool_prop = "scrub:timed,scrub_freq:1"
        for prop_val in pool_prop.split(","):
            if prop_val is not None:
                value = prop_val.split(":")
                self.pool.set_property(value[0], value[1])
        if cont_prop is None:
            cont_prop = "cksum:crc16"
        self.add_container(pool=self.pool, properties=cont_prop)

    def run_ior_and_check_scrubber_status(self, pool, cont):
        """Run IOR and get scrubber metrics

        Args:
            pool (object): Pool object
            cont (object): Container object within the pool.

        Returns:
            bool: True (Scrubber working), False(Scrubber not working)
        """
        initial_metrics = self.scrubber.get_csum_total_metrics()
        self.pool = pool
        self.container = cont
        # Print the pool properties
        result = self.pool.get_prop("scrub")
        self.log.info("Pool Properties")
        self.log.info("===============")
        self.log.info(result)
        result = self.container.get_prop()
        self.log.info("Container Properties")
        self.log.info("===============")
        self.log.info(result)

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.run_ior_with_pool,
                                   kwargs={"create_pool": True,
                                           "create_cont": False,
                                           "fail_on_warning": True,
                                           "timeout": self.ior_timeout})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        process.join()
        final_metrics = self.scrubber.get_csum_total_metrics()
        # Just make sure scrubber is working here.
        return self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
