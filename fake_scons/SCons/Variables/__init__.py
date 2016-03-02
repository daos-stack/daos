"""Fake Variables class"""
class PathVariable(object):
    """Fake PathVariable"""
    PathIsDirCreate = 1
    PathIsDir = 2
    PathAccept = 3
    def __init__(self, *args, **kw):
        pass

__all__ = ['PathVariable']
