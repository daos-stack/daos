#!/usr/bin/python
"""
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
"""
from __future__ import print_function

import os
import time
from apricot import TestWithServers
from daos_api import DaosPool, DaosApiError
from ior_utils import IorCommand
import slurm_utils


class Soak(TestWithServers):
    """Execute DAOS Soak test cases.

    :avocado: recursive
    Args:
        TestWithServers (AvocadoTest): Unit Test test cases
    There are currently two types of soak tests.
        1) smoke - runs each specified cmdline (job spec) for a single
           iteration. The smoke test is to verify the environment is
           configured properly before running the longer soaks
        2) 1 hour - this will run a defined set of jobs and continue to submit
           the jobs until the time has expired.

    Soak Test will require a Slurm partition that contains the client nodes
    that are listed in the yaml file.

    The tests also use an IOR that is compiled with MPICH and is built with
    both the DAOS and MPI-IO drivers.

    """

    def job_done(self, args):
        """Call this function when a job is done.

        Args:
            args (list):handle --which job, i.e. the job ID,
                        state  --string indicating job completion status
        """
        self.soak_results[args["handle"]] = args["state"]

    def create_pool(self, pool):
        """Create a pool that the various tests use for storage.

        Args:
            pool (str): pool from pool list from yaml file; /run/{pool}/
        Returns:
            list: [pool_cxt, pool_uuid, svc_list, createsize]

        """
        # Create all specified pools and save object, UUID, svcl, size in dict

        pool_param = "/run/" + pool + "/"
        createmode = self.params.get("mode_RW", pool_param + "createmode/")
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", pool_param + "createset/")
        createsize = self.params.get("size", pool_param + "createsize/")
        createsvc = self.params.get("svcn", pool_param + "createsvc/")

        print("<<Creating pool {} with size {} svcn {} >>".format(
            createsetid, createsize, createsvc))
        try:
            pool_cxt = DaosPool(self.context)
            pool_cxt.create(
                createmode, createuid, creategid, createsize,
                createsetid, None, None, createsvc)
        except DaosApiError as excep:
            print(excep)
            self.fail("Pool.create failed.\n")

        pool_uuid = pool_cxt.get_uuid_str()

        svc_int_list = [
            int(pool_cxt.svc.rl_ranks[item]) for item in range(createsvc)]
        svc_list = ":".join([str(item) for item in svc_int_list])

        self.pool_attr = [pool_cxt, pool_uuid, svc_list, createsize]
        return self.pool_attr

    def destroy_pool(self):
        """Destroy the specified pool - TO DO."""
        pass

    def create_ior_cmdline(self, job_params, job_spec, pool_attr):
        """Create an IOR cmdline to run in slurm batch.

        Args:
            job_params (str): job params from yaml file
            job_spec (str): specific ior job to run
            pool_attr (list): list of [pool_ctx, uuid, svc_list, size]

        Returns:
            cmd: cmdline string

        """
        # TODO blocksize will need to be poolsize/(#nodes*#task) per job
        iteration = self.test_iteration
        ior_params = "/run/" + job_spec + "/"
        if self.test_iteration < 0:
            iteration = 1000000
        ior_cmd = IorCommand()
        ior_cmd.set_params(self, ior_params)
        ior_cmd.block_size.value = self.params.get(
            "blocksize", job_params + "*")
        ior_cmd.repetitions.value = iteration
        ior_cmd.max_duration.value = self.params.get("time", job_params + '*')
        ior_cmd.daos_pool.value = pool_attr[1]
        ior_cmd.daos_svcl.value = pool_attr[2]
        ior_cmd.daos_cont.value = "`uuidgen`"
        ior_cmd.segment_count.value = 1

        command = ior_cmd.__str__()
        if ior_cmd.api.value == "MPIIO":
            env = {
                "CRT_ATTACH_INFO_PATH": os.path.join(
                    self.basepath, "install/tmp"),
                "DAOS_POOL": str(ior_cmd.daos_pool.value),
                "MPI_LIB": "",
                "DAOS_SVCL": str(ior_cmd.daos_svcl.value),
                "DAOS_SINGLETON_CLI": 1,
                "FI_PSM2_DISCONNECT": 1,
            }
            export_cmd = [
                "export {}={}".format(key, val) for key, val in env.items()]
            command = "; ".join(export_cmd + command)
        print("<<IOR cmdline >>: {}".format(command))
        return command

    def create_dmg_cmdline(self, job_params, job_spec):
        """Create a dmg cmdline to run in slurm batch.

        Args:
            job_params (str): job params from yaml file
            job_spec (str): specific dmg job to run
        Returns:
            cmd: [description]

        """
        cmd = ""
        return cmd

    def build_job_script(self, nodesperjob, job, pool_attr, loop=""):
        """Create a slurm batch script that will execute a list of jobs.

        Args:
            job(str): the job that will be defined in the slurm script with
            /run/"job"/.  It is currently defined in the yaml as:
            Example job:
            job1:
                name: job1    - unique name
                time: 10      - cmdline time in seconds; used in IOR -T param
                tasks: 1      - number of processes per node --ntaskspernode
                blocksize: '4G' - need to take into account #nodes, #ntasks
                partition: client
                jobspec:
                    - ior1
                    - ior2

        Returns:
            script_list: list of slurm batch scripts

        """
        print("<<Build Script for job {} >> at {}".format(job, time.ctime()))

        script_list = []
        # read job info
        # for now create one script per cmdline
        job_params = "/run/" + job + "/"
        job_name = self.params.get("name", job_params + "*")
        job_specs = self.params.get("jobspec", job_params + "*")
        self.tasks = self.params.get("tasks", job_params + "*")
        job_time = self.params.get("time", job_params + "*")
        # job_time in minutes:seconds format
        job_time = str(job_time) + ":00"
        for job_spec in job_specs:
            if "ior" in job_spec:
                # Create IOR cmdline
                cmd = self.create_ior_cmdline(job_params, job_spec, pool_attr)
            elif "dmg" in job_spec:
                # create dmg cmdline
                cmd = self.create_dmg_cmdline(job_params, job_spec)
            else:
                self.fail("Soak job: {} Job spec {} is invalid".format(
                    job, job_spec))

            # a single cmdline per batch job; so that a failure is per cmdline
            # change to multiple cmdlines per batch job  later.

            output = os.path.join(self.tmp, self.test_name + "_" + job_name
                                  + "_" + job_spec + "_results.out")
            # additional sbatch params
            num_tasks = nodesperjob * self.tasks
            sbatch = {"partition": self.client_partition,
                      "ntasks-per-node": self.tasks,
                      "ntasks": num_tasks,
                      "time": job_time}
            script = slurm_utils.write_slurm_script(self.tmp, job_name,
                                                    output,
                                                    nodesperjob,
                                                    [cmd], sbatch)
            script_list.append(script)

        return script_list

    def job_setup(self, test_param, pool_attr):
        """Create the slurm job batch script .

        Args:
            test_param (str): test params from yaml file
            pool_attr (list): pool attributes

        Returns:
            scripts: list of slurm batch scripts

        """
        # test params
        self.test_name = self.params.get("name", test_param)
        self.test_iteration = self.params.get("test_iteration", test_param)
        self.job_list = self.params.get("joblist", test_param + "*")
        self.nodesperjob = self.params.get("nodesperjob", test_param)
        self.soak_results = {}
        script_list = []

        # nodesperjob = -1 indicates to use all nodes in client partition
        if self.nodesperjob < 0:
            self.nodesperjob = len(self.hostlist_clients)

        if len(self.hostlist_clients)/self.nodesperjob < 1:
            self.fail("There are not enough client nodes {} for this job. "
                      "It requires {}".format(len(self.hostlist_clients),
                                              self.nodesperjob))

        print("<<Starting soak {} >> at {}".format(self.test_name,
                                                   time.ctime()))

        # queue up slurm script and register a callback to retrieve
        # results.  The slurm batch script are single cmdline for now.
        # scripts is a list of slurm batch scripts with a single cmdline
        for job in self.job_list:
            scripts = self.build_job_script(self.nodesperjob, job, pool_attr)
            script_list.extend(scripts)
        return script_list

    def job_startup(self, scripts, loop=""):
        """Submit job batch script.

        Args:
            scripts (list): list of slurm batch scripts to submit to queue
            loop (int): number of passes completed for soak tests
        Returns:
            job_id_list: IDs of each job submitted to slurm.

        """
        job_id_list = []
        # scripts is a list of batch script files
        for script in scripts:
            # logfile = script + ".results_%j_%t_" + loop
            job_id = slurm_utils.run_slurm_script(str(script))
            print(
                "<<Job {} started with {} >> at {}".format(
                    job_id, script, time.ctime()))
            slurm_utils.register_for_job_results(
                job_id, self, maxwait=self.test_timeout)
            # keep a list of the job_id's
            job_id_list.append(int(job_id))
        return job_id_list

    def job_cleanup(self, job_id_list):
        """Wait for job completion and cleanup.

        Args:
            job_id_list: IDs of each job submitted to slurm
        """
        # wait for all the jobs to finish
        while len(self.soak_results) < len(job_id_list):
            # print("<<Waiting for results {} >>".format(self.soak_results))
            time.sleep(10)
        # check for job COMPLETED and remove it from the job queue
        for job, result in self.soak_results.items():
            if result == "COMPLETED":
                job_id_list.remove(int(job))
            else:
                print("<< Job {} failed with status {}>>".format(job, result))
        if len(job_id_list) > 0:
            print("<<Cancel jobs in queue with id's {} >>".format(job_id_list))
            for job in self.job_id_list:
                time.sleep(10)
                status = slurm_utils.cancel_jobs(int(job))
                if status:
                    print(
                        "<<Job {} successfully cancelled>>".format(job))
                else:
                    print("<<Job {} could not be killed>>".format(job))
            self.fail("<< Soak {} has failed >>".format(self.test_name))
        self.soak_results = {}

    def setUp(self):
        """Define test setup to be done."""
        print("<<setUp Started>> at {}".format(time.ctime()))
        try:
            self.client_partition = self.params.get(
                "client", "/run/partition/")
            self.server_partition = self.params.get(
                "server", "/run/partition/")
        finally:
            super(Soak, self).setUp()

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        print("<<tearDown Started>> at {}".format(time.ctime()))
        # clear out any jobs in squeue;
        try:
            if len(self.job_id_list) > 0:
                print("<<Cancel jobs in queue with ids {} >>".format(
                    self.job_id_list))
                for job_id in self.job_id_list:
                    time.sleep(10)
                    slurm_utils.cancel_jobs(job_id)
        finally:
            super(Soak, self).tearDown()

    def test_soak_smoke(self):
        """Run soak smoke.

        Test ID: DAOS-2192
        Test Description: This will create a slurm batch job that runs IOR
        with DAOS with the number of processes determined by the number of
        nodes.
        For this test a single pool will be created.  It will run for ~10 min
        :avocado: tags=soak_smoke
        """
        test_param = "/run/smoke/"
        self.pool = self.params.get("pool", test_param + "*")
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_id_list = []
        # Create all specified pools and save context, UUID, svcl, size in dict
        pool_attr = self.create_pool(self.pool)
        # Create the slurm batch scripts
        scripts = self.job_setup(test_param, pool_attr)
        # Gather the job_id for slurm batch jobs
        self.job_id_list = self.job_startup(scripts)
        # Wait for slurm jobs to finish and cancel jobs if necessary
        self.job_cleanup(self.job_id_list)

    def test_soak_1(self):
        """Run 1 hour soak.

        Test ID: DAOS-2256
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run for 1 hours.
        Initially it will run the same jobs in a loop.  As new benchmarks
        and applications are added, the soak1 will be continuous
        stream of random jobs
        :avocado: tags=soak_1
        """
        test_param = "/run/soak1/"
        self.pool = self.params.get("pool", test_param + "*")
        self.test_timeout = self.params.get("test_timeout", test_param)
        loop = 0
        self.job_id_list = []
        start_time = time.time()
        while time.time() < start_time + self.test_timeout:
            loop = loop + 1
            print("<<Soak1 pass {}: time until done {}>>".format(
                loop, (start_time + self.test_timeout - time.time())))
            # create a pool that all IOR jobs will access.
            # pool_attr = [pool_cxt, pool_uuid, svc_list, createsize]
            pool_attr = self.create_pool(self.pool)
            # Create the slurm batch scripts
            scripts = self.job_setup(test_param, pool_attr)
            # Gather the job_id for slurm batch jobs
            self.job_id_list = self.job_startup(scripts, str(loop))
            # Wait for slurm jobs to finish and cancel jobs if necessary
            self.job_cleanup(self.job_id_list)
            # pool context is pool_attr[0]
            pool_attr[0].destroy(1)
