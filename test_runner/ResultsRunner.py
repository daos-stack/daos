#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Fill in a subtest_results.yml file From the provided information as
Test Runner is processing a description file.

  Tests:
      name: test set
      status: PASS/FAIL
      submission: Tue Dec 01 16:36:46 PDT 2015
      report_version: 2
      duration: time
      SubTests:
          name: test name
          status: PASS/FAIL
          submission: Tue Dec 01 16:36:46 PDT 2015
          duration: time

"""
import logging
from datetime import datetime
from time import time
from yaml import dump
try:
    from yaml import CDumper as Dumper
except ImportError:
    from yaml import Dumper


class SubTestResults():
    """ subtest results object"""

    def __init__(self, base_dir, name=""):
        """save base directory"""
        self.logger = logging.getLogger("TestRunnerLogger")
        self.index = -1
        self.results_list = []
        self.base_dir = base_dir
        if name:
            self.add_test_set(name)

    def add_test_set(self, name):
        """add a base test set"""
        #self.index = self.index + 1
        self.index += 1
        test_base_data = {'name': name,
                          'status': "Running",
                          'duration': time(),
                          'report_version': 2,
                          'submission': datetime.now(). \
                                            strftime("%A %B %d %H:%M %Z %Y"),
                          'SubTests': []
                         }
        self.results_list.append(test_base_data)
        self.logger.log(0, "create subtest results: %s",
                        str(self.results_list))

    def test_set_name(self, loop=None):
        """return base name"""
        self.logger.log(0, "want test set for: %s", str(loop))
        if loop is None:
            loop = self.index
        self.logger.log(0, "return test set for: %d", loop)
        self.logger.log(0, "return test set name: %s",
                        self.results_list[loop]['name'])
        return self.results_list[loop]['name']

    def get_subtest_list(self, loop=0):
        """return base data"""
        self.logger.log(0, "return subtest for: %d", loop)
        self.logger.log(0, "return subtest results: %s",
                        str(self.results_list[loop]['SubTests']))
        return self.results_list[loop]['SubTests']

    def update_subtest_results(self, update_results):
        """add results to base subtest list"""
        self.logger.log(0, "subtest results %s update subtest results: %s",
                        self.index, str(update_results))
        self.results_list[self.index]['SubTests'].append(update_results)

    def update_testset_results(self, status="FAIL"):
        """add results to base subtest list """
        start_time = self.results_list[self.index]['duration']
        duration = '{:.2f}'.format(time() - start_time)
        self.results_list[self.index]['status'] = status
        self.results_list[self.index]['duration'] = duration
        self.logger.log(0, "test results update set results: %s",
                        str(self.results_list))

    def update_testset_zero(self, status="FAIL"):
        """add results to base subtest list """
        start_time = self.results_list[0]['duration']
        duration = '{:.2f}'.format(time() - start_time)
        self.results_list[0]['status'] = status
        self.results_list[0]['duration'] = duration
        self.logger.log(0, "test results update set results: %s",
                        str(self.results_list))

    def create_test_set_results(self):
        """create and return a test set ojbect"""
        self.logger.log(0, "subtest results: create subtest_results")
        self.logger.log(0, "subtest results test_data: %s",
                        str(self.results_list))
        self.logger.log(0, "subtest results test_data len: %d",
                        len(self.results_list))
        name = "%s/subtest_results.yml" % self.base_dir
        with open(name, 'w') as fd:
            dump(self.results_list, fd, Dumper=Dumper, indent=4,
                 default_flow_style=False)
