#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import random
import string
import enum
from functools import reduce
from ctypes import (
    c_uint64,
    c_uint32,
    c_uint16,
    c_uint8,
    c_int,
    c_uint
)


class Range:
    def __init__(self, min_val, max_val):
        self.min = min_val
        self.max = max_val

    def is_within(self, val):
        return val >= self.min and val <= self.max


class DefaultRanges(Range, enum.Enum):
    UINT8 = 0, c_uint8(-1).value
    INT16 = int(-c_uint16(-1).value / 2) - 1, int(c_uint16(-1).value / 2)
    UINT16 = 0, c_uint16(-1).value
    UINT32 = 0, c_uint32(-1).value
    UINT64 = 0, c_uint64(-1).value
    INT = int(-c_uint(-1).value / 2) - 1, int(c_uint(-1).value / 2)


class RandomGenerator:
    def __init__(self, base_range=DefaultRanges.INT, count=1000):
        with open("config/random.cfg") as f:
            self.random = random.Random(int(f.read()))
        self.exclude = []
        self.range = base_range
        self.count = count
        self.n = 0

    def exclude_range(self, excl_range):
        self.exclude.append(excl_range)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self.n >= self.count:
            raise StopIteration()
        self.n += 1
        while True:
            val = self.random.randint(self.range.min, self.range.max)
            if self.exclude:
                excl_map = map(lambda e: e.is_within(val), self.exclude)
                is_excluded = reduce(lambda a, b: a or b, excl_map)
                if is_excluded:
                    continue
            return val


class RandomStringGenerator:
    def __init__(self, len_range=Range(0, 20), count=700, extra_chars=[]):
        with open("config/random.cfg") as f:
            self.random = random.Random(int(f.read()))
        self.generator = self.__string_generator(len_range, extra_chars)
        self.count = count
        self.n = 0

    def __string_generator(self, len_range, extra_chars):
        while True:
            for t in [string.digits,
                      string.ascii_letters + string.digits,
                      string.ascii_lowercase,
                      string.ascii_uppercase,
                      string.printable,
                      string.punctuation,
                      string.hexdigits,
                      *extra_chars]:
                yield ''.join(random.choice(t) for _ in range(
                    self.random.randint(len_range.min, len_range.max)
                ))

    def __iter__(self):
        return self

    def __next__(self):
        if self.n >= self.count:
            raise StopIteration()
        self.n += 1
        return next(self.generator)
