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
provided in Contract No. 8F-30005.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
'''

import os
import sys
import json
import time
from avocado import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import server_utils
import write_host_file
import ior_utils
import slurm_utils
import dmg_utils
from daos_api import DaosContext, DaosPool, DaosApiError


class Soak(Test):
    """
    Test class Description: DAOS Soak test cases
    """

    def job_done(self, args):
        """
        This is a callback function called when a job is done

        handle --which job, i.e. the job ID
        state  --string indicating job completion status
        """

        self.soak_results[args["handle"]] = args["state"]


    def create_pool(self):
        """
        Creates a pool that the various tests use for storage.
        """

        createmode = self.params.get("mode", '/run/pool1/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool1/createset/')
        createsize = self.params.get("size", '/run/pool1/createsize/')
        self.createsvc = self.params.get("svcn", '/run/pool1/createsvc/')

        self.pool = DaosPool(self.context)
        self.pool.create(createmode, createuid, creategid,
                         createsize, createsetid, None, None,
                         self.createsvc)

    def build_ior_script(self, job):
        """
        Builds an IOR command string which is then added to slurm script

        job --which job to read in the yaml file

        """

        # for the moment build IOR
        #IorUtils.build_ior(self.basepath)

        # read job info
        job_params = "/run/" + job + "/"
        job_name = self.params.get("name", job_params)
        job_nodes = self.params.get("nodes", job_params)
        job_processes = self.params.get("process_per_node",
                                        job_params)
        job_spec = self.params.get("jobspec", job_params)

        # read ior cmd info
        spec = "/run/" + job_spec + "/"
        iteration = self.params.get("iter", spec + 'iteration/')
        ior_flags = self.params.get("F", spec + 'iorflags/')
        transfer_size = self.params.get("t", spec + 'transfersize/')
        record_size = self.params.get("r", spec + 'recordsize/*')
        stripe_size = self.params.get("s", spec + 'stripesize/*')
        stripe_count = self.params.get("c", spec + 'stripecount/')
        async_io = self.params.get("a", spec + 'asyncio/')
        object_class = self.params.get("o", spec + 'objectclass/')

        self.partition = self.params.get("partition",
                                         '/run/hosts/test_machines/')

        pool_uuid = self.pool.get_uuid_str()
        tmplist = []
        svc_list = ""
        for i in range(self.createsvc):
            tmplist.append(int(self.pool.svc.rl_ranks[i]))
            svc_list += str(tmplist[i]) + ":"
        svc_list = svc_list[:-1]

        block_size = '1536m'

        if stripe_size == '8m':
            transfer_size = stripe_size

        hostfile = os.path.join(self.tmpdir, "ior_hosts_" + job_name)

        cmd = ior_utils.get_ior_cmd(ior_flags, iteration, block_size,
                                    transfer_size, pool_uuid, svc_list,
                                    record_size, stripe_size, stripe_count,
                                    async_io, object_class, self.basepath,
                                    hostfile, job_processes)

        output = os.path.join(self.tmpdir, job_name + "_results.out")
        script = slurm_utils.write_slurm_script(self.tmpdir, job_name,
                                                output, int(job_nodes), [cmd])
        return script

    def setUp(self):

        # intermediate results are stored in this global
        # start off with it empty
        self.soak_results = {}

        self.partition = None

        # initialize anything we rely on existing
        self.pool = None
        self.hostlist_servers = None

        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as thefile:
            build_paths = json.load(thefile)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        # workdir was not successful, not sure why right now
        self.tmpdir = self.basepath + "/install/tmp"
        try:
            os.makedirs(self.tmpdir)
        except:
            pass

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        # start the servers
        self.hostlist_servers = self.params.get("daos_servers",
                                                '/run/hosts/test_machines/*')
        filename = write_host_file.write_host_file(self.hostlist_servers,
                                                   self.workdir)
        self.server_group = self.params.get("name", '/server_config/',
                                            'daos_server')
        print("Servers {} group {} basepath {}".format(self.hostlist_servers,
                                                       self.server_group,
                                                       self.basepath))
        server_utils.run_server(filename, self.server_group, self.basepath)

        # setup the storage
        self.create_pool()

    def tearDown(self):
        server_utils.stop_server(hosts=self.hostlist_servers)

    def test_soak_1(self):
        """
        Test ID: DAOS-2192
        Test Description: This test runs 2 DAOS API IOR jobs.
        :avocado: tags=soak1
        """

        try:
            # turn job parameters into slurm script
            script1 = self.build_ior_script('job1')

            # queue it up to run and register a callback to retrieve results
            job_id1 = slurm_utils.run_slurm_script(script1)
            slurm_utils.register_for_job_results(job_id1, self, maxwait=3600)

            # queue up a second job
            script2 = self.build_ior_script('job2')
            job_id2 = slurm_utils.run_slurm_script(script2)
            slurm_utils.register_for_job_results(job_id2, self, maxwait=3600)            

            # wait for all the jobs to finish
            while len(self.soak_results) < 2:
                time.sleep(10)

            for job, result in self.soak_results.iteritems():
                if result != "COMPLETED":
                    self.fail("Soak job: {} didn't complete as expected: {}".
                              format(job, result))

        except (DaosApiError, ior_utils.IorFailed) as error:
            self.fail("<Soak Test 1 Failed>\n {}".format(error))
        finally:
            try:
                os.remove(script1)
            except StandardError:
                pass
            try:
                os.remove(script2)
            except StandardError:
                pass

    def test_soak_2(self):
        """
        Test ID: DAOS-2192
        Test Description: This test verifies that a dmg script can be submitted.
        :avocado: tags=soak2
        """

        script = None
        try:
            dmgcmds = dmg_utils.get_dmg_script("dmg1", self.params,
                                               self.basepath)

            s2_job1_name = self.params.get("name", '/run/job3/')
            s2_job1_nodes = self.params.get("nodes", '/run/job3/')

            output = os.path.join(self.tmpdir, s2_job1_name + "_results.out")

            script = slurm_utils.write_slurm_script(self.tmpdir, s2_job1_name,
                                                    output,
                                                    s2_job1_nodes, dmgcmds)
            job_id = slurm_utils.run_slurm_script(script)
            slurm_utils.register_for_job_results(job_id, self, maxwait=3600)

            # wait for all the jobs to finish
            while len(self.soak_results) < 1:
                time.sleep(10)

            for job, result in self.soak_results.iteritems():
                if result != "COMPLETED":
                    self.fail("Soak job: {} didn't complete as expected: {}".
                              format(job, result))

        except (DaosApiError, ior_utils.IorFailed) as error:
            self.fail("Soak Test 2 Failed/n {}".format(error))
        finally:
            try:
                os.remove(script)
            finally:
                pass

    def test_soak_3(self):
        """
        Test ID: DAOS-2192
        Test Description: this time try a dmg command combined with IOR run
        Use Cases:
        :avocado: tags=soak3
        """

        script1 = None
        script2 = None
        try:
            # retrieve IOR job parameters
            script1 = self.build_ior_script('job1')
            job_id1 = slurm_utils.run_slurm_script(script1)
            slurm_utils.register_for_job_results(job_id1, self, maxwait=3600)

            # now do the dmg job
            dmgcmds = dmg_utils.get_dmg_script("dmg1", self.params,
                                               self.basepath)

            s3_job2_name = self.params.get("name", '/run/job3/')
            s3_job2_nodes = self.params.get("nodes", '/run/job3/')
            output = os.path.join(self.tmpdir, s3_job2_name + "_results.out")
            script2 = slurm_utils.write_slurm_script(self.tmpdir, s3_job2_name,
                                                     output, s3_job2_nodes,
                                                     dmgcmds)
            job_id2 = slurm_utils.run_slurm_script(script2)
            slurm_utils.register_for_job_results(job_id2, self, maxwait=3600)

            # wait for all the jobs to finish
            while len(self.soak_results) < 2:
                time.sleep(10)

            for job, result in self.soak_results.iteritems():
                if result != "COMPLETED":
                    self.fail("Soak job: {} didn't complete as expected: {}".
                              format(job, result))

        except (DaosApiError, ior_utils.IorFailed) as error:
            self.fail("Soak Test 3 Failed\n {}".format(error))
        finally:
            try:
                os.remove(script1)
            except StandardError:
                pass
            try:
                os.remove(script2)
            except StandardError:
                pass
