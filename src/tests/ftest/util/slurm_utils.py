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

import os, sys, random, time, subprocess, threading
import pyslurm

_lock = threading.Lock()

class SlurmFailed(Exception):
    """ Thrown when something goes wrong with slurm """

def write_slurm_script(path, name, output, nodecount, cmds, sbatch=None):
    """
    Generates a script for submitting a job to slurm.

    path      --where to write the script file
    name      --job name
    output    --where to put the output (full path)
    nodecount --number of compute nodes to execute on
    cmds      --shell commands that are to be executed
    sbatch    --dictionary containing other less often used parameters to
                sbatch, e.g. mem:100
    returns   --the full path of the script
    """

    if name is None or output is None or nodecount is None or cmds is None:
        raise SlurmFailed("Bad parameters passed for slurm script.")

    unique = random.randint(1, 100000)

    if not os.path.exists(path):
        os.makedirs(path)
    scriptfile = path + '/jobscript' + str(unique) + ".sh"

    f = open(scriptfile, 'w')

    # identify what be used to run this script
    f.write("#!/bin/bash\n#\n")

    # write the mandatory parameters
    f.write("#SBATCH --job-name={}\n".format(name))
    f.write("#SBATCH --output={}\n".format(output))
    f.write("#SBATCH --nodes={}\n".format(nodecount))
    f.write("#SBATCH --distribution=cyclic\n")

    if sbatch:
        for key, value in sbatch:
            f.write("#SBATCH --{}={}\n".format(key, value))
    f.write("\n")

    # debug
    f.write("echo \"nodes: \" $SLURM_JOB_NODELIST \"\"\n")
    f.write("echo \"node count: \" $SLURM_JOB_NUM_NODES \" \"\n")
    f.write("echo \"job name: \" $SLURM_JOB_NAME \"\"\n")

    for cmd in cmds:
        f.write("srun " + cmd)
        f.write("\n")

    f.close()
    return scriptfile

def run_slurm_cmd(cmd, name):

    """
        For very simple situations where you just want to run a simple
        shell command via slurm this will do the trick.

        cmd  --command that can be run from a shell
        name --job name

        returns --the job ID, which is used as a handle for other functions
    """

    daos_test_job = {"wrap": cmd, "job_name": name}
    jobid = pyslurm.job().submit_batch_job(daos_test_job)
    return jobid

def run_slurm_script(script):

    """
        For running scripts, for example those created with write_slurm_script
        function above.

        script --a script file suitable to be run by slurm

        returns --the job ID, which is used as a handle for other functions
    """

    cmd = "sbatch " + script
    output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)
    print output
    if 'Submitted batch job' in output:
        output_list = output.split()
        if len(output_list) >= 4:
            return output_list[3]

    raise SlurmFailed("Batch job failed to start: {}".format(output))

def get_job_output(handle, path):
    """
        Get the output from a slurm job.

        handle   --slurm job id


        returns  --string containing both stdout and stderr
    """
    pass

def check_slurm_job(handle):
    """
        Get the state of a job initiated via slurm.

        handle   --slurm job id

        returns  --one of the slurm defined JOB_STATE_CODES strings plus
                   one extra UNKNOWN if the handle doesn't match a known
                   slurm job.
    """

    job_attributes = pyslurm.job().find_id(handle)
    if job_attributes and len(job_attributes) > 0:
        state = job_attributes[0]["job_state"]
    else:
        state = "UNKNOWN"
    return state


def register_for_job_results(handle, cb_func, maxwait=3600):
    """
        Register a callback for a slurm soak test job.

        handle   --slurm job id
        maxwait  --maximum time to wait in seconds, defaults to 1 hour
        returns  --None

    """
    params = {"handle":handle, "maxwait":maxwait, "cb_func":cb_func}
    athread = threading.Thread(target=watch_job,
                         kwargs=params)
    athread.start()

def watch_job(handle, maxwait, cb_func):
    """
    This function waits for a slurm job to finish and
    calls the provided callback function with the result.

    handle   --the slurm job handle
    maxwait  --max time in seconds to wait
    cb_func  --whom to notify when its done

    return   --none, all results handled through callback
    """

    wait_time = 0
    while True:
        state = check_slurm_job(handle)
        if state == "PENDING" or state == "RUNNING":
            if wait_time > maxwait:
                state = "MAXWAITREACHED"
                print("reached maxwait")
                break
            else:
                wait_time += 5
                time.sleep(5)
        else:
            break

    print("FINAL STATE: slurm job {} completed with : {}".format(handle, state))
    params = {"handle":handle, "state":state}
    with _lock:
        cb_func(params)
