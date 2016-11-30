#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""walk the testRun directory and find testcase directories"""

#pylint: disable=too-few-public-methods

import sys
import os
import getopt
import logging
#pylint: disable=import-error
import PostRunner

class LoggedTestCase(PostRunner.PostRunner):
    """walk the testRun directory and find testcase directories"""
    logger = logging.getLogger("findTestLogs")
    logger.setLevel(logging.DEBUG) # or whatever you prefer
    ch = logging.StreamHandler(sys.stdout)
    logger.addHandler(ch)

if __name__ == "__main__":

    findLog = LoggedTestCase()
    startdir = ""
    startcheck = ""
    dumplogs = False
    try:
        opts, args = getopt.getopt(sys.argv[1:], 'd:l:c:',
                                   ['dumplogs=', 'logfile=', '-checkdir='])
    except getopt.GetoptError:
        sys.exit(2)

    for opt, arg in opts:
        if opt in ('-d', '--dumplogs'):
            dumplogs = True
            startdir = arg
        elif opt in ('-l', '--logfile'):
            startdir = arg
        elif opt in ('-c', '-checkdir'):
            startcheck = arg
        else:
            continue

    if os.path.exists(startdir):
        findLog.top_logdir(startdir, dumplogs)
    elif os.path.exists(startcheck):
        findLog.check_log_mode(startcheck)
    else:
        print("Directory not found: %s" % startdir)
