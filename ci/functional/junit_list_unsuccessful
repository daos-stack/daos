#!/usr/bin/env python3
""" list JUnit failures """

import sys
from glob import glob

from junitparser import Error, Failure, JUnitXml

for file in glob(sys.argv[1]):
    for case in JUnitXml.fromfile(file):
        try:
            if isinstance(case.result[0], (Error, Failure)):
                print(f"{case.classname}:{case.name}")
        except IndexError:
            pass
