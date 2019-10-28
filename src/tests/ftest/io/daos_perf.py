#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

from apricot import TestWithServers

import write_host_file
import daos_perf_utils


class DaosPerf(TestWithServers):
    """
    Test Class Description: Tests daos_perf with different config.
    :avocado: recursive
    """

    def setUp(self):
        super(DaosPerf, self).setUp()

        # set client variables
        self.hostfile_clients = (
            write_host_file.write_host_file(self.hostlist_clients,
                                            self.workdir, None))
        # initialise daos_perf_cmd
        self.daos_perf_cmd = daos_perf_utils.DaosPerfCommand()

    def runner(self, test_path):
        """
        Common runner function to obtain and run daos_perf
        Args:
            test_path: string to depict path of params in yaml file
        """

        # daos_perf parameters
        pool_size_scm = self.params.get(
            "size", '/run/{}/pool_size/scm/'.format(test_path))
        client_processes = self.params.get(
            "np", '/run/{}/client_processes/*'.format(test_path))
        mode = self.params.get(
            "mode", '/run/{}/client_processes/*'.format(test_path))
        single_value_size = self.params.get(
            "size", '/run/{}/client_processes/*'.format(test_path))
        num_of_obj = self.params.get(
            "obj", '/run/{}/client_processes/*/value_type/*'.format(test_path))
        num_of_dkeys = self.params.get(
            "dkeys", '/run/{}/client_processes/*/value_type/*'.
            format(test_path))
        num_of_akeys = self.params.get(
            "akeys", '/run/{}/client_processes/*/value_type/*'.
            format(test_path))
        num_of_recs = self.params.get(
            "records", '/run/{}/client_processes/*/value_type/*'.
            format(test_path))
        daos_perf_flags = self.params.get(
            "F", '/run/{}/client_processes/*/value_type/*/flags/*'.
            format(test_path))
        object_class = self.params.get(
            "o", '/run/{}/client_processes/np_16/*'.format(test_path))

        try:
            # set daos_perf params
            self.daos_perf_cmd.pool_size_scm.value = pool_size_scm
            self.daos_perf_cmd.test_mode.value = mode
            self.daos_perf_cmd.flags.value = daos_perf_flags
            self.daos_perf_cmd.single_value_size.value = single_value_size
            self.daos_perf_cmd.num_of_objects.value = num_of_obj
            self.daos_perf_cmd.dkeys.value = num_of_dkeys
            self.daos_perf_cmd.akeys.value = num_of_akeys
            self.daos_perf_cmd.records.value = num_of_recs
            self.daos_perf_cmd.oclass.value = object_class

            # run daos_perf
            self.daos_perf_cmd.run(self.basepath, client_processes,
                                   self.hostfile_clients)

            # Parsing output to look for failures
            # stderr directed to stdout
            stdout = self.logdir + "/stdout"
            searchfile = open(stdout, "r")
            error_message = ["non-zero exit code", "errors",
                             "Failed", "failed"]

            for line in searchfile:
                for message in error_message:
                    if message in line:
                        self.fail("DaosPerf Test Failed with error_message: "
                                  "{}".format(line))
        except (daos_perf_utils.DaosPerfFailed) as excep:
            self.fail("<DaosPerf Test FAILED>\n {}".format(excep))

    def test_small(self):
        """
        Jira ID: DAOS-1714
        Test Description: Small daos_perf test
        Use cases: Run daos_perf in 'daos' and 'vos' modes
                   Run daos_perf using single value and array value types
                   for 'vos' mode. Also run the above config with and
                   without shuffle option '-S' of daos_perf
                   Run daos_perf using single value type for 'LARGE' and 'R2s'
                   object class. Run this config with multiple server/client
                   configuration.
        :avocado: tags=daosperf,daosperfsmall
        """
        # run test
        self.runner("daos_perf")
