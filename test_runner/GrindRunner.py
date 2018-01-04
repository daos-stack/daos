#!/usr/bin/env python3
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# -*- coding: utf-8 -*-
"""
test runner valgrind processing class

"""

import os
import subprocess

#pylint: disable=too-many-locals


class GrindRunner():
    """post test runner for valgrind"""
    last_testlogdir = ""
    test_info = None
    info = None
    logger = None

    def valgrind_memcheck(self):
        """ If memcheck is used to check the results """
        self.logger.info("TestRunner: valgrind memcheck begin")
        from xml.etree import ElementTree
        rtn = 0
        error_str = ""
        newdir = self.last_testlogdir
        if not os.path.exists(newdir):
            self.logger.info("Directory not found: %s" % newdir)
            return 1
        dirlist = os.listdir(newdir)
        for psdir in dirlist:
            dname = os.path.join(newdir, psdir)
            if os.path.isfile(dname):
                continue
            testdirlist = os.listdir(dname)
            for fname in testdirlist:
                if str(fname).endswith(".xml"):
                    with open(os.path.join(dname, fname), "r") as xmlfile:
                        tree = ElementTree.parse(xmlfile)
                    error_types = {}
                    for node in tree.iter('error'):
                        kind = node.find('./kind')
                        if not kind.text in error_types:
                            error_types[kind.text] = 0
                        error_types[kind.text] += 1
                    if error_types:
                        error_str += "test dir: %s  file: %s\n" % (psdir, fname)
                        for err in error_types:
                            error_str += "%-3d %s errors\n"%(error_types[err],
                                                             err)
        if error_str:
            self.logger.info("""
#########################################################
    memcheck TESTS failed.
%s
#########################################################
""" % error_str)
            rtn = 1
        self.logger.info("TestRunner: valgrind memcheck end")
        return rtn

    def callgrind_annotate(self):
        """ If callgrind is used pull in the results """
        self.logger.info("TestRunner: Callgrind annotate begin")
        module = self.test_info.get_module()
        srcdir = module.get('srcDir', "")
        src_rootdir = self.info.get_info('SRCDIR')
        if srcdir and src_rootdir:
            if isinstance(srcdir, str):
                srcfile = " %s/%s/*.c" % (src_rootdir, srcdir)
            else:
                srcfile = ""
                for item in srcdir:
                    srcfile += " %s/%s/*.c" % (src_rootdir, item)
            dirlist = os.listdir(self.last_testlogdir)
            for infile in dirlist:
                if os.path.isfile(infile) and infile.find(".out"):
                    outfile = infile.replace("out", "gp.out")
                    cmdarg = "callgrind_annotate " + infile + srcfile
                    self.logger.info(cmdarg)
                    with open(outfile, 'w') as out:
                        subprocess.call(cmdarg, timeout=30, shell=True,
                                        stdout=out,
                                        stderr=subprocess.STDOUT)
        else:
            self.logger.info("TestRunner: Callgrind no source directory")
        self.logger.info("TestRunner: Callgrind annotate end")
