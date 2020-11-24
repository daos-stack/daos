# Copyright (c) 2016-2020 Intel Corporation
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
"""Fake scons environment shutting up pylint on SCons files"""

import os
import sys
import copy


class SConscript():
    """Fake SConscript"""

    def __init__(self, *_args, **_kw):
        """init function"""


class DefaultEnvironment():
    """Default environment"""

    def __init__(self, *_args, **_kwargs):
        """constructor"""

    def RunTests(self, *_args, **_kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def RunMemcheckTests(self, *_args, **_kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def RunHelgrindTests(self, *_args, **_kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def CFile(self, *_args, **_kw):
        """Fake CFile"""
        return []

    def WhereIs(self, *_args, **_kw):
        """Fake WhereIs"""
        return []

    def Java(self, *_args, **_kw):
        """Fake Java"""
        return []

    def Jar(self, *_args, **_kw):
        """Fake Jar"""
        return []

    def StaticLibrary(self, *_args, **_kw):
        """Fake StaticLibrary"""
        return []

    def M4(self, *_args, **_kw):
        """Fake M4"""
        return []

    def DVI(self, *_args, **_kw):
        """Fake DVI"""
        return []

    def Zip(self, *_args, **_kw):
        """Fake Zip"""
        return []

    def CXXFile(self, *_args, **_kw):
        """Fake CXXFile"""
        return []

    def InstallAs(self, *_args, **_kw):
        """Fake InstallAs"""
        return []

    def InstallVersionedLib(self, *_args, **_kw):
        """Fake InstallVersionedLib"""
        return []

    def RPCGenHeader(self, *_args, **_kw):
        """Fake RPCGenHeader"""
        return []

    def RPCGenXDR(self, *_args, **_kw):
        """Fake RPCGenXDR"""
        return []

    def JavaClassDir(self, *_args, **_kw):
        """Fake JavaClassDir"""
        return []

    def LoadableModule(self, *_args, **_kw):
        """Fake LoadableModule"""
        return []

    def JavaFile(self, *_args, **_kw):
        """Fake JavaFile"""
        return []

    def Command(self, *_args, **_kw):
        """Fake Command"""
        return []

    def CopyAs(self, *_args, **_kw):
        """Fake CopyAs"""
        return []

    def JavaH(self, *_args, **_kw):
        """Fake JavaH"""
        return []

    def CopyTo(self, *_args, **_kw):
        """Fake CopyTo"""
        return []

    def PDF(self, *_args, **_kw):
        """Fake PDF"""
        return []

    def StaticObject(self, *_args, **_kw):
        """Fake StaticObject"""
        return []

    def Gs(self, *_args, **_kw):
        """Fake Gs"""
        return []

    def Tar(self, *_args, **_kw):
        """Fake Tar"""
        return []

    def JavaClassFile(self, *_args, **_kw):
        """Fake JavaClassFile"""
        return []

    def RPCGenService(self, *_args, **_kw):
        """Fake RPCGenService"""
        return []

    def RPCGenClient(self, *_args, **_kw):
        """Fake RPCGenClient"""
        return []

    def Literal(self, *_args, **_kw):
        """Fake Literal"""
        return []

    def Library(self, *_args, **_kw):
        """Fake Library"""
        return []

    def RMIC(self, *_args, **_kw):
        """Fake RMIC"""
        return []

    def PostScript(self, *_args, **_kw):
        """Fake PostScript"""
        return []

    def Rpm(self, *_args, **_kw):
        """Fake Rpm"""
        return []

    def Program(self, *_args, **_kw):
        """Fake Program"""
        return []

    def Alias(self, *_args, **_kw):
        """Fake Alias"""
        return []

    def __getitem__(self, x):
        """Fake __getitem__"""
        return []

    def Install(self, *_args, **_kw):
        """Fake Install"""

    def SharedLibrary(self, *_args, **_kw):
        """Fake SharedLibrary"""
        return []

    def SharedObject(self, *_args, **_kw):
        """Fake SharedObject"""
        return []

    def Object(self, *_args, **_kw):
        """Fake Object"""
        return []

    def Replace(self, *_args, **_kw):
        """Fake Replace"""

    def Clone(self, *_args, **_kw):
        """Fake Replace"""
        return copy.copy(self)

    def Append(self, *_args, **_kw):
        """Fake Append"""

    def AppendUnique(self, *_args, **_kw):
        """Fake Append Unique"""

    def AppendIfSupported(self, *_args, **_kw):
        """Fake AppendIfSupported"""

    def subst(self, val):
        """Fake subst"""
        return val

    def get(self, var, *_args, **_kw):
        """Fake get"""
        return var

    def GetOption(self, *_args, **_kw):
        """Fake GetOption"""
        return []


class Variables():
    """Fake variables"""

    def __init__(self, *_args, **_kw):
        """constructor"""

    def Add(self, *_args, **_kw):
        """Fake Add function"""

    def Update(self, *_args, **_kw):
        """Fake Update function"""

    def GenerateHelpText(self, *_args, **_kw):
        """Fake GenerateHelpText"""

    def UnknownVariables(self, *_args, **_kw):
        """Fake UnknownVariables"""

    def Save(self, *_args, **_kw):
        """Fake Save"""


class Configure():
    """Fake Configure"""
    def __init__(self, *_args, **_kw):
        self.env = DefaultEnvironment()
        """constructor"""

    def CheckHeader(self, *_args, **_kw):
        """Fake CheckHeader"""
        return True

    def CheckLib(self, *_args, **_kw):
        """Fake CheckLib"""
        return True

    def CheckLibWithHeader(self, *_args, **_kw):
        """Fake CheckLibWithHeader"""
        return True

    def CheckStructMember(self, *_args, **_kw):
        """Fake CheckStructMember"""
        return True

    def CheckCmockaSkip(self, *_args, **_kw):
        """Fake CheckCmockaSkip"""
        return True

    def CheckProg(self, *_args, **_kw):
        """Fake CheckProg"""
        return True

    def CheckFunc(self, *_args, **_kw):
        """Fake CheckFunc"""
        return True

    def CheckFlag(self, *_args, **_kw):
        """Fake CheckFlag"""
        return True

    def Finish(self):
        """Fake finish"""


class Literal():
    """Fake Literal"""

    def __init__(self, *_args, **_kw):
        """constructor"""

class Dir():
    """Fake Dir"""
    def __init__(self, *_args, **_kw):
        self.abspath = os.getcwd()
        self.path = os.getcwd()

    def srcnode(self):
        """Fake srcnode"""
        return self


def VariantDir(*_args, **_kw):
    """Fake VariantDir"""


def AddOption(*_args, **_kw):
    """Fake AddOption"""
    return True


def GetOption(*_args, **_kw):
    """Fake GetOption"""
    return []


def SetOption(*_args, **_kw):
    """Fake SetOption"""
    return True


class Help():
    """Fake Help"""
    def __init__(self, *_args, **_kw):
        """constructor"""


def Glob(*_args):
    """Fake Glob"""
    return []

def Split(*_args):
    """Fake Split"""
    return []

def Exit(status):
    """Fake Exit"""
    sys.exit(status)


def Import(*_args):
    """Fake Import"""


def Export(*_args):
    """Fake Export"""


def Default(*_args):
    """Fake Default"""

def Delete(*_args, **_kw):
    """Fake Delete"""
    return ["fake"]

def AlwaysBuild(*_args):
    """Fake AlwaysBuild"""

def Copy(*_args, **_kw):
    """Fake Copy"""
    return ["fake"]

def Command(*_args, **_kw):
    """Fake Command"""
    return ["fake"]


def Builder(*_args, **_kw):
    """Fake Builder"""
    return ["fake"]


def Platform():
    """Fake Platform"""
    return ''


def Depends(*_args, **_kw):
    """Fake Depends"""


COMMAND_LINE_TARGETS = []
BUILD_TARGETS = []
DEFAULT_TARGETS = []

Environment = DefaultEnvironment

__all__ = ['DefaultEnvironment',
           'Variables',
           'Configure',
           'GetOption',
           'SetOption',
           'Depends',
           'Platform',
           'Literal',
           'Dir',
           'Help',
           'Glob',
           'Split',
           'Exit',
           'Import',
           'Export',
           'SConscript',
           'Default',
           'AlwaysBuild',
           'Command',
           'Builder',
           'AddOption',
           'VariantDir',
           'COMMAND_LINE_TARGETS',
           'BUILD_TARGETS',
           'DEFAULT_TARGETS']
