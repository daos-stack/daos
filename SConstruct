"""SConstruct to build all components"""
import sys
sys.path.insert(0, ".")
from prereq_tools import PreReqComponent
ENV = DefaultEnvironment()

OPTS = Variables()
OPTS.Add(ListVariable("REQUIRES",
                      "List of libraries to build",
                      'mercury,ompi',
                      ["mercury", "bmi", "openpa",
                       "ompi", "pmix", "hwloc"]))
OPTS.Update(ENV)
REQS = PreReqComponent(ENV, OPTS)
REQS.preload("components.py", prebuild=ENV.get("REQUIRES"))

Help(OPTS.GenerateHelpText(ENV))

