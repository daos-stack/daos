"""Fake scons environment shutting up pylint on SCons files"""
import os
import sys
import copy

class SConscript(object):
    """Fake SConscript"""
    def __init__(self, *args, **kw):
        pass

class DefaultEnvironment(object):
    """Default environment"""
    def __init__(self):
        pass

    def CFile(self, *args, **kw):
        """Fake CFile"""
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

    def Install(self, *args, **kw):
        """Fake Install"""
        pass

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
        pass

    def Clone(self, *args, **kw):
        """Fake Replace"""
        return copy.copy(self)

    def Append(self, *args, **kw):
        """Fake Append"""
        pass

    def AppendUnique(self, *args, **kw):
        """Fake Append Unique"""
        pass

    def subst(self, val):
        """Fake subst"""
        return val

    def get(self, var):
        """Fake get"""
        return var

class Variables(object):
    """Fake variables"""
    def __init__(self, *args, **kw):
        pass

    def Add(self, *args, **kw):
        """Fake Add function"""
        pass

    def Update(self, *args, **kw):
        """Fake Update function"""
        pass

    def GenerateHelpText(self, *args, **kw):
        """Fake GenerateHelpText"""
        pass

    def UnknownVariables(self, *args, **kw):
        """Fake UnknownVariables"""
        pass

    def Save(self, *args, **kw):
        """Fake Save"""
        pass

class Configure(object):
    """Fake Configure"""
    def __init__(self, *args, **kw):
        pass

    def CheckHeader(self, *args, **kw):
        """Fake CheckHeader"""
        return True

    def CheckLib(self, *args, **kw):
        """Fake CheckLib"""
        return True

    def Finish(self):
        """Fake finish"""
        pass

class Dir(object):
    """Fake Dir"""
    def __init__(self, *args, **kw):
        pass

    def abspath(self):
        """Fake abspath"""
        return os.getcwd()

def VariantDir(*args, **kw):
    """Fake VariantDir"""
    pass

def AddOption(*args, **kw):
    """Fake AddOption"""
    return True

def GetOption(*args, **kw):
    """Fake GetOption"""
    return []

def SetOption(*args, **kw):
    """Fake SetOption"""
    return True

class Help(object):
    """Fake Help"""
    def __init__(self, *args, **kw):
        pass

def Glob(*args):
    """Fake Glob"""
    return []

def Exit(status):
    """Fake Exit"""
    sys.exit(status)

def Import(*args):
    """Fake Import"""
    pass

def Export(*args):
    """Fake Export"""
    pass

def Default(*args):
    """Fake Default"""
    pass

def AlwaysBuild(*args):
    """Fake AlwaysBuild"""
    pass

def Command(*args, **kw):
    """Fake Command"""
    return ["fake"]

def Builder(*args, **kw):
    """Fake Builder"""
    return ["fake"]

__all__ = ['DefaultEnvironment',
           'Variables',
           'Configure',
           'GetOption',
           'SetOption',
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
           'VariantDir']

