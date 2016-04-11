"""SConstruct to build all components"""
import sys
import os
sys.path.insert(0, os.path.realpath("."))
from prereq_tools import PreReqComponent
ENV = DefaultEnvironment()

OPTS = Variables()
OPTS.Add(ListVariable("REQUIRES",
                      "List of libraries to build",
                      'mercury,ompi',
                      ["mercury", "bmi", "openpa",
                       "ompi", "pmix", "hwloc",
                       "nvml", "cci"]))
OPTS.Update(ENV)
REQS = PreReqComponent(ENV, OPTS)
REQS.preload("components.py", prebuild=ENV.get("REQUIRES"))

try:
    Help(OPTS.GenerateHelpText(ENV), append=True)
except TypeError:
    Help(OPTS.GenerateHelpText(ENV))
