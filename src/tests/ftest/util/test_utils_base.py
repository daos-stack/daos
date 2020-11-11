#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from logging import getLogger
from time import sleep

from command_utils_base import ObjectWithParameters, BasicParameter
from pydaos.raw import DaosApiError


class CallbackHandler(object):
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

    def __init__(self, namespace, cb_handler=None):
        """Create a TestDaosApi object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        super(TestDaosApiBase, self).__init__(namespace)
        self.cb_handler = cb_handler
        self.debug = BasicParameter(None, False)

        # Test yaml parameter used to define the control method:
        #   USE_API    - use the API methods to create/destroy containers
        #   USE_DMG    - use the dmg command to create/destroy pools/containers
        #   USE_DAOS   - use the daos command to create/destroy pools/containers
        self.control_method = BasicParameter(self.USE_API, self.USE_API)

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
            "{}.{}".format(method.im_class.__name__, method.__name__), kwargs)

        try:
            method(**kwargs)
        except DaosApiError as error:
            # Log the exception to obtain additional trace information
            self.log.debug(
                "Exception raised by %s.%s(%s)",
                method.__module__, method.__name__,
                ", ".join(
                    ["{}={}".format(key, val) for key, val in kwargs.items()]),
                exc_info=error)
            # Raise the exception so it can be handled by the caller
            raise error

        if self.cb_handler:
            # Wait for the call back if one is provided
            self.cb_handler.wait()

    def _check_info(self, check_list):
        """Verify each info attribute value matches an expected value.

        Args:
            check_list (list): a list of tuples containing the name of the
                information attribute to check, the current value of the
                attribute, and the expected value of the attribute. If the
                expected value is specified as a string with a number preceded
                by '<', '<=', '>', or '>=' then this comparison will be used
                instead of the default '=='.

        Returns:
            bool: True if at least one check has been specified and all the
            actual and expected values match; False otherwise.

        """
        check_status = len(check_list) > 0
        for check, actual, expect in check_list:
            # Determine which comparison to utilize for this check
            compare = ("==", lambda x, y: x == y, "does not match")
            if isinstance(expect, str):
                comparisons = {
                    "<": (lambda x, y: x < y, "is too large"),
                    ">": (lambda x, y: x > y, "is too small"),
                    "<=": (
                        lambda x, y: x <= y, "is too large or does not match"),
                    ">=": (
                        lambda x, y: x >= y, "is too small or does not match"),
                }
                for key, val in comparisons.items():
                    # If the expected value is preceded by one of the known
                    # comparison keys, use the comparison and remove the key
                    # from the expected value
                    if expect[:len(key)] == key:
                        compare = (key, val[0], val[1])
                        expect = expect[len(key):]
                        try:
                            expect = int(expect)
                        except ValueError:
                            # Allow strings to be strings
                            pass
                        break
            self.log.info(
                "Verifying the %s %s: %s %s %s",
                self.__class__.__name__.replace("Test", "").lower(),
                check, actual, compare[0], expect)
            if not compare[1](actual, expect):
                msg = "  The {} {}: actual={}, expected={}".format(
                    check, compare[2], actual, expect)
                self.log.error(msg)
                check_status = False
        return check_status
