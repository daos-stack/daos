#!/usr/bin/python
"""
(C) Copyright 2020 Intel Corporation.

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


class Bytes(object):
    """Defines a byte capacity with its units.

    Allows comparison values with different units, e.g. '1.6T' to '750G'.
    """

    SIZES = ["", "K", "M", "G", "T", "P", "E", "Z", "Y"]

    def __init__(self, amount, units):
        """Initialize a Bytes object.

        Args:
            amount (object): number of bytes relative to the unit specified
            units (str): units as a single letter
        """
        self._amount = 0
        self._units = self.SIZES[0]

        self.amount = amount
        self.units = units

    @property
    def amount(self):
        """Get the bytes amount."""
        return self._amount

    @property
    def units(self):
        """Get the units amount."""
        return self._units

    @amount.setter
    def amount(self, value):
        """Set the bytes amount.

        Args:
            value (object): number of bytes relative to the unit specified

        Raises:
            ValueError: if the value is invalid (not a number)

        """
        self.amount = float(value)
        if str(self.amount).endswith(".0"):
            self.amount = int(self.amount)

    @units.setter
    def units(self, value):
        """Set the units amount.

        Args:
            units (str): units as a single letter

        Raises:
            ValueError: if the value is not a supported size

        """
        if isinstance(value, str) and value.upper()[0] in self.SIZES:
            self.units = value.upper()[0]
        else:
            raise ValueError(
                "Invalid units: {} is not one of {}".format(value, self.SIZES))

    def __str__(self):
        """Return the string of the Bytes object.

        Returns:
            str: the byte amount and its units

        """
        return "{}{}".format(self.amount, self.units)

    def __eq__(self, other):
        """Determine if this object is equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object equals the other object

        """
        if isinstance(other, Bytes):
            return self.__str__() == str(other)
        return False

    def __ne__(self, other):
        """Determine if this object is not equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object does not equal the other object

        """
        return not self.__eq__(other)

    def __lt__(self, other):
        """Determine if this object is less than the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object is less than the other object

        """
        compare = False
        if isinstance(other, Bytes):
            if self.units in self.SIZES and other.units in self.SIZES:
                if self.units != other.units:
                    self_order = self.SIZES.index(self.units)
                    other_order = self.SIZES.index(other.units)
                    compare = self_order < other_order
                else:
                    compare = self.amount < other.amount
        return compare

    def __le__(self, other):
        """Determine if this object is less than or equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object is less than or equal to the other object

        """
        compare = False
        if isinstance(other, Bytes):
            if self.units in self.SIZES and other.units in self.SIZES:
                if self.units != other.units:
                    self_order = self.SIZES.index(self.units)
                    other_order = self.SIZES.index(other.units)
                    compare = self_order <= other_order
                else:
                    compare = self.amount <= other.amount
        return compare

    def __gt__(self, other):
        """Determine if this object is greater than the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object is greater than the other object

        """
        return not self.__eq__(other) and not self.__lt__(other)

    def __ge__(self, other):
        """Determine if this object is greater or equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object is greater than or equal to the other
                object

        """
        return self.__eq__(other) or not self.__lt__(other)

    def convert(self, unit):
        """Convert the current Bytes object to a different unit.

        Args:
            unit (str): unit to which to convert

        Raises:
            ValueError: if the unit is not valid

        """
        log = getLogger()
        original = self.__str__()
        if unit not in self.SIZES:
            raise ValueError(
                "Invalid 'unit'; must be {}: {}".format(self.SIZES, unit))
        power = self.SIZES.index(self.units) - self.SIZES.index(unit)
        self.amount *= 1000 ** power
        if int(self.amount) == self.amount or str(self.amount).endswith(".0"):
            self.amount = int(self.amount)
        self.units = unit
        log.debug("Bytes conversion: %sB -> %sB", original, str(self))

    def convert_down(self):
        """Decrement the units until the amount is greater than 1000."""
        log = getLogger()
        while self.amount < 1000 and self.units != self.SIZES[0]:
            previous_unit = self.SIZES[self.SIZES.index(self.units) - 1]
            self.convert(previous_unit)
        log.debug("Updated whole number Bytes: %s", str(self))

    def convert_up(self):
        """Increment the units until the amount is less than 1000."""
        log = getLogger()
        while self.amount > 1000 and self.units != self.SIZES[-1]:
            next_unit = self.SIZES[self.SIZES.index(self.units) + 1]
            self.convert(next_unit)
        log.debug("Updated whole number Bytes: %s", str(self))
