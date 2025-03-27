#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import sys
import random


with open("config/random.cfg", "w") as f:
    f.write(str(random.randint(0, sys.maxsize)))
