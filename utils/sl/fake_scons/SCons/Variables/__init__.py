# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""Fake Variables class"""


class PathVariable():
    """Fake PathVariable"""
    PathIsDirCreate = 1
    PathIsDir = 2
    PathAccept = 3

    def __init__(self, *args, **kw):
        pass


class ListVariable():
    """Fake ListVariable"""

    def __init__(self, *args, **kw):
        pass


class BoolVariable():
    """Fake BoolVariable"""

    def __init__(self, *args, **kw):
        pass


class EnumVariable():
    """Fake EnumVariable"""

    def __init__(self, *args, **kw):
        pass

ARGUMENTS = {}

__all__ = ['PathVariable',
           'ListVariable',
           'BoolVariable',
           'EnumVariable',
           'ARGUMENTS']
