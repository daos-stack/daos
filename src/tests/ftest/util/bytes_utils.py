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


class BytePrefix(object):
    # pylint: disable=too-few-public-methods
    """Defines the unit for a Byte object."""

    ORDER = ("", "K", "M", "G", "T", "P", "E", "Z", "Y")

    def __init__(self, value=None):
        """Initialize a ByteUnit object with a unit.

        Args:
            value (str, optional): case-sensitive unit value. Defaults to None.
        """
        self._value = ""
        self._base = 1000

        supported = [""]
        supported.extend(["".join([item, "B"]) for item in self.ORDER if item])
        supported.extend(["".join([item, "iB"]) for item in self.ORDER if item])
        if value in supported:
            self._value = str(value)
        elif value is not None:
            raise ValueError(
                "Invalid BytePrefix value; {} is not a {}".format(
                    value, supported))

        if "i" in self._value:
            self._base = 1024

    @property
    def base(self):
        """Get the base.

        Returns:
            int: unit base

        """
        return self._base

    @property
    def order(self):
        """Get the unit order.

        Returns:
            int: unit order

        """
        return self.ORDER.index(self._get_order_key())

    def _get_order_key(self):
        """Get the order list key for the current prefix value.

        Returns:
            str: the first character of the current prefix value

        """
        return self._value[0] if self._value else self._value

    def __str__(self):
        """Convert the BytePrefix object into a string.

        Returns:
            str: the unit

        """
        return self._value

    def __repr__(self):
        """Represent the BytePrefix object as a string.

        Returns:
            str: the unit

        """
        return self.__str__()

    def __eq__(self, other):
        """Determine if this object is equal to the other object.

        Args:
            other (BytePrefix): the other ByteUnit object to compare

        Returns:
            bool: whether this object equals the other object

        """
        return self.__str__() == str(other)

    def __ne__(self, other):
        """Determine if this object is not equal to the other object.

        Args:
            other (BytePrefix): the other ByteUnit object to compare

        Returns:
            bool: whether this object does not equal the other object

        """
        return not self.__eq__(other)

    def __lt__(self, other):
        """Determine if this object is less than the other object.

        Args:
            other (BytePrefix): the other Bytes object to compare

        Returns:
            bool: whether this object is less than the other object

        """
        return self.order < other.order and self.base < other.base

    def __le__(self, other):
        """Determine if this object is less than or equal to the other object.

        Args:
            other (BytePrefix): the other ByteUnit object to compare

        Returns:
            bool: whether this object is less than or equal to the other object

        """
        return self.__lt__(other) or self.__eq__(other)

    def __gt__(self, other):
        """Determine if this object is greater than the other object.

        Args:
            other (BytePrefix): the other ByteUnit object to compare

        Returns:
            bool: whether this object is greater than the other object

        """
        return not self.__lt__(other) and not self.__eq__(other)

    def __ge__(self, other):
        """Determine if this object is greater or equal to the other object.

        Args:
            other (ByteUnit): the other ByteUnit object to compare

        Returns:
            bool: whether this object is greater than or equal to the other
                object

        """
        return not self.__lt__(other) or self.__eq__(other)


class Bytes(object):
    """Defines a decimal byte capacity with its units.

    Allows comparison values with different units, e.g. '1.6T' to '750G'.
    """

    def __init__(self, amount, units):
        """Initialize a Bytes object.

        Args:
            amount (object): number of bytes relative to the unit specified
            units (str): units as a single letter
        """
        self._amount = None
        self._units = None

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
        try:
            self._amount = float(value)
        except ValueError as error:
            raise ValueError("Invalid Bytes.amount: {}".format(error))

    @units.setter
    def units(self, value):
        """Set the units amount.

        Args:
            units (str, BytePrefix): units to assign

        Raises:
            ValueError: if the value is not a supported BytePrefix value

        """
        self._units = BytePrefix(value)

    def __str__(self):
        """Convert the Bytes object into a string.

        Returns:
            str: the byte amount and its units

        """
        amount = "{:f}".format(self.amount)
        while "." in amount and (amount.endswith("0") or amount.endswith(".")):
            amount = amount[:-1]
        return "".join((amount, str(self.units)))

    def __repr__(self):
        """Represent the Byte object as a string.

        Returns:
            str: the byte amount and its units

        """
        return self.__str__()

    def __eq__(self, other):
        """Determine if this object is equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object equals the other object

        """
        if not isinstance(other, Bytes):
            other = Bytes(other, None)

        # Convert the amounts to the smallest units
        convert_units = self.units
        if self.units > other.units:
            convert_units = other.units
        amount_a = self.get_converted_amount(convert_units)
        amount_b = other.get_converted_amount(convert_units)

        # Compare amounts of the same unit
        return amount_a == amount_b

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
        if not isinstance(other, Bytes):
            other = Bytes(other, None)

        # Convert the amounts to the smallest units
        convert_units = self.units
        if self.units > other.units:
            convert_units = other.units
        amount_a = self.get_converted_amount(convert_units)
        amount_b = other.get_converted_amount(convert_units)

        # Compare amounts of the same unit
        return amount_a < amount_b

    def __le__(self, other):
        """Determine if this object is less than or equal to the other object.

        Args:
            other (Bytes): the other Bytes object to compare

        Returns:
            bool: whether this object is less than or equal to the other object

        """
        return self.__eq__(other) or self.__lt__(other)

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

    def __add__(self, other):
        """Add to the Bytes.amount.

        Args:
            other (Bytes, int, float): amount to add

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the added amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = self.amount + other.amount
        elif self.units < other.units:
            size = self.amount + other.get_converted_amount(self.units)
        else:
            size = self.get_converted_amount(other.units) + other.amount
            units = other.units

        return Bytes(size, units)

    def __sub__(self, other):
        """Subtract the Bytes.amount.

        Args:
            other (Bytes, int, float): amount to subtract

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the subtracted amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = self.amount - other.amount
        elif self.units < other.units:
            size = self.amount - other.get_converted_amount(self.units)
        else:
            size = self.get_converted_amount(other.units) - other.amount
            units = other.units

        return Bytes(size, units)

    def __mul__(self, other):
        """Multiply the Bytes.amount.

        Args:
            other (Bytes, int, float): amount to multiply

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the multiplied amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = self.amount * other.amount
        elif self.units < other.units:
            size = self.amount * other.get_converted_amount(self.units)
        else:
            size = self.get_converted_amount(other.units) * other.amount
            units = other.units

        return Bytes(size, units)

    def __div__(self, other):
        """Divide the Bytes.amount.

        Args:
            other (Bytes, int, float): amount by which to divide

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the divided amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = self.amount / other.amount
        elif self.units < other.units:
            size = self.amount / other.get_converted_amount(self.units)
        else:
            size = self.get_converted_amount(other.units) / other.amount
            units = other.units

        return Bytes(size, units)

    def __radd__(self, other):
        """Perform reflective addition on the Bytes object.

        Args:
            other (Bytes, int, float): amount to add

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the reflective added amount

        """
        return self.__add__(other)

    def __rsub__(self, other):
        """Perform reflective subtraction on the Bytes object.

        Args:
            other (Bytes, int, float): amount from which to be subtracted

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the reflective subtracted amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = other.amount - self.amount
        elif self.units < other.units:
            size = other.get_converted_amount(self.units) - self.amount
        else:
            size = other.amount - self.get_converted_amount(other.units)
            units = other.units

        return Bytes(size, units)

    def __rmul__(self, other):
        """Perform reflective multiplication on the Bytes object.

        Args:
            other (Bytes, int, float): amount to multiply

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the reflective multiplied amount

        """
        return self.__mul__(other)

    def __rdiv__(self, other):
        """Perform reflective division on the Bytes object.

        Args:
            other (Bytes, int, float): amount to be divided

        Raises:
            TypeError: if an unsupported type is specified

        Returns:
            Bytes: a new Bytes object with the reflective divided amount

        """
        if not isinstance(other, Bytes):
            # Support operations with other object types (e.g. int)
            other = Bytes(other, None)

        units = self.units
        if self.units == other.units:
            size = other.amount / self.amount
        elif self.units < other.units:
            size = other.get_converted_amount(self.units) / self.amount
        else:
            size = other.amount / self.get_converted_amount(other.units)
            units = other.units

        return Bytes(size, units)

    def get_converted_amount(self, unit):
        """Get the current amount converted to the specified units.

        Args:
            unit (str, BytePrefix): unit to which to convert

        Raises:
            ValueError: if the unit is not valid

        Returns:
            float: the converted amount

        """
        if not isinstance(unit, BytePrefix):
            try:
                unit = BytePrefix(unit)
            except ValueError as error:
                raise ValueError(
                    "Invalid 'unit' {}: {}".format(unit, error))

        power = self.units.order - unit.order
        return self.amount * (self.units ** power)

    def convert(self, unit):
        """Convert the current Bytes object to a different unit.

        Args:
            unit (str): unit to which to convert

        Raises:
            ValueError: if the unit is not valid

        """
        log = getLogger()
        original = self.__str__()
        self.amount = self.get_converted_amount(unit)
        self.units = unit
        log.debug("Bytes conversion: %sB -> %sB", original, str(self))

    def convert_down(self):
        """Decrement the units until the amount is greater than 1000."""
        log = getLogger()
        while self.amount < 1000 and self.units.order != 0:
            previous_unit = self.units.ORDER[self.units.order - 1]
            self.convert(previous_unit)
        log.debug("Updated Bytes: %s", str(self))

    def convert_up(self):
        """Increment the units until the amount is less than 1000."""
        log = getLogger()
        while self.amount > 1000 and self.units != self.units.ORDER[-1]:
            next_unit = self.units.ORDER[self.units.order + 1]
            self.convert(next_unit)
        log.debug("Updated Bytes: %s", str(self))
