#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from os import environ
from time import sleep

from command_utils_base import ObjectWithParameters, BasicParameter
from pydaos.raw import DaosApiError


class CallbackHandler():
    """Defines a callback method to use with DaosApi class methods."""

    def __init__(self, delay=1):
        """Create a CallbackHandler object.

        Args:
            delay (int, optional): number of seconds to wait in between
                checking if the callback() method has been called.
                Defaults to 1.
        """
        self.delay = delay
        self.ret_code = None
        self.obj = None
        self._called = False
        self.log = getLogger(__name__)

    def callback(self, event):
        """Return an event from a DaosApi class method.

        Args:
            event (CallbackEvent): event returned by the DaosApi class method
        """
        # Get the return code and calling object from the event
        self.ret_code = event.event.ev_error
        self.obj = event.obj

        # Indicate that this method has being called
        self._called = True

    def wait(self):
        """Wait for this object's callback() method to be called."""
        # Reset the event return code and calling object
        self.ret_code = None
        self.obj = None

        # Wait for the callback() method to be called
        while not self._called:
            self.log.info(" Waiting ...")
            sleep(self.delay)

        # Reset the flag indicating that the callback() method was called
        self._called = False


class TestDaosApiBase(ObjectWithParameters):
    # pylint: disable=too-few-public-methods
    """A base class for functional testing of DaosPools objects."""

    # Constants to define whether to use API or a command to create and destroy
    # pools and containers.
    USE_API = "API"
    USE_DMG = "dmg"
    USE_DAOS = "daos"

    def __init__(self, namespace, cb_handler=None, crt_timeout=None):
        """Create a TestDaosApi object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
            crt_timeout (str, optional): value to use for the CRT_TIMEOUT when running pydaos
                commands. Defaults to None.
        """
        super().__init__(namespace)
        self.cb_handler = cb_handler
        self.debug = BasicParameter(None, False)
        self.silent = BasicParameter(None, False)

        # Test yaml parameter used to define the control method:
        #   USE_API    - use the API methods to create/destroy containers
        #   USE_DMG    - use the dmg command to create/destroy pools/containers
        #   USE_DAOS   - use the daos command to create/destroy pools/containers
        self.control_method = BasicParameter(self.USE_API, self.USE_API)

        # Set the CRT_TIMEOUT, if specified, for pydaos commands
        if crt_timeout is not None:
            environ["CRT_TIMEOUT"] = str(crt_timeout)
            self.log.info("Setting CRT_TIMEOUT to %s for pydaos commands", environ["CRT_TIMEOUT"])

    def _log_method(self, name, kwargs):
        """Log the method call with its arguments.

        Args:
            name (str): method name
            kwargs (dict): dictionary of method arguments
        """
        if self.debug.value:
            args = ", ".join(
                ["{}={}".format(key, kwargs[key]) for key in sorted(kwargs)])
            self.log.debug("  %s(%s)", name, args)

    def _call_method(self, method, kwargs):
        """Call the DAOS API class method with the optional callback method.

        Args:
            method (object): method to call
            kwargs (dict): keyworded arguments for the method
        """
        if self.cb_handler:
            kwargs["cb_func"] = self.cb_handler.callback

        # Optionally log the method call with its arguments if debug is set
        self._log_method(
            "{}.{}".format(
                method.__self__.__class__.__name__, method.__name__), kwargs)

        try:
            method(**kwargs)
        except DaosApiError as error:
            # Log the exception to obtain additional trace information
            if not self.silent.value:
                self.log.debug(
                    "Exception raised by %s.%s(%s)",
                    method.__module__, method.__name__,
                    ", ".join(
                        ["{}={}".format(key, val) for key, val in list(
                            kwargs.items())]),
                    exc_info=error)
            # Raise the exception so it can be handled by the caller
            raise error

        if self.cb_handler:
            # Wait for the call back if one is provided
            self.cb_handler.wait()


class LabelGenerator():
    # pylint: disable=too-few-public-methods
    """Generates label used for pools and containers."""

    def __init__(self, value=1):
        """Constructor.

        Args:
            value (int): Number that's attached after the base_label.
        """
        self.value = value

    def get_label(self, base_label):
        """Create a label by adding number after the given base_label.

        Args:
            base_label (str): Label prefix. Don't include space.

        Returns:
            str: Created label.

        """
        label = base_label
        if label is not None:
            label = "_".join([base_label, str(self.value)])
            self.value += 1
        return label
