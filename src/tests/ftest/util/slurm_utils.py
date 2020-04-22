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
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""

from __future__ import print_function
import os
import random
import time
import subprocess
import threading
import re
from avocado.utils import process

W_LOCK = threading.Lock()


class SlurmFailed(Exception):
    """Thrown when something goes wrong with slurm."""


def cancel_jobs(job_id):
    """Cancel slurms jobs.

    :param job_id: slurm job id
    :type job_id: int
    :return: status
    :rtype: bool

    """
    status = process.system("scancel {}".format(job_id))
    if status > 0:
        raise SlurmFailed(
            "Slurm: scancel failed to kill job {}".format(job_id))
    return status


def create_slurm_partition(nodelist, name):
    """Create a slurm partion for soak jobs.

    client nodes will be allocated for this partiton
    Args:
        nodelist (list): list of nodes for job allocation
        name (str): partition name
    Returns: status bool
    """
    # If the partition exists; delete it because if may have wrong nodes
    status = process.system(
        "scontrol delete PartitionName={}".format(name))
    if status == 0:
        status = process.system(
            "scontrol create PartitionName={} Nodes={}".format(
                name, ",".join(nodelist)))
    return status


def delete_slurm_partition(name):
    """Create a slurm partion for soak jobs.

    Remove the partition from slurm
    Args:
        name (str): partition name
    Returns: status bool
    """
    # If the partition exists; delete it because if may have wrong nodes
    status = process.system(
        "scontrol delete PartitionName={}".format(name))
    return status


def write_slurm_script(path, name, output, nodecount, cmds, uniq, sbatch=None):
    """Generate a script for submitting a job to slurm.

    path      --where to write the script file
    name      --job name
    output    --where to put the output (full path)
    nodecount --number of compute nodes to execute on
    cmds      --shell commands that are to be executed
    sbatch    --dictionary containing other less often used parameters to
                sbatch, e.g. mem:100
    uniq string  --a unique string to append to the job and log files
    returns   --the full path of the script
    """
    if name is None or nodecount is None or cmds is None:
        raise SlurmFailed("Bad parameters passed for slurm script.")
    if uniq is None:
        uniq = random.randint(1, 100000)

    if not os.path.exists(path):
        os.makedirs(path)
    scriptfile = path + '/jobscript' + "_" + str(uniq) + ".sh"
    with open(scriptfile, 'w') as script_file:
        # identify what be used to run this script
        script_file.write("#!/bin/bash\n#\n")

        # write the mandatory parameters
        script_file.write("#SBATCH --job-name={}\n".format(name))
        script_file.write("#SBATCH --nodes={}\n".format(nodecount))
        script_file.write("#SBATCH --distribution=cyclic\n")
        if output is not None:
            output = output + str(uniq)
            script_file.write("#SBATCH --output={}\n".format(output))
        if sbatch:
            for key, value in sbatch.items():
                if key == "error":
                    value = value + str(uniq)
                script_file.write("#SBATCH --{}={}\n".format(key, value))
        script_file.write("\n")

        # debug
        script_file.write("echo \"nodes: \" $SLURM_JOB_NODELIST \n")
        script_file.write("echo \"node count: \" $SLURM_JOB_NUM_NODES \n")
        script_file.write("echo \"job name: \" $SLURM_JOB_NAME \n")

        for cmd in list(cmds):
            script_file.write(cmd + "\n")
    return scriptfile


def run_slurm_script(script, logfile=None):
    """Run slurm script.

    Args:
        script (str): script file suitable to by run by slurm
        logfile (str): add -o param and generate logfile

    Returns:
        str: the job ID, which is used as a handle for other functions

    """
    job_id = None
    cmd = ["sbatch"]
    if logfile is not None:
        cmd.extend(["-o", logfile])
    cmd.append(script)
    try:
        result = process.run(cmd, timeout=10)
    except process.CmdError as error:
        result = None
        raise SlurmFailed("job failed : {}".format(error))
    if result:
        output = result.stdout
        match = re.search(r"Submitted\s+batch\s+job\s+(\d+)", str(output))
        if match is not None:
            job_id = match.group(1)
    return job_id


def check_slurm_job(handle):
    """Get the state of a job initiated via slurm.

    handle   --slurm job id

    returns  --one of the slurm defined JOB_STATE_CODES strings plus
                one extra UNKNOWN if the handle doesn't match a known
                slurm job.
    """
    cmd = ["scontrol", "show", "job", handle]
    output = subprocess.check_output(cmd, stderr=subprocess.STDOUT)  # nosec
    match = re.search(r"JobState=([a-zA-Z]+)", str(output))
    if match is not None:
        state = match.group(1)
    else:
        state = "UNKNOWN"
    return state


def register_for_job_results(handle, test, maxwait=3600):
    """
    Register a callback for a slurm soak test job.

    handle   --slurm job id
    test     --object with a job_done callback function
    maxwait  --maximum time to wait in seconds, defaults to 1 hour
    returns  --None

    """
    params = {"handle": handle, "maxwait": maxwait, "test_obj": test}
    athread = threading.Thread(target=watch_job,
                               kwargs=params)
    athread.start()


def watch_job(handle, maxwait, test_obj):
    """Watch for a slurm job to finish use callback function with the result.

    handle   --the slurm job handle
    maxwait  --max time in seconds to wait
    test_obj --whom to notify when its done

    return   --none, all results handled through callback
    """
    wait_time = 0
    while True:
        state = check_slurm_job(handle)
        if state in ("PENDING", "RUNNING", "COMPLETING"):
            if wait_time > maxwait:
                state = "MAXWAITREACHED"
                print("Job {} has timedout after {} secs".format(handle,
                                                                 maxwait))
                break
            else:
                wait_time += 5
                time.sleep(5)
        else:
            break

    print("FINAL STATE: slurm job {} completed with : {} at {}\n".format(
        handle, state, time.ctime()))
    params = {"handle": handle, "state": state}
    with W_LOCK:
        test_obj.job_done(params)


def srun(nodes, cmd, srun_params=None):
    """Run srun cmd on slurm partition.

    Args:
        hosts (str): hosts to allocate
        cmd (list): cmdline to execute as a list
        srun_params(dict):  additional params for srun

    Returns:
        CmdResult: srun command result

    """
    params_list = []
    params = ""
    if srun_params is not None:
        for key, value in srun_params.items():
            params_list.extend(["--{}={}".format(key, value)])
            params = " ".join(params_list)
    cmd = ["srun", "--nodelist={}".format(nodes), params] + list(cmd)
    try:
        result = process.run(cmd, timeout=30)
    except process.CmdError as error:
        result = None
        raise SlurmFailed("srun failed : {}".format(error))
    return result
