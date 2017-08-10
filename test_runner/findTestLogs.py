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

def usage():
    """usage message"""
    print("""
        python3 test_runner/findTestLogs.py <task> <top directory>
            task options:
        ('-d', '--dumplogs'): dump the application std(out|err) logs
        ('-l', '--logfile'):  print the application std(out|err) logs location
        ('-c', '--checkdir'): check/change the application std(out|err)
                              logs permissions
        """)

if __name__ == "__main__":

    findLog = LoggedTestCase()
    startdir = ""
    startcheck = ""
    dumplogs = False
    try:
        opts, args = getopt.getopt(sys.argv[1:], 'd:l:c:',
                                   ['dumplogs=', 'logfile=', 'checkdir=',
                                    'help'])
    except getopt.GetoptError as err:
        print(err)
        usage()
        sys.exit(2)

    for opt, arg in opts:
        if opt in ('-d', '--dumplogs'):
            dumplogs = True
            startdir = arg
        elif opt in ('-l', '--logfile'):
            startdir = arg
        elif opt in ('-c', '--checkdir'):
            startcheck = arg
        elif opt in '--help':
            usage()
        else:
            continue

    if os.path.exists(startdir):
        findLog.top_logdir(startdir, dumplogs)
    elif os.path.exists(startcheck):
        PostRunner.check_log_mode(startcheck)
    else:
        print("Directory not found: %s" % startdir)
