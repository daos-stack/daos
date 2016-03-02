"""SConstruct to build all components"""
import sys
sys.path.insert(0, ".")
from prereq_tools import PreReqComponent
ENV = DefaultEnvironment()

OPTS = Variables()
REQS = PreReqComponent(ENV, OPTS)
REQS.preload("components.py", prebuild=["mercury", "ompi"])

Help(OPTS.GenerateHelpText(ENV))

