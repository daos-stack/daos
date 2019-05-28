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
'''
import os
import traceback
import sys
import json
import time
import uuid
import subprocess
import threading
import time
import signal
import math
import re
#import influx_client

# set an insane number of env variables
def init_env():
    # duplicate env/client.sh to be able to run wiht sudo under Jupyter
    sys.path.append('/home/daos/demo/src/daos-centos/src/utils/py')
    sys.path.append('/usr/lib/python2.7/site-packages')
    #sys.path.append('/home/daos/demo/script')
    os.environ["PATH"] = "/home/daos/demo/install/centos/sbin/:" + os.environ["PATH"]
    os.environ["PATH"] = "/home/daos/demo/install/centos/bin/:" + os.environ["PATH"]
    os.environ["CPATH"] = "/home/daos/demo/install/centos/include/:"
    os.environ["CRT_PHY_ADDR_STR"] = "ofi+sockets"
    #os.environ["CRT_PHY_ADDR_STR"] = "ofi+psm2"
    os.environ["OFI_INTERFACE"] = "ib0"
    os.environ["OFI_PORT"] = "31416"
    os.environ["PSM2_MULTI_EP"] = "1"
    os.environ["FI_SOCKETS_MAX_CONN_RETRY"] = "1"
    os.environ["CRT_CTX_SHARE_ADDR"] = "1"
    os.environ["FI_PSM2_DISCONNECT"] = "1"
    os.environ["D_LOG_FILE"] = "/tmp/daos_client.log"
    os.environ["CRT_ATTACH_INFO_PATH"] = "/home/daos/demo/uri"
    os.environ["DAOS_SINGLETON_CLI"] = "1"

init_env()
from ClusterShell.Task import task_self
from daos_api import DaosContext, DaosPool, DaosContainer, DaosApiError

# constants
kb = 1000
mb = 1000 * kb
gb = 1000 * mb
kib = 1024
mib = 1024 * kib
gib = 1024 * mib

# mutable input params
clients = "wolf-130"
#clients = "wolf-130,wolf-131,wolf-132,wolf-133"
servers = "wolf-71"
ior_bsize = 10 * mib
ior_iter = 1
ior_hostnames = "wolf-130:1"
#ior_hostnames = "wolf-77:1,wolf-131:1,wolf-132:1,wolf-133:1"
ior_tasks = 1
#ior_tasks = 4
mydb = None
#mydb = influx_client.influxdb()

# internal global variables
aep_used = 0
ssd_used = 0
stop = False
task = task_self()
thread = None

# pool class for the demo.
# automatically created/destroyed on __init__/__del__
class PoolDemo:

    pool = None
    context = None
    persist = False # destroy on __del__

    def __init__(self, context, scm=18*gb, nvme=18*gb, persistent=False):
        self.context = context
        self.pool = DaosPool(context)
        self.pool.create(511, 0, 0, scm, b"daos_server", nvme_size=nvme)
        self.pool.connect(1 << 1)
        self.persist = persistent
        print("\tCreated pool " + self.pool.get_uuid_str())

    def __del__(self):
        print("\tDestroying pool " + self.pool.get_uuid_str())
        self.pool.disconnect()
        if persist == False:
            self.pool.destroy(1)

    def uuid(self):
        return self.pool.get_uuid_str()

    def stat(self, show=False):
        pool_info = self.pool.pool_query()

        d = dict()
        # fill AEP space info
        d['aep_total'] = pool_info.pi_space.ps_space.s_total[0]
        d['aep_used'] = d['aep_total'] - pool_info.pi_space.ps_space.s_free[0]
        d['aep_ratio'] = (100 * float(d['aep_used'])) / d['aep_total']
        d['aep_ratio'] = round(d['aep_ratio'], 1)
        if d['aep_ratio'] == 0 and aep_used != 0:
            d['aep_ratio'] = 0.1
        d['aep_total'] = round(float(d['aep_total']) / gb, 1)
        d['aep_used'] = round(float(d['aep_used']) / gb, 1)

        # fill SSD space info
        d['ssd_total'] = pool_info.pi_space.ps_space.s_total[1]
        d['ssd_used'] = d['ssd_total'] - pool_info.pi_space.ps_space.s_free[1]
        if d['ssd_total'] == 0:
            d['ssd_ratio'] = 0
        else:
            d['ssd_ratio'] = (100 * float(d['ssd_used'])) / d['ssd_total']
            d['ssd_ratio'] = round(d['ssd_ratio'], 1)
            if d['ssd_ratio'] == 0 and ssd_used != 0:
                d['aep_ratio'] = 0.1
        d['ssd_total'] = round(float(d['ssd_total']) / gb, 1)
        d['ssd_used'] = round(float(d['ssd_used']) / gb, 1)

        if show == False:
            return d

        print("\tAEP Space:")
        print("\t\tTotal: " + str(d['aep_total']) + "GB")
        print("\t\tUsed: " + str(d['aep_used']) + "GB (" + str(d['aep_ratio']) + "%)")
        print("\tNVMe Space:")
        print("\t\tTotal: " + str(d['ssd_total']) + "GB")
        print("\t\tUsed: " + str(d['ssd_used']) + "GB (" + str(d['ssd_ratio']) + "%)")

        return d

    # create new container from this pool
    def new_cont(self):
        cont = DaosContainer(self.context)
        cont.create(self.pool.handle, uuid.uuid1())
        return ContDemo(self, cont)

# container class for the demo.
# automatically created/destroyed on PoolDemo::new_cont()/__del__
class ContDemo:

    cont = None
    pool = None

    def __init__(self, pool, cont):
        self.pool = pool
        self.cont = cont
        cont.open()
        print("\tCreated container " + self.cont.get_uuid_str())

    def __del__(self):
        self.cont.close()
        print("\tDestroying container " + self.cont.get_uuid_str())
        self.cont.destroy(1)
        self.pool = None
        self.cont = None

    def uuid(self):
        return self.cont.get_uuid_str()

    def aggregate(self):
        self.cont.aggregate(self.cont.coh, 0)

# background thread dumping space info to file/stdout
def space_mon(pool, show):
    global mydb
    global aep_used
    global ssd_used

    f = open('/tmp/space', 'w')
    f.truncate()
    while stop == False:
        d = pool.stat()
        aep_used = d['aep_used']
        ssd_used = d['ssd_used']
        if show == True:
            print("\t\t" + str(d['aep_ratio']) + "%\t" + str(d['ssd_ratio']) + "%")
            f.write(str(d['aep_ratio']) + "%\t" + str(d['ssd_ratio']) + "%\n")
            f.flush()
        if mydb is not None:
            mydb.write_storage_info(d)
        time.sleep(2)
    f.close()

# fire an ior job
def run_ior(puuid, cuuid, size):
    global mydb

    cmd = "orterun -allow-run-as-root"
    cmd += " -H " + ior_hostnames
    cmd += " -np " + str(ior_tasks)
    cmd += " ior -a DAOS"
    cmd += " -w -r -k -E -i " + str(ior_iter)
    cmd += " -b " + str(ior_bsize) + " -t " + str(size)
    cmd += " --daos.pool " + puuid + " --daos.svcl 0 --daos.cont " + cuuid

    f = open('/tmp/ior-' + str(size), 'w')
    f.truncate()
    f.write(cmd + "\n")

    ior_in_data = {'size' : size,
                   'ior_bsize' : ior_bsize,
                   'ior_tasks': ior_tasks}
    if mydb is not None:
        mydb.write_ior_input(**ior_in_data)

    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)
    while True:
        out = process.stdout.readline()
        output = out.decode('utf-8') 
        if output == '' and process.poll() is not None:
            break

        # print output to /tmp for debugging purpose
        if output:
            f.write(output)
            if any(re.findall(r'write|read', output)) and mydb is not None:
                mydb.write_ior_output(output)
            f.flush()

        # extract perf from the output
        l = output.split()
        if len(l) > 1 and l[0] in ("write", "read"):
            if size >= mb:
                # BW for block >= 1MB
                print("\t\t" + l[0] + "@" + l[1] + "MiB/s")
            else:
                # Convert to IOPS otherwise
                val = float(l[1])
                val *= mib
                val /= size
                val /= kb
                val = round(val, 2)
                print("\t\t" + l[0] + "@" + str(val)  + "K IOPS")
    f.close()

    if process.poll() != 0:
        print("\t\tIOR failed")

def run_ior_256B(pool, cont):
    run_ior(pool.uuid(), cont.uuid(), 256)

def run_ior_1MiB(pool, cont):
    run_ior(pool.uuid(), cont.uuid(), mib)

def trigger_wait_aggregation(cont):
    if aep_used == 0:
        return
    used = ssd_used

    # trigger aggregation
    cont.aggregate()

    # wait for ssd used space to decrease (wait for 10s max)
    nr = 0
    while used >= ssd_used and nr < 5:
        time.sleep(2)
        nr += 1

    # wait for ssd space to stabilize
    nr = 0
    while used < ssd_used and nr < 2:
        if used == ssd_used:
            nr += 1
        else:
            nr = 0
            used = ssd_used
        time.sleep(2)

def setup_ior(show=False):
    global mydb
    global thread

    if mydb is not None:
        mydb.connect()
        mydb.write_zero_point()

    # kill all possible IOR zombie processes
    task.run("killall -9 ior", nodes=clients)

    try:
        # setup the DAOS python API
        with open('/home/daos/demo/src/daos-centos/.build_vars.json') as build_file:
            data = json.load(build_file)
        context = DaosContext(data['PREFIX'] + '/lib/')

        # create a pool & print space usage info
        pool = PoolDemo(context)
        os.environ["PUUID"] = pool.uuid()
        pool.stat(show=True)

        # start space monitoring thread
        thread = threading.Thread(target=space_mon, args=(pool,show))
        thread.start()
    except DaosApiError as excep:
        print(excep)
        print(traceback.format_exc())
        if mydb is not None:
            mydb.write_zero_point()

    return pool

def teardown_ior():
    global mydb
    global stop

    # stop space monitoring thread
    stop = True
    thread.join()
    if mydb is not None:
        mydb.write_zero_point()

# signal handler for main above
def signal_handler(sig, frame):
    # Kill external processes
    task.run("killall -9 ior", nodes=clients)
    teardown_ior()

# Run same sequence as in the IOR notebook
if __name__ == '__main__':
    print("Running IOR Demo")
    print("[1/5] Setup DAOS Pool")
    pool = setup_ior(show=True)

    signal.signal(signal.SIGINT, signal_handler)

    # create a container
    cont = pool.new_cont()
    print("[1/5] COMPLETED")

    print("[2/5] Run IOR/256B")
    run_ior_256B(pool, cont)
    print("[2/5] COMPLETED")

    # trigger aggregation
    print("[3/5] Trigger aggregation")
    trigger_wait_aggregation(cont)
    print("[3/5] COMPLETED")

    # run ior with 1MB size
    print("[4/5] Run IOR/1MB")
    run_ior_1MiB(pool, cont)
    print("[4/5] COMPLETED")

    # print final space usage
    time.sleep(2)
    pool.stat(show=True)

    print("[5/5] Teardown")
    cont = None
    pool = None
    teardown_ior()
    print("[5/5] COMPLETED")
