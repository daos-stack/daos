#!/usr/bin/python3
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import threading

from scrubber_utils import ScrubberUtils
from ior_test_base import IorTestBase


class TestWithScrubber(IorTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
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
        self.scrubber = ScrubberUtils(self.get_dmg_command(),
                                      self.server_managers[0].hosts)
        self.daos_cmd = self.get_daos_command()
        self.dmg_cmd = self.get_dmg_command()
        self.pool = None
        self.container = None
        self.initial_metrics = {}
        self.final_metrics = {}

    def verify_scrubber_metrics_value(self, initial_metrics, final_metrics):
        """ Compare the initial metrics value to final value after
            IO data. The counters should increase from initial value.

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
        # Use some of the default values provided in scrubber testbase file.
        # Testing scenario : Create a pool and container without properties
        # and update them at runtime.
        if pool_prop is None:
            self.add_pool(create=False, connect=False)
            self.pool.properties.value = pool_prop
            self.pool.create()
            self.pool.connect()
        else:
            self.add_pool()
        self.add_container(pool=self.pool, create=False)
        if pool_prop is None:
            pool_prop = "scrub:timed,scrub-freq:1"
        if cont_prop is None:
            cont_prop = "cksum:crc16"
        x = pool_prop.split(",")
        for prop_val in x:
            if prop_val is not None:
                value = prop_val.split(":")
                self.pool.set_property(value[0], value[1])
        self.container.properties.value = cont_prop
        self.container.create()
        values = "Pool : {} Container: {}".format(self.pool, self.container)
        self.log.info(values)

    def run_ior_and_check_scruber_status(self, pool, cont, fail_on_warning=True):
        """Run IOR and get scrubber metrics

        Args:
            pool (object): Pool object
            cont (object): Container object within the pool.
            fail_on_warning (bool, optional): [description]. Defaults to True.

        Returns:
            status(bool) : True (Srubber working), False(Scrubber not working)
        """
        status = False
        self.initial_metrics = self.scrubber.get_csum_total_metrics()
        self.pool = pool
        self.container = cont
        # Print the pool properties
        result = self.dmg_cmd.pool_get_prop(self.pool.uuid, "scrub")
        self.log.info("Pool Properties")
        self.log.info("===============")
        self.log.info(result)
        result = self.daos_cmd.container_get_prop(self.pool.uuid, self.container.uuid)
        self.log.info("Container Properties")
        self.log.info("===============")
        self.log.info(result)

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.run_ior_with_pool,
                                   kwargs={"create_pool": True,
                                           "create_cont": False,
                                           "fail_on_warning": fail_on_warning})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        process.join()
        self.final_metrics = self.scrubber.get_csum_total_metrics()
        # Just make sure scrubber is working here.
        status = self.verify_scrubber_metrics_value(self.initial_metrics, self.final_metrics)
        return status
