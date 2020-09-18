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

    def __init__(self, *args, **kw):
        """init function"""


class DefaultEnvironment():
    """Default environment"""

    def __init__(self, *args, **kwargs):
        """constructor"""

    def RunTests(self, *args, **kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def RunMemcheckTests(self, *args, **kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def RunHelgrindTests(self, *args, **kw):
        """Fake tests builder (defined by prereq_tools)"""
        return []

    def CFile(self, *args, **kw):
        """Fake CFile"""
        return []

    def WhereIs(self, *args, **kw):
        """Fake WhereIs"""
        return []

    def Java(self, *args, **kw):
        """Fake Java"""
        return []

    def Jar(self, *args, **kw):
        """Fake Jar"""
        return []

    def StaticLibrary(self, *args, **kw):
        """Fake StaticLibrary"""
        return []

    def M4(self, *args, **kw):
        """Fake M4"""
        return []

    def DVI(self, *args, **kw):
        """Fake DVI"""
        return []

    def Zip(self, *args, **kw):
        """Fake Zip"""
        return []

    def CXXFile(self, *args, **kw):
        """Fake CXXFile"""
        return []

    def InstallAs(self, *args, **kw):
        """Fake InstallAs"""
        return []

    def InstallVersionedLib(self, *args, **kw):
        """Fake InstallVersionedLib"""
        return []

    def RPCGenHeader(self, *args, **kw):
        """Fake RPCGenHeader"""
        return []

    def RPCGenXDR(self, *args, **kw):
        """Fake RPCGenXDR"""
        return []

    def JavaClassDir(self, *args, **kw):
        """Fake JavaClassDir"""
        return []

    def LoadableModule(self, *args, **kw):
        """Fake LoadableModule"""
        return []

    def JavaFile(self, *args, **kw):
        """Fake JavaFile"""
        return []

    def Command(self, *args, **kw):
        """Fake Command"""
        return []

    def CopyAs(self, *args, **kw):
        """Fake CopyAs"""
        return []

    def JavaH(self, *args, **kw):
        """Fake JavaH"""
        return []

    def CopyTo(self, *args, **kw):
        """Fake CopyTo"""
        return []

    def PDF(self, *args, **kw):
        """Fake PDF"""
        return []

    def StaticObject(self, *args, **kw):
        """Fake StaticObject"""
        return []

    def Gs(self, *args, **kw):
        """Fake Gs"""
        return []

    def Tar(self, *args, **kw):
        """Fake Tar"""
        return []

    def JavaClassFile(self, *args, **kw):
        """Fake JavaClassFile"""
        return []

    def RPCGenService(self, *args, **kw):
        """Fake RPCGenService"""
        return []

    def RPCGenClient(self, *args, **kw):
        """Fake RPCGenClient"""
        return []

    def Literal(self, *args, **kw):
        """Fake Literal"""
        return []

    def Library(self, *args, **kw):
        """Fake Library"""
        return []

    def RMIC(self, *args, **kw):
        """Fake RMIC"""
        return []

    def PostScript(self, *args, **kw):
        """Fake PostScript"""
        return []

    def Rpm(self, *args, **kw):
        """Fake Rpm"""
        return []

    def Program(self, *args, **kw):
        """Fake Program"""
        return []

    def Alias(self, *args, **kw):
        """Fake Alias"""
        return []

    def __getitem__(self, x):
        """Fake __getitem__"""
        return []

    def Install(self, *args, **kw):
        """Fake Install"""

    def SharedLibrary(self, *args, **kw):
        """Fake SharedLibrary"""
        return []

    def SharedObject(self, *args, **kw):
        """Fake SharedObject"""
        return []

    def Object(self, *args, **kw):
        """Fake Object"""
        return []

    def Replace(self, *args, **kw):
        """Fake Replace"""

    def Clone(self, *args, **kw):
        """Fake Replace"""
        return copy.copy(self)

    def Append(self, *args, **kw):
        """Fake Append"""

    def AppendUnique(self, *args, **kw):
        """Fake Append Unique"""

    def AppendIfSupported(self, *args, **kw):
        """Fake AppendIfSupported"""

    def subst(self, val):
        """Fake subst"""
        return val

    def get(self, var, *args, **kw):
        """Fake get"""
        return var

    def GetOption(self, *args, **kw):
        """Fake GetOption"""
        return []


class Variables():
    """Fake variables"""

    def __init__(self, *args, **kw):
        """constructor"""

    def Add(self, *args, **kw):
        """Fake Add function"""

    def Update(self, *args, **kw):
        """Fake Update function"""

    def GenerateHelpText(self, *args, **kw):
        """Fake GenerateHelpText"""

    def UnknownVariables(self, *args, **kw):
        """Fake UnknownVariables"""

    def Save(self, *args, **kw):
        """Fake Save"""


class Configure():
    """Fake Configure"""
    def __init__(self, *args, **kw):
        self.env = DefaultEnvironment()
        """constructor"""

    def CheckHeader(self, *args, **kw):
        """Fake CheckHeader"""
        return True

    def CheckLib(self, *args, **kw):
        """Fake CheckLib"""
        return True

    def CheckLibWithHeader(self, *args, **kw):
        """Fake CheckLibWithHeader"""
        return True

    def CheckStructMember(self, *args, **kw):
        """Fake CheckStructMember"""
        return True

    def CheckCmockaSkip(self, *args, **kw):
        """Fake CheckCmockaSkip"""
        return True

    def CheckProg(self, *args, **kw):
        """Fake CheckProg"""
        return True

    def CheckFunc(self, *args, **kw):
        """Fake CheckFunc"""
        return True

    def CheckFlag(self, *args, **kw):
        """Fake CheckFlag"""
        return True

    def Finish(self):
        """Fake finish"""


class Literal():
    """Fake Literal"""

    def __init__(self, *args, **kw):
        """constructor"""

class Dir():
    """Fake Dir"""
    def __init__(self, *args, **kw):
        self.abspath = os.getcwd()
        self.path = os.getcwd()

    def srcnode(self):
        """Fake srcnode"""
        return self


def VariantDir(*args, **kw):
    """Fake VariantDir"""


def AddOption(*args, **kw):
    """Fake AddOption"""
    return True


def GetOption(*args, **kw):
    """Fake GetOption"""
    return []


def SetOption(*args, **kw):
    """Fake SetOption"""
    return True


class Help():
    """Fake Help"""
    def __init__(self, *args, **kw):
        """constructor"""


def Glob(*args):
    """Fake Glob"""
    return []


def Exit(status):
    """Fake Exit"""
    sys.exit(status)


def Import(*args):
    """Fake Import"""


def Export(*args):
    """Fake Export"""


def Default(*args):
    """Fake Default"""

def Delete(*args, **kw):
    """Fake Delete"""
    return ["fake"]

def AlwaysBuild(*args):
    """Fake AlwaysBuild"""

def Copy(*args, **kw):
    """Fake Copy"""
    return ["fake"]

def Command(*args, **kw):
    """Fake Command"""
    return ["fake"]


def Builder(*args, **kw):
    """Fake Builder"""
    return ["fake"]


def Platform():
    """Fake Platform"""
    return ''


def Depends(*args, **kw):
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
