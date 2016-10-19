#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""walk the testRun directory and find testcase directories"""
import sys
import os

def testcase_logdir(logdir):
    """walk the testcase directory and find stdout and stderr files"""
    # testcase directories
    dirlist = sorted(os.listdir(logdir), reverse=True)
    for psdir in dirlist:
        dname = os.path.join(logdir, psdir)
        if os.path.isfile(dname):
            continue
        rankdirlist = sorted(os.listdir(dname), reverse=True)
        # rank directories
        for rankdir in rankdirlist:
            if os.path.isfile(os.path.join(logdir, psdir, rankdir)):
                continue
            dumpstdout = os.path.join(logdir, psdir, rankdir, "stdout")
            dumpstderr = os.path.join(logdir, psdir, rankdir, "stderr")
            print("*******************************************************")
            print("Log file %s\n %s\n %s" % (rankdir, dumpstdout, dumpstderr))
            print("*******************************************************")

def top_logdir(newdir):
    """walk the testRun directory and find testcase directories"""
    # testRun directory
    dirlist = sorted(os.listdir(newdir), reverse=True)
    # test loop directories
    for loopdir in dirlist:
        loopname = os.path.join(newdir, loopdir)
        if os.path.isfile(loopname):
            continue
        print("*******************************************************")
        print("%s" % loopdir)
        iddirlist = sorted(os.listdir(loopname), reverse=True)
        # test id directories
        for iddir in iddirlist:
            idname = os.path.join(loopname, iddir)
            if os.path.isfile(idname):
                continue
            print("*******************************************************")
            print("%s" % iddir)
            tcdirlist = sorted(os.listdir(idname), reverse=True)
            for tcdir in tcdirlist:
                tcname = os.path.join(idname, tcdir)
                if os.path.isfile(tcname):
                    continue
                print("*******************************************************")
                print("%s" % tcdir)
                testcase_logdir(tcname)


if __name__ == "__main__":
    startdir = sys.argv[1]
    if os.path.exists(startdir):
        top_logdir(startdir)
    else:
        print("Directory not found: %s" % startdir)
