"""SConstruct to build all components"""
import sys
import os
sys.path.insert(0, os.path.realpath("."))
from prereq_tools import PreReqComponent
ENV = DefaultEnvironment()

OPTS = Variables()
REQS = PreReqComponent(ENV, OPTS)
REQS.preload("components.py")

print REQS.get_defined_components()
OPTS.Add(ListVariable("REQUIRES",
                      "List of libraries to build",
                      'mercury,ompi',
                      REQS.get_defined_components()))
OPTS.Update(ENV)

REQS.require(ENV, *ENV.get("REQUIRES"))
try:
    Help(OPTS.GenerateHelpText(ENV), append=True)
except TypeError:
    Help(OPTS.GenerateHelpText(ENV))
