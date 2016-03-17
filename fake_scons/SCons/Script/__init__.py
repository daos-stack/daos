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

    def Program(self, *args, **kw):
        """Fake Program"""
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

def AddOption(*args, **kw):
    """Fake AddOption"""
    return True

def GetOption(*args, **kw):
    """Fake GetOption"""
    return True

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
           'Command']

