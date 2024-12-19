#!/usr/bin/env python3
#
# (C) Copyright 2024 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Scan daos_engine log files to get a summary of pools activity."""

import argparse
import re
import sys
import time
import pprint
from collections import Counter, OrderedDict, defaultdict

import cart_logparse

class SysPools():
    """Directory of Pools and Summary Activities Found in Engine Log Files"""

    # TODO
    # diagram of nested dictionaries constructed
    # system map update events (output outside if pool-specific context)
    # SWIM events seen by PS leader?
    # add/remove target events on PS leader?
    # pool map version changes
    # rebuild queued (PS leader)
    # rebuild scanning (PS leader, and count of # engines completing scanning, time duration, stalls)
    # rebuild pulling (PS leader? count of # of engines starting/finishing pulling, may be a subset, time duration, stalls)
    # rebuild number of objects, records and progress made?
    # rebuild aborted/errored
    # rebuild completed/success

    re_rank_assign = re.compile("ds_mgmt_drpc_set_rank.*set rank to (\d+)")
    re_step_up = re.compile("rdb_raft_step_up.*([0-9a-fA-F]{8}).*became leader of term (\d+)")
    re_step_down = re.compile("rdb_raft_step_down.*([0-9a-fA-F]{8}).*no longer leader of term (\d+)")
    # TODO: update_one_tgt(), update_tgt_down_drain_to_downout() "change Target.*rank (\d+) idx (\d+).*to (\w+)"
    #       need those functions to print pool UUID. Then here, store a list of target change events in
    #       tgt_change = {"rank": affected_rank, "tgt_idx": affected_tgtidx, "state": new_tgtstate}
    #       self._pools[puuid][highest_term[puuid]]["maps"][highest_pmapver[puuid]]["tgt_state_changes"].append(tgt_change)
    re_pmap_update = re.compile("ds_pool_tgt_map_update\(\) ([0-9a-fA-F]{8}): updated pool map: version=(\d+)->(\d+) pointer")
    #TODO/FIXME rebuild_task_ult() waiting for scheduling time, and waiting for pool map dist needs uniform rb= string.
    #re.rebuild_ldr_wait_schedtime = re.compile("rebuild_task_ult\(\).*rebuild task sleep (\d+) second")
    #re.rebuild_ldr_wait_pmapdist = re.compile("rebuild_task_ult\(\).*map_dist_ver (\d+) map ver (\d+)")
    # uniform rebuild string identifier rb=<pool_uuid>/<rebuild_ver>/<rebuild_gen>/<opcode_string>
    re_rebuild_ldr_start= re.compile("rebuild_leader_start.*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+)$")
    re_rebuild_ldr_status = re.compile("rebuild_leader_status_check\(\).*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+) \[(\w+)\].*duration=(\d+)")
    re_rebuild_ldr_scanning = re.compile("rebuild_leader_status_check\(\).*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+) \[scanning\].*duration=(\d+) secs")
    re_rebuild_ldr_pulling= re.compile("rebuild_leader_status_check\(\).*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+) \[pulling\].*duration=(\d+) secs")
    re_rebuild_ldr_completed= re.compile("rebuild_leader_status_check\(\).*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+) \[completed\].*duration=(\d+) secs")
    re_rebuild_ldr_aborted= re.compile("rebuild_leader_status_check\(\).*rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+) \[aborted\].*duration=(\d+) secs")

    def __init__(self):
        # dictionaries indexed by pool UUID
        self._pools = {}
        self._highest_pmapver = {}
        self._cur_ldr_rank = {}
        self._cur_term = {}

    def check_file(self, log_iter):
        fname = log_iter.fname
        for pid in log_iter.get_pids():
            rank = -1
            for line in log_iter.new_iter(pid=pid):
                msg = line.get_msg()
                host = line.hostname
                datetime = line.time_stamp
                # Find engine rank assignment (early in log)
                match = self.re_rank_assign.match(msg)
                if match:
                    rank = int(match.group(1))
                    print(f"========== rank {rank} logfile {fname} ==========")
                    continue

                # Find pool term begin (PS leader step_up)
                match = self.re_step_up.match(msg)
                if match:
                    puuid = match.group(1)
                    term = int(match.group(2))
                    if puuid not in self._pools:
                        self._pools[puuid] = {}
                    self._cur_ldr_rank[puuid]= rank
                    self._cur_term[puuid] = term
                    old_term = term - 1
                    # if term already exists, error?
                    if term in self._pools[puuid]:
                        print(f"WARN: pool {puuid} term {term} already seen!")
                    # carry over the most recent map version change into the new term, to avoid possible KeyError in rebuild leader status match?
                    if old_term in self._pools:
                        #print(f"PROCESS FOUND pool {puuid} BEGIN term {term}: prior term {old_term} exists")
                        #if self._pools[old_term]["maps"] is not None:
                            #print(f"PROCESS FOUND pool {puuid} BEGIN term {term}: maps dictionary exists.")
                            #if (self._pools[old_term]["maps"]) != {}:
                                #print(f'PROCESS FOUND pool {puuid} BEGIN term {term}: maps dictionary is non-empty, with {len(list(self._pools[puuid][old_term]["maps"].keys()))} keys')
                        if self._pools and self._pools[puuid][old_term]["maps"] != {}:
                            last_mapver = list(self._pools[puuid][old_term]["maps"].keys())[-1]
                            pmap_versions = self._pools[puuid][old_term]["maps"][last_mapver]
                            pmap_versions["carryover"] = True
                            #pmap_versions["rebuild_gens"] = {}
                    else:
                        pmap_versions = {}
                    self._pools[puuid][term] = {"rank": rank, "begin_time": datetime, "end_time": "", "host": host, "pid": pid, "logfile": fname, "maps": pmap_versions}
                    #print(f"{datetime} FOUND pool {puuid} BEGIN\tterm {term} pmap_versions empty: {str(pmap_versions == {})} rank {rank}\t{host}\tPID {pid}\t{fname}")
                    continue

                # Find pool term end (PS leader step_down)
                match = self.re_step_down.match(msg)
                if match:
                    puuid = match.group(1)
                    term = int(match.group(2))
                    if (term != self._cur_term[puuid]):
                        print(f"WARN: step_down term={term} != cur_term={self._cur_term}")
                    self._cur_ldr_rank[puuid] = -1
                    self._cur_term[puuid] = -1
                    self._pools[puuid][term]["end_time"] = datetime
                    #print(f"{datetime} FOUND pool {puuid} END\tterm {term} rank {rank}\t{host}\tPID {pid}\t{fname}")
                    continue

                # TODO: find target status updates (precursor to pool map updates)

                # Find pool map updates
                # FIXME: but only on the current PS leader engine / term? And carry over latest pmap version from prior term to start with?
                match = self.re_pmap_update.match(msg)
                if match:
                    puuid = match.group(1)
                    from_ver = int(match.group(2))
                    to_ver = int(match.group(3))
                    # ignore if this engine is not the leader
                    if puuid not in self._pools or rank != self._cur_ldr_rank[puuid]:
                        continue
                    term = self._cur_term[puuid]
                    self._pools[puuid][term]["maps"][to_ver] = {"carryover": False, "from_ver": from_ver, "time": datetime, "rebuild_gens": {}}
                    #print(f"FOUND pool {puuid} map update {from_ver}->{to_ver} rank {rank}\t{host}\tPID {pid}\t{fname}")
                    continue

                # Find rebuild start by the PS leader
                match = self.re_rebuild_ldr_start.match(msg)
                if match:
                    puuid = match.group(1)
                    mapver = int(match.group(2))
                    rebuild_gen = int(match.group(3))
                    rebuild_op = match.group(4)
                    if rank != self._cur_ldr_rank[puuid]:
                        continue
                    term = self._cur_term[puuid]
                    if term < 1:
                        print(f"WARN pool {puuid} I don't know what term it is ({term})!")
                    # TODO: for now assuming rebuild_gen isn't in the dictionary yet. should we test to be safe?
                    if rebuild_gen in self._pools[puuid][term]["maps"][mapver]["rebuild_gens"]:
                        print(f"WARN pool {puuid} term {term} mapver {mapver} already has rebuild_gen {rebuild_gen}!")
                    # TODO: keep timestamps for overall/scan start, pull start, completed, convert to float and store component durations too
                    self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen] = {"op": rebuild_op, "start_time": datetime,  "time": "xx/xx-xx:xx:xx.xx", "started": True, "scanning": False, "pulling": False, "completed": False, "aborted": False, "duration": 0}
                    #print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{mapver}/{rebuild_gen}/{rebuild_op} rank {rank}\t{host}\tPID {pid}\t{fname}")
                    continue

                # Find rebuild status update reported by the PS leader
                match = self.re_rebuild_ldr_status.match(msg)
                if match:
                    puuid = match.group(1)
                    mapver = int(match.group(2))
                    rebuild_gen = int(match.group(3))
                    rebuild_op = match.group(4)
                    status = match.group(5)
                    dur = int(match.group(6))
                    if rank != self._cur_ldr_rank[puuid]:
                        continue
                    term = self._cur_term[puuid]
                    if term < 1:
                        print(f"WARN pool {puuid} I don't know what term it is ({term})!")
                    if mapver not in self._pools[puuid][term]["maps"]:
                        print(f"WARN pool {puuid} term {term} mapver {mapver} is not in maps dictionary - creating placeholder")
                        self._pools[puuid][term]["maps"][mapver] = {"carryover": False, "from_ver": mapver, "time": "xx/xx-xx:xx:xx.xx", "rebuild_gens": {}}
                        self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen] = {"op": rebuild_op,  "start_time": "xx/xx-xx:xx:xx.xx",  "time": "xx/xx-xx:xx:xx.xx", "started": True, "scanning": False, "pulling": False, "completed": False, "aborted": False, "duration": 0}
                    
                    # TODO: verify rebuild_op == self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["op"]?
                    if rebuild_gen in self._pools[puuid][term]["maps"][mapver]["rebuild_gens"]:
                        existing_op = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["op"]
                        if rebuild_op != existing_op:
                            print(f"WARN  rb={puuid}/{mapver}/{rebuild_gen}/{existing_op} != this line's op {rebuild_op}")
                    if (status == "scanning"):
                        #print(f'ASSIGN _pools[{puuid}][{term}]["maps"][{mapver}]["rebuild_gens"][{rebuild_gen}]["scanning"] = True : rank {rank}\t{host}\tPID {pid}\t{fname}')
                        self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["scanning"] = True
                    elif (status == "pulling"):
                        #print(f'ASSIGN _pools[{puuid}][{term}]["maps"][{mapver}]["rebuild_gens"][{rebuild_gen}]["pulling"] = True : rank {rank}\t{host}\tPID {pid}\t{fname}')
                        self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["pulling"] = True
                    elif (status == "completed"):
                        #print(f'ASSIGN _pools[{puuid}][{term}]["maps"][{mapver}]["rebuild_gens"][{rebuild_gen}]["completed"] = True : rank {rank}\t{host}\tPID {pid}\t{fname}')
                        self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["completed"] = True
                    elif (status == "aborted"):
                         self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["aborted"] = True

                    self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["time"] = datetime
                    self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["duration"] = dur
                    #print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{mapver}/{rebuild_gen}/{rebuild_op} STATUS={status}, DURATION={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
                    continue

                # TODO: look for scan/migrate activity on all pool engines, count and correlate to PS leader activity?

    def print_pools(self):
        for puuid in self._pools:
                print(f"========== Pool {puuid}:")
                for term in self._pools[puuid]:
                    b = self._pools[puuid][term]["begin_time"]
                    e = self._pools[puuid][term]["end_time"]
                    r = self._pools[puuid][term]["rank"]
                    h = self._pools[puuid][term]["host"]
                    p = self._pools[puuid][term]["pid"]
                    f = self._pools[puuid][term]["logfile"]
                    # Print term begin
                    print(f"{b} {puuid} BEGIN  term {term}\trank {r}\t{h}\tPID {p}\t{f}")
                    
                    # Print pool map updates that happened within the term
                    for mapver in self._pools[puuid][term]["maps"]:
                        # Print rebuilds

                        # TODO: print tgt state changes
                        #for tgt_change in self._pools[puuid][term]["maps"][mapver]["tgt_state_changes"]:
                        #    print(f"TGT state {tgt_change["state"]}, rank: {tgt_change["rank"]}, idx: {tgt_change["tgt_idx"]}")
                        t = self._pools[puuid][term]["maps"][mapver]["time"]
                        from_ver = self._pools[puuid][term]["maps"][mapver]["from_ver"]
                        print(f"{t} {puuid} MAPVER {from_ver}->{mapver}")
                        
                        # Rebuilds
                        for rebuild_gen in self._pools[puuid][term]["maps"][mapver]["rebuild_gens"]:
                            rebuild_op = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["op"]
                            dur = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["duration"]
                            started = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["started"]
                            # TODO: scan_done, pull_done booleans
                            scanning = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["scanning"]
                            pulling = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["pulling"]
                            completed =self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["completed"]
                            aborted = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["aborted"]
                            st = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["start_time"]
                            ut = self._pools[puuid][term]["maps"][mapver]["rebuild_gens"][rebuild_gen]["time"]
                            status = "started"
                            if aborted:
                                status = "aborted"
                            if completed:
                                status = "completed"
                            elif pulling:
                                status = "pulling"
                            elif scanning:
                                status = "scanning"
                            print(f"{st} {puuid} RBSTRT {mapver}/{rebuild_gen}/{rebuild_op}")
                            updated = scanning or pulling or completed or aborted
                            if updated:
                             print(f"{ut} {puuid} RBUPDT {mapver}/{rebuild_gen}/{rebuild_op} {status} {dur} seconds")

                    # Print term end (if there is a PS leader step_down)
                    if e != "":
                        print(f"{e} {puuid} END    term {term}\trank {r}\t{h}\tPID {p}\t{f}")
                    else:
                        print(" " * 18 + f"{puuid} END    term {term}\trank {r}\t{h}\tPID {p}\t{f}")

    def sort(self):
        for puuid in self._pools:
            tmp = dict(sorted(self._pools[puuid].items()))
            self._pools[puuid] = tmp
        # _pools[puuid][term]["maps"] should have been inserted in ascending order already?

def run():
    """Scan a list of daos_engine logfiles"""
    ap = argparse.ArgumentParser()
    ap.add_argument('filelist', nargs='+')    
    args = ap.parse_args()

    out_fname = 'sys_logscan.txt'
    out_fd = open(out_fname, 'w')  # pylint: disable=consider-using-with
    real_stdout = sys.stdout
    sys.stdout = out_fd
    print(f'Logging to {out_fname}', file=real_stdout)

    sp = SysPools()

    for fname in args.filelist:
        if fname.endswith("cart_logtest"):
            continue
        #print(f"\n========== Engine log: {fname} ==========")
        try:
            log_iter = cart_logparse.LogIter(fname)
        except UnicodeDecodeError:
            log_iter = cart_logparse.LogIter(args.file, check_encoding=True)
    
        if log_iter.file_corrupt:
            sys.exit(1)
        #print(f"{len(log_iter.get_pids())} PIDs")
        sp.check_file(log_iter)
    #pprint.pprint(sp._pools)
    sp.sort()
    sp.print_pools()

if __name__ == '__main__':
    run()
