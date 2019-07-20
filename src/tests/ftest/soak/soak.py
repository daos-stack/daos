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
from ior_utils import IorCommand
import slurm_utils
from general_utils import TestPool
from ClusterShell.NodeSet import NodeSet
from avocado.utils import process
import socket


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

    def create_pool(self, path):
        """Create a pool that the various tests use for storage.

        Args:
            path: pool params from yaml file  /run/pool/* is default
        Returns:
            object: TestPool object

        """
        # Create a pool
        pool = TestPool(self.context, self.log)
        pool.get_params(self, path)
        pool.create()
        self.log.info("Valid Pool UUID is %s", pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            pool.check_files(self.hostlist_servers),
            "Pool data not detected on servers")
        return pool

    def destroy_pool(self, pool):
        """Destroy the specified pool - TO DO."""
        pass

    def remote_copy(self, hostlist, remote_dir, local_dir):
        """Copy files from remote dir to local dir.

        This is a temporary method and will be replaced by
        clush in general_utils
        Args:
                hostlist (list): list of remote nodes
                remote_dir (str): remote directory of files
                local_dir (str): local directory

        Returns:
            status: bool

        """
        this_host = socket.gethostname()
        # Copy logfiles from non-empty client directories
        command = "clush -w {} -B -S \"{}\"".format(
            NodeSet.fromlist(hostlist),
            "if [ ! -z \\\"\\$(ls -A {0})\\\" ]; then "
            "scp -p -r {0}/ \\\"{1}:'{2}/'\\\" && rm -rf {0}/*; fi".format(
                remote_dir, this_host, local_dir))
        status = process.run(command, timeout=300)
        return status

    def create_ior_cmdline(self, job_params, job_spec, pool):
        """Create an IOR cmdline to run in slurm batch.

        Args:
            job_params (str): job params from yaml file
            job_spec (str): specific ior job to run
            pool (obj):   TestPool obj

        Returns:
            cmd: cmdline string

        """
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
        ior_cmd.segment_count.value = 1
        ior_cmd.set_daos_params(self.server_group, pool.pool)
        # export the user environment to test node
        exports = ["ALL"]
        if ior_cmd.api.value == "MPIIO":
            env = {
                "CRT_ATTACH_INFO_PATH": os.path.join(
                    self.basepath, "install/tmp"),
                "DAOS_POOL": str(ior_cmd.daos_pool.value),
                "MPI_LIB": "\"\"",
                "DAOS_SVCL": str(ior_cmd.daos_svcl.value),
                "DAOS_SINGLETON_CLI": 1,
                "FI_PSM2_DISCONNECT": 1,
            }
            exports.extend(
                ["{}={}".format(key, val) for key, val in env.items()])
        command = "srun -l --mpi=pmi2 --export={} {}".format(
            ",".join(exports), ior_cmd)
        self.log.debug("<<IOR cmdline >>: %s", command)
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

    def build_job_script(self, nodesperjob, job, pool):
        """Create a slurm batch script that will execute a list of jobs.

        Args:
            nodesperjob(int): number of jobs executing on each node
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
            pool (obj):   TestPool obj

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
                cmd = self.create_ior_cmdline(job_params, job_spec, pool)
            elif "dmg" in job_spec:
                # create dmg cmdline
                cmd = self.create_dmg_cmdline(job_params, job_spec)
            else:
                self.fail("Soak job: {} Job spec {} is invalid".format(
                    job, job_spec))

            # a single cmdline per batch job; so that a failure is per cmdline
            # change to multiple cmdlines per batch job  later.

            output = os.path.join(
                self.rem_pass_dir, "%N_" + self.test_name + "_" + job_name
                + "_" + job_spec + "_results.out_%j_%t_")
            # additional sbatch params
            num_tasks = nodesperjob * self.tasks
            sbatch = {
                      "ntasks-per-node": self.tasks,
                      "ntasks": num_tasks,
                      "time": job_time}
            script = slurm_utils.write_slurm_script(
                self.rem_pass_dir, job_name, output, nodesperjob,
                [cmd], sbatch)
            script_list.append(script)

        return script_list

    def job_setup(self, test_param, pool):
        """Create the slurm job batch script .

        Args:
            test_param (str): test params from yaml file
            pool (obj): TestPool obj

        Returns:
            scripts: list of slurm batch scripts

        """
        status = 0
        self.rem_pass_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        # Create the remote log directories on all client nodes
        remote_cmd = "mkdir -p {}".format(self.rem_pass_dir)
        command = "clush -w {} -B -S {}".format(
            NodeSet.fromlist(self.hostlist_clients), remote_cmd)
        status = process.run(command, timeout=300)
        if not status:
            self.fail("Failed to create log directory on clients")
        # Create local log directory
        os.makedirs(self.local_pass_dir)
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
            scripts = self.build_job_script(self.nodesperjob, job, pool)
            script_list.extend(scripts)
        return script_list

    def job_startup(self, scripts):
        """Submit job batch script.

        Args:
            scripts (list): list of slurm batch scripts to submit to queue
        Returns:
            job_id_list: IDs of each job submitted to slurm.

        """
        job_id_list = []
        # scripts is a list of batch script files
        for script in scripts:
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
            time.sleep(2)
        # check for job COMPLETED and remove it from the job queue
        for job, result in self.soak_results.items():
            # The queue seems to include status of "COMPLETING"
            # sleep to allow job to move to final state
            time.sleep(2)
            if result == "COMPLETED":
                job_id_list.remove(int(job))
            else:
                print("<< Job {} failed with status {}>>".format(job, result))
        if len(job_id_list) > 0:
            print("<<Cancel jobs in queue with id's {} >>".format(job_id_list))
            for job in self.job_id_list:
                status = slurm_utils.cancel_jobs(int(job))
                if status == 0:
                    print(
                        "<<Job {} successfully cancelled>>".format(job))
                    self.job_id_list.remove(int(job))
                else:
                    print("<<Job {} could not be killed>>".format(job))
            # Currently the test will fail on first failure
            self.fail("<< Soak {} has failed >>".format(self.test_name))
        # gather all the logfiles for this pass and cleanup test nodes
        # If there is a failure the files will again be cleaned up in Teardown
        status = self.remote_copy(
            self.hostlist_clients, self.rem_pass_dir, self.outputsoakdir)
        if status > 0:
            self.log.info(
                "Some logfiles may not be available from client node")
            return
        # cleanup files
        command = "clush -w {} -B -S rm -rf {}".format(
            NodeSet.fromlist(self.hostlist_clients), self.rem_pass_dir)
        status = process.run(command, timeout=300)

        self.soak_results = {}

    def setUp(self):
        """Define test setup to be done."""
        print("<<setUp Started>> at {}".format(time.ctime()))
        super(Soak, self).setUp()
        # Initialize loop param for all tests
        self.loop = 1
        # Create slurm client partition from hostlist_client
        self.client_partition = self.params.get("name", "/run/partition/")
        status = slurm_utils.create_slurm_partition(
            self.hostlist_clients, self.client_partition)
        if status > 0:
            self.fail("FAILED:  No client partition available for test")

        # Setup logging directories for soak
        self.log_dir = "/tmp/soak"
        self.outputsoakdir = self.outputdir + "/soak"
        # cleanup client log directories before test
        command = "clush -w {} -B -S rm -rf {}".format(
            NodeSet.fromlist(self.hostlist_clients), self.log_dir)
        status = process.run(command, timeout=300)

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        print("<<tearDown Started>> at {}".format(time.ctime()))
        # clear out any jobs in squeue;
        try:
            if len(self.job_id_list) > 0:
                print("<<Cancel jobs in queue with ids {} >>".format(
                    self.job_id_list))
                for job_id in self.job_id_list:
                    slurm_utils.cancel_jobs(job_id)
            slurm_utils.delete_slurm_partition(self.client_partition)
        finally:
            # One last attempt to copy any logfiles from client nodes
            status = self.remote_copy(
                self.hostlist_clients, self.rem_pass_dir, self.outputsoakdir)
            if not status:
                self.log.info(
                    "Some logfiles may not be available from client node")
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
        pool_path = "/run/pool/*"
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_id_list = []
        # Create all specified pools
        pool = self.create_pool(pool_path)
        # Create the slurm batch scripts
        scripts = self.job_setup(test_param, pool)
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
        pool_path = "/run/pool/*"
        # print("logdir: {}".format(self.logdir))
        # print("outputdir: {}".format(self.outputdir))
        # print("basedir: {}".format(self.basedir))
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_id_list = []
        start_time = time.time()
        while time.time() < start_time + self.test_timeout:
            print("<<Soak1 PASS {}: time until done {}>>".format(
                self.loop, (start_time + self.test_timeout - time.time())))
            # create a pool that all jobs will access.
            pool = self.create_pool(pool_path)
            # Create the slurm batch scripts
            scripts = self.job_setup(test_param, pool)
            # Gather the job_id for slurm batch jobs
            self.job_id_list = self.job_startup(scripts)
            # Wait for slurm jobs to finish and cancel jobs if necessary
            self.job_cleanup(self.job_id_list)
            # create a new pool on every pass
            pool.destroy(1)
            self.loop += 1
