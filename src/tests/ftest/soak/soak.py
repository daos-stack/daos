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
from test_utils import TestPool, TestContainer
from ClusterShell.NodeSet import NodeSet
from general_utils import pcmd
import socket


class SoakTestError(Exception):
    """Soak exception class."""


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

    def create_pool(self, pools):
        """Create a pool that the various tests use for storage.

        Args:
            pools: list of pool name from yaml file
                        /run/<test_params>/poollist/*
        Returns:
            list: list of TestPool object

        """
        pool_obj_list = []
        for pool_name in pools:
            path = "/run/" + pool_name + "/*"
            # Create a pool
            pool_obj_list.append(TestPool(self.context, self.log))
            pool_obj_list[-1].namespace = path
            pool_obj_list[-1].get_params(self)
            pool_obj_list[-1].create()
            self.log.info("Valid Pool UUID is %s", pool_obj_list[-1].uuid)

            # Check that the pool was created
            self.assertTrue(
                pool_obj_list[-1].check_files(self.hostlist_servers),
                "Pool data not detected on servers")
        return pool_obj_list

    def destroy_pool(self, pool):
        """Destroy the specified pool - TO DO."""
        pass

    def remote_copy(self, hostlist, remote_dir, local_dir):
        """Copy files from remote dir to local dir.

        Args:
                hostlist (list): list of remote nodes
                remote_dir (str): remote directory of files
                local_dir (str): local directory

        Raises:
            SoakTestError: if there is an error with the remote copy

        """
        this_host = socket.gethostname()
        result = pcmd(
            NodeSet.fromlist(hostlist),
            "if [ ! -z '$(ls -A {0})' ]; then "
            "scp -p -r {0}/ \"{1}:'{2}/'\" && rm -rf {0}/*; fi".format(
                remote_dir, this_host, local_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "Error executing remote copy: {}".format(
                    ", ".join(
                        [str(result[key]) for key in result if key != 0])))

    def create_ior_cmdline(self, job_params, job_spec, pool):
        """Create an IOR cmdline to run in slurm batch.

        Args:
            job_params (str): job params from yaml file
            job_spec (str): specific ior job to run
            pool (obj):   TestPool obj

        Returns:
            cmd: cmdline string

        """
        command = []
        iteration = self.test_iteration
        ior_params = "/run/" + job_spec + "/"

        ior_cmd = IorCommand()
        ior_cmd.namespace = ior_params
        ior_cmd.get_params(self)
        if iteration is not None and iteration < 0:
            ior_cmd.repetitions.update(1000000)
        ior_cmd.max_duration.update(self.params.get("time", job_params + '*'))
        # IOR job specs with a list of parameters; update each value
        #   transfer_size
        #   block_size
        #   daos object class
        tsize_list = ior_cmd.transfer_size.value
        bsize_list = ior_cmd.block_size.value
        oclass_list = ior_cmd.daos_oclass.value
        for b_size in bsize_list:
            ior_cmd.block_size.update(b_size)
            for o_type in oclass_list:
                ior_cmd.daos_oclass.update(o_type)
                for t_size in tsize_list:
                    ior_cmd.transfer_size.update(t_size)
                    ior_cmd.set_daos_params(self.server_group, pool)
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
                            "FI_PSM2_DISCONNECT": 1
                        }
                        exports.extend(
                            ["{}={}".format(
                                key, val) for key, val in env.items()])
                    cmd = "srun -l --mpi=pmi2 --export={} {}".format(
                        ",".join(exports), ior_cmd)
                    command.append(cmd)
                    self.log.debug("<<IOR cmdline >>: %s \n", cmd)
        return command

    def create_dmg_cmdline(self, job_params, job_spec, pool):
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
            nodesperjob(int): number of nodes executing each job
            job(str): the job that will be defined in the slurm script with
            /run/"job"/.  It is currently defined in the yaml as:
            Example job:
            job1:
                name: job1    - unique name
                time: 10      - cmdline time in seconds; used in IOR -T param
                tasks: 1      - number of processes per node --ntaskspernode
                jobspec:
                    - ior_daos
                    - ior_mpiio
            pool (obj):   TestPool obj

        Returns:
            script_list: list of slurm batch scripts

        """
        self.log.info("<<Build Script for job %s >> at %s", job, time.ctime())

        script_list = []
        # create one batch script per cmdline
        # get job params
        job_params = "/run/" + job + "/"
        job_name = self.params.get("name", job_params + "*")
        job_specs = self.params.get("jobspec", job_params + "*")
        task_list = self.params.get("tasks", job_params + "*")
        job_time = self.params.get("time", job_params + "*")

        # job_time in minutes:seconds format
        job_time = str(job_time) + ":00"
        for job_spec in job_specs:
            if "ior" in job_spec:
                # Create IOR cmdline
                cmd_list = self.create_ior_cmdline(job_params, job_spec, pool)
            elif "dmg" in job_spec:
                # create dmg cmdline
                cmd_list = self.create_dmg_cmdline(job_params, job_spec, pool)
            else:
                raise SoakTestError(
                    "<<FAILED: Soak job: {} Job spec {} is invalid>>".format(
                        job, job_spec))

            # a single cmdline per batch job; so that a failure is per cmdline
            # change to multiple cmdlines per batch job  later.
            for cmd in cmd_list:
                # additional sbatch params
                for tasks in task_list:
                    output = os.path.join(
                        self.rem_pass_dir, "%N_" + self.test_name +
                        "_" + job_name + "_" + job_spec +
                        "_results.out_%j_%t_" + str(tasks) + "_")
                    num_tasks = nodesperjob * tasks
                    sbatch = {
                            "ntasks-per-node": tasks,
                            "ntasks": num_tasks,
                            "time": job_time,
                            "partition": self.partition_clients,
                            "exclude": NodeSet.fromlist(self.hostlist_servers)}
                    script = slurm_utils.write_slurm_script(
                        self.rem_pass_dir, job_name, output, nodesperjob,
                        [cmd], sbatch)
                    script_list.append(script)
        return script_list

    def job_setup(self, test_param, pool):
        """Create the slurm job batch script .

        Args:
            test_param (str): test_params from yaml file
            pool (obj): TestPool obj

        Returns:
            scripts: list of slurm batch scripts

        """
        # Get jobmanager
        self.job_manager = self.params.get("jobmanager", "/run/*")
        # Get test params
        self.test_name = self.params.get("name", test_param)
        self.test_iteration = self.params.get("test_iteration", test_param)
        self.job_list = self.params.get("joblist", test_param + "*")
        self.nodesperjob = self.params.get("nodesperjob", test_param)
        self.soak_results = {}
        script_list = []

        self.log.info(
            "<<Job_Setup %s >> at %s", self.test_name, time.ctime())
        # Create the remote log directories from new loop/pass
        self.rem_pass_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)
        result = pcmd(
            NodeSet.fromlist(self.hostlist_clients),
            "mkdir -p {}".format(self.rem_pass_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "<<FAILED: logfile directory not"
                "created on clients>>: {}".format(", ".join(
                    [str(result[key]) for key in result if key != 0])))

        # Create local log directory
        os.makedirs(self.local_pass_dir)

        # nodesperjob = -1 indicates to use all nodes in client hostlist
        if self.nodesperjob < 0:
            self.nodesperjob = len(self.hostlist_clients)

        if len(self.hostlist_clients)/self.nodesperjob < 1:
            raise SoakTestError(
                "<<FAILED: There are only {} client nodes for this job. "
                "Job requires {}".format(
                    len(self.hostlist_clients), self.nodesperjob))
        if self.job_manager == "slurm":
            # queue up slurm script and register a callback to retrieve
            # results.  The slurm batch script are single cmdline for now.
            # scripts is a list of slurm batch scripts with a single cmdline
            for job in self.job_list:
                scripts = self.build_job_script(self.nodesperjob, job, pool)
                script_list.extend(scripts)
            return script_list
        else:
            raise SoakTestError(
                "<<FAILED: Job manager {} is not yet enabled. "
                "Job requires slurm".format(self.job_manager))

    def job_startup(self, scripts):
        """Submit job batch script.

        Args:
            scripts (list): list of slurm batch scripts to submit to queue
        Returns:
            job_id_list: IDs of each job submitted to slurm.

        """
        self.log.info(
            "<<Job Startup - %s >> at %s", self.test_name, time.ctime())
        job_id_list = []
        # scripts is a list of batch script files
        for script in scripts:
            try:
                job_id = slurm_utils.run_slurm_script(str(script))
            except slurm_utils.SlurmFailed as error:
                self.log.error(error)
                # Force the test to exit with failure
                job_id = None

            if job_id:
                print(
                    "<<Job {} started with {} >> at {}".format(
                        job_id, script, time.ctime()))
                slurm_utils.register_for_job_results(
                    job_id, self, maxwait=self.test_timeout)
                # keep a list of the job_id's
                job_id_list.append(int(job_id))
            else:
                # one of the jobs failed to queue; exit on first fail for now.
                err_msg = "Slurm failed to submit job for {}".format(script)
                job_id_list = []
                raise SoakTestError(
                    "<<FAILED:  Soak {}: {}>>".format(self.test_name, err_msg))
        return job_id_list

    def job_completion(self, job_id_list):
        """Wait for job completion and cleanup.

        Args:
            job_id_list: IDs of each job submitted to slurm
        """
        self.log.info(
            "<<Job Completion - %s >> at %s", self.test_name, time.ctime())
        # If there is nothing to do; exit
        if len(job_id_list) > 0:
            # wait for all the jobs to finish
            while len(self.soak_results) < len(job_id_list):
                # print("<<Waiting for results {} >>".format(
                #        self.soak_results))
                time.sleep(2)
            # check for job COMPLETED and remove it from the job queue
            for job, result in self.soak_results.items():
                # The queue include status of "COMPLETING"
                # sleep to allow job to move to final state
                if result == "COMPLETED":
                    job_id_list.remove(int(job))
                else:
                    self.log.info(
                        "<< Job %s failed with status %s>>", job, result)
            if len(job_id_list) > 0:
                self.log.info(
                    "<<Cancel jobs in queue with id's %s >>", job_id_list)
                for job in job_id_list:
                    status = slurm_utils.cancel_jobs(int(job))
                    if status == 0:
                        self.log.info("<<Job %s successfully cancelled>>", job)
                        # job_id_list.remove(int(job))
                    else:
                        self.log.info("<<Job %s could not be killed>>", job)
            # gather all the logfiles for this pass and cleanup test nodes
            # If there is a failure the files can be gathered again in Teardown
            try:
                self.remote_copy(
                    self.node_list, self.rem_pass_dir, self.outputsoakdir)
            except SoakTestError as error:
                self.log.info("Remote copy failed with %s", error)
            self.soak_results = {}
        return job_id_list

    def execute_jobs(self, test_param, pools):
        """Execute the overall soak test.

        Args:
            test_param (str): test_params from yaml file
            pools (list): list of TestPool obj

        Raise:
            SoakTestError

        """
        cmdlist = []
        # Setup cmdlines for jobs
        for pool in pools:
            cmdlist.extend(self.job_setup(test_param, pool))

        # Gather the job_ids
        self.job_id_list = self.job_startup(cmdlist)

        # Initialize the failed_job_list to job_list so that any
        # unexpected failures will clear the squeue in tearDown
        self.failed_job_id_list = self.job_id_list

        # Wait for jobs to finish and cancel/kill jobs if necessary
        self.failed_job_id_list = self.job_completion(self.job_id_list)

        # Test fails on first error but could use continue on error here
        if len(self.failed_job_id_list) > 0:
            raise SoakTestError(
                "<<FAILED: The following jobs failed {} >>".format(
                    " ,".join(
                        str(job_id) for job_id in self.failed_job_id_list)))

    def run_soak(self, test_param):
        """Run the soak test specified by the test params.

        Args:
            test_param (str): test_params from yaml file

        """
        pool_list = self.params.get("poollist", test_param + "*")
        self.test_timeout = self.params.get("test_timeout", test_param)
        self.job_id_list = []
        start_time = time.time()

        rank = self.params.get("rank", "/run/container_reserved/*")
        obj_class = self.params.get(
            "object_class", "/run/container_reserved/*")

        # Create the reserved pool with data
        self.pool = self.create_pool(["pool_reserved"])
        self.pool[0].connect()
        self.container = TestContainer(self.pool[0])
        self.container.namespace = "/run/container_reserved/"
        self.container.get_params(self)
        self.container.create()
        self.container.write_objects(rank, obj_class)

        while time.time() < start_time + self.test_timeout:
            print("<<Soak1 PASS {}: time until done {}>>".format(
                self.loop, (start_time + self.test_timeout - time.time())))
            # Create all specified pools
            self.pool.extend(self.create_pool(pool_list))
            try:
                self.execute_jobs(test_param, self.pool[1:])
            except SoakTestError as error:
                self.fail(error)
            errors = self.destroy_pools(self.pool[1:])
            # delete the test pools from self.pool; preserving reserved pool
            self.pool = [self.pool[0]]
            self.assertEqual(len(errors), 0, "\n".join(errors))
            self.loop += 1
            # Break out of loop if smoke
            if "smoke" in self.test_name:
                break
        # Check that the reserve pool is still allocated
        self.assertTrue(
                self.pool[0].check_files(self.hostlist_servers),
                "Pool data not detected on servers")
        # Verify the data after soak is done
        self.assertTrue(
                self.container.read_objects(),
                "Data verification error on reserved pool"
                "after SOAK completed")

    def setUp(self):
        """Define test setup to be done."""
        print("<<setUp Started>> at {}".format(time.ctime()))
        super(Soak, self).setUp()
        # Initialize loop param for all tests
        self.loop = 1

        self.failed_job_id_list = []
        # Fail if slurm partition daos_client is not defined
        if not self.partition_clients:
            raise SoakTestError(
                "<<FAILED: Partition is not correctly setup for daos "
                "slurm partition>>")
        # Check if the server nodes are in the client list;
        # this will happen when only one partition is specified
        for host_server in self.hostlist_servers:
            if host_server in self.hostlist_clients:
                self.hostlist_clients.remove(host_server)
        self.log.info(
                "<<Updated hostlist_clients %s >>", self.hostlist_clients)
        # include test node for log cleanup; remove from client list
        # self.test_node = [socket.gethostname().split('.', 1)[0]]
        # if self.test_node[0] in self.hostlist_clients:
        #     self.hostlist_clients.remove(self.test_node[0])
        #     self.log.info(
        #         "<<Updated hostlist_clients %s >>", self.hostlist_clients)
        # self.node_list = self.hostlist_clients + self.test_node
        self.node_list = self.hostlist_clients

        # Setup logging directories for soak logfiles
        # self.output dir is an avocado directory .../data/
        self.log_dir = "/tmp/soak"
        self.outputsoakdir = self.outputdir + "/soak"

        # Create the remote log directories on all client nodes
        self.rem_pass_dir = self.log_dir + "/pass" + str(self.loop)
        self.local_pass_dir = self.outputsoakdir + "/pass" + str(self.loop)

        # cleanup soak log directories before test on all nodes
        result = pcmd(
            NodeSet.fromlist(self.node_list),
            "rm -rf {}".format(self.log_dir),
            verbose=False)
        if len(result) > 1 or 0 not in result:
            raise SoakTestError(
                "<<FAILED: Soak directories not removed"
                "from clients>>: {}".format(", ".join(
                    [str(result[key]) for key in result if key != 0])))

    def tearDown(self):
        """Define tearDown and clear any left over jobs in squeue."""
        print("<<tearDown Started>> at {}".format(time.ctime()))
        # clear out any jobs in squeue;
        errors_detected = False
        if len(self.failed_job_id_list) > 0:
            print("<<Cancel jobs in queue with ids {} >>".format(
                self.failed_job_id_list))
            for job_id in self.failed_job_id_list:
                try:
                    slurm_utils.cancel_jobs(job_id)
                except slurm_utils.SlurmFailed as error:
                    self.log.info(
                        "  Failed to cancel job %s with error %s", job_id, str(
                            error))
                    errors_detected = True
        # One last attempt to copy any logfiles from client nodes
        try:
            self.remote_copy(
                self.node_list, self.rem_pass_dir, self.outputsoakdir)
        except SoakTestError as error:
            self.log.info("Remote copy failed with %s", error)
            errors_detected = True
        super(Soak, self).tearDown()
        if errors_detected:
            self.fail("Errors detected cancelling slurm jobs in tearDown()")

    def test_soak_smoke(self):
        """Run soak smoke.

        Test ID: DAOS-2192
        Test Description: This will create a slurm batch job that runs IOR
        with DAOS with the number of processes determined by the number of
        nodes.
        For this test a single pool will be created.  It will run for ~10 min
        :avocado: tags=soak,soak_smoke
        """
        test_param = "/run/smoke/"
        self.run_soak(test_param)

    def test_soak_ior_daos(self):
        """Run soak test with IOR -a daos.

        Test ID: DAOS-2256
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run for the time specififed in
        /run/test_param_test_timeout.

        :avocado: tags=soak,soak_ior,soak_ior_daos
        """
        test_param = "/run/soak_ior_daos/"
        self.run_soak(test_param)

    def test_soak_ior_mpiio(self):
        """Run soak test with IOR -a mpiio.

        Test ID: DAOS-2401,
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run for the time specififed in
        /run/test_param_test_timeout.

        :avocado: tags=soak,soak_ior,soak_ior_mpiio
        """
        test_param = "/run/soak_ior_mpiio/"
        self.run_soak(test_param)

    def test_soak_stress(self):
        """Run soak test with IOR -a mpiio.

        Test ID: DAOS-2256
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run for the time specififed in
        /run/test_param_test_timeout.

        :avocado: tags=soak,soak_stress
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)
