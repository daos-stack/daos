#
# Copyright(c) 2019-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import os
import sys
import pytest
import gc

sys.path.append(os.path.join(os.path.dirname(__file__), os.path.pardir))
from pyocf.types.logger import LogLevel, DefaultLogger, BufferLogger
from pyocf.types.volume import Volume, ErrorDevice
from pyocf.types.ctx import get_default_ctx


def pytest_configure(config):
    sys.path.append(os.path.join(os.path.dirname(__file__), os.path.pardir))


@pytest.fixture()
def pyocf_ctx():
    c = get_default_ctx(DefaultLogger(LogLevel.WARN))
    c.register_volume_type(Volume)
    c.register_volume_type(ErrorDevice)
    yield c
    c.exit()
    gc.collect()


@pytest.fixture()
def pyocf_ctx_log_buffer():
    logger = BufferLogger(LogLevel.DEBUG)
    c = get_default_ctx(logger)
    c.register_volume_type(Volume)
    c.register_volume_type(ErrorDevice)
    yield logger
    c.exit()
    gc.collect()
