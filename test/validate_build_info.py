"""A simple script to exercise the BuildInfo module"""
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))
from build_info import BuildInfo

FILENAME = os.path.join(SCRIPT_DIR, "sl_test.info")

INFO = BuildInfo(FILENAME)

PREFIX = INFO.get("PREFIX")
if not os.path.exists(PREFIX):
    print "PREFIX doesn't exist"
    os.unlink(FILENAME)
    sys.exit(-1)

if not SCRIPT_DIR in PREFIX:
    print "PREFIX not at expected location"
    os.unlink(FILENAME)
    sys.exit(-1)

if not os.path.exists(INFO.get("HWLOC_PREFIX")):
    print "No hwloc directory"
    os.unlink(FILENAME)
    sys.exit(-1)

PREFIX = INFO.get("HWLOC2_PREFIX")
if not os.path.exists(PREFIX):
    print "HWLOC2_PREFIX doesn't exist"
    os.unlink(FILENAME)
    sys.exit(-1)

if not SCRIPT_DIR in PREFIX:
    print "HWLOC2_PREFIX not at expected location"
    os.unlink(FILENAME)
    sys.exit(-1)

SH_SCRIPT = os.path.join(SCRIPT_DIR, "sl_test.sh")
INFO.gen_script(SH_SCRIPT)
os.system("source %s"%SH_SCRIPT)
os.unlink(SH_SCRIPT)

os.unlink(FILENAME)
