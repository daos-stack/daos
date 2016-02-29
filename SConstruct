"""SConstruct to build all components"""
import sys
sys.path.insert(0, ".")
from prereq_tools import PreReqComponent

__ENV__ = DefaultEnvironment()

__OPTS__ = Variables()
__REQS__ = PreReqComponent(__ENV__, __OPTS__)
__REQS__.preload("components.py", prebuild=["mercury", "ompi"])

Help(__OPTS__.GenerateHelpText(__ENV__))

