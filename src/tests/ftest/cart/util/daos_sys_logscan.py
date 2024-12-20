#!/usr/bin/env python3
#
# (C) Copyright 2024 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Scan daos_engine log files to get a summary of pools activity."""

import argparse
import cart_logparse
import re
import sys


class SysPools():
    """Directory of Pools and Summary Activities Found in Engine Log Files"""

    # TODO
    # diagram of nested dictionaries constructed
    # system map update events (output outside if pool-specific context)
    # SWIM events seen by PS leader?
    # add/remove target events on PS leader?
    # rebuild queued (PS leader)
    # rebuild scanning (PS leader warn about engine updates, #engines finishing scanning, stalls)
    # rebuild pulling (PS leader, #engines/may be a subset, starting/finishing pulling, stalls)
    # rebuild number of objects, records and progress made?

    # Engine rank assignment and pool service leader step_up/down events
    re_rank_assign = re.compile(r"ds_mgmt_drpc_set_rank.*set rank to (\d+)")
    re_step_up = re.compile(r"rdb_raft_step_up.*([0-9a-fA-F]{8}).*leader of term (\d+)")
    re_step_down = re.compile(r"rdb_raft_step_down.*([0-9a-fA-F]{8}).*leader of term (\d+)")

    # TODO: target state change events
    # update_one_tgt(), update_tgt_down_drain_to_downout()
    #   "change Target.*rank (\d+) idx (\d+).*to (\w+)"
    #   need those functions to print pool UUID. Then here, store a list of target change events in
    #   tgt_change = {"rank": affected_rank, "tgt_idx": affected_tgtidx, "state": new_tgtstate}
    #   self._pools[puuid][term]["maps"][mapver]["tgt_state_changes"].append(tgt_change)

    # pool map version update events
    upd_re = r"ds_pool_tgt_map_update.*([0-9a-fA-F]{8}): updated.*map: version=(\d+)->(\d+) pointer"
    re_pmap_update = re.compile(upd_re)

    # uniform rebuild string identifier rb=<pool_uuid>/<rebuild_ver>/<rebuild_gen>/<opcode_string>
    rbid_re = r"rb=([0-9a-fA-F]{8})/(\d+)/(\d+)/(\w+)"

    # Rebuild preliminary steps
    # TODO/FIXME rebuild_task_ult() waiting for sched time, and map dist needs uniform rb= string.
    # re.compile(r"rebuild_task_ult\(\).*rebuild task sleep (\d+) second")
    # re.compile(r"rebuild_task_ult\(\).*map_dist_ver (\d+) map ver (\d+)")

    # Rebuild: PS leader engine starting and status checking a given operation
    # statuses: "scanning", "pulling", "completed", "aborted"
    ldr_start_re = "rebuild_leader_start.*" + rbid_re + "$"
    ldr_status_re = r"rebuild_leader_status_check\(\).*" + rbid_re + r" \[(\w+)\].*duration=(\d+)"
    re_rebuild_ldr_start = re.compile(ldr_start_re)
    re_rebuild_ldr_status = re.compile(ldr_status_re)

    # Legacy rebuild PS leader logging (before uniform rebuild string)
    old_ldr_start_re = r"rebuild_leader_start.*([0-9a-fA-F]{8}).*version=(\d+)/(\d+).*op=(\w+)"
    old_ldr_status_re = (r"rebuild_leader_status_check\(\) (\w+) \[(\w+)\] \(pool ([0-9a-fA-F]{8}) "
                         r"leader (\d+) term (\d+).*ver=(\d+),gen (\d+).*duration=(\d+) secs")
    re_old_ldr_start = re.compile(old_ldr_start_re)
    re_old_ldr_status = re.compile(old_ldr_status_re)

    def __init__(self):
        # dictionaries indexed by pool UUID
        self._pools = {}
        self._highest_pmapver = {}
        self._cur_ldr_rank = {}
        self._cur_ldr_pid = {}
        self._cur_term = {}

        # filename to rank map
        self._file_to_rank = {}

        self._warnings = []

    def _warn(self, wmsg, fname, line):
        full_msg = f"WARN file={fname}"
        if line:
            full_msg += f", line={line.lineno}"
        full_msg += f": {wmsg}"

        self._warnings.append(full_msg)
        print(full_msg)

    def find_rank(self, log_iter):
        print(f"INFO: searching for rank in file {log_iter.fname}")
        found = False
        for line in log_iter.new_iter():
            # when a rank assignment log line found (engine start)
            match = self.re_rank_assign.match(line.get_msg())
            if match:
                self._file_to_rank[log_iter.fname] = int(match.group(1))
                found = True
                break

            # TODO: what about log rotation (not an engine start scenario)?
        return found

    # return log-message, hostname, and date/timestamp components of the line
    def get_line_components(self, line):
        return line.get_msg(), line.hostname, line.time_stamp

    # is this rank, pid the leader of the pool with uuid puuid?
    def is_leader(self, puuid, rank, pid):
        if puuid not in self._pools:
            return False
        if self._cur_ldr_rank[puuid] == rank and self._cur_ldr_pid[puuid] == pid:
            return True
        return False

    def match_ps_step_up(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_step_up.match(msg)
        if match:
            puuid = match.group(1)
            term = int(match.group(2))
            if puuid not in self._pools:
                self._pools[puuid] = {}
            self._cur_ldr_rank[puuid] = rank
            self._cur_ldr_pid[puuid] = pid
            self._cur_term[puuid] = term
            old_term = term - 1
            # if term already exists, error?
            if term in self._pools[puuid]:
                self._warn(f"pool {puuid} term {term} already seen!", fname, line)
            # carry over most recent map version into the new term, avoid later KeyError
            if old_term in self._pools:
                if self._pools and self._pools[puuid][old_term]["maps"] != {}:
                    last_mapver = list(self._pools[puuid][old_term]["maps"].keys())[-1]
                    pmap_versions = self._pools[puuid][old_term]["maps"][last_mapver]
                    pmap_versions["carryover"] = True
                    # pmap_versions["rb_gens"] = {}
            else:
                pmap_versions = {}
            self._pools[puuid][term] = {
                "rank": rank,
                "begin_time": datetime,
                "end_time": "",
                "host": host,
                "pid": pid,
                "logfile": fname,
                "maps": pmap_versions
            }
            # DEBUG
            # print(f"{datetime} FOUND pool {puuid} BEGIN\tterm {term} pmap_versions empty: "
            #      f"{str(pmap_versions == {})} rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def match_ps_step_down(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_step_down.match(msg)
        if match:
            puuid = match.group(1)
            term = int(match.group(2))
            if term != self._cur_term[puuid]:
                self._warn(f"step_down term={term} != cur_term={self._cur_term}", fname, line)
            self._cur_ldr_rank[puuid] = -1
            self._cur_ldr_pid[puuid] = -1
            self._cur_term[puuid] = -1
            self._pools[puuid][term]["end_time"] = datetime
            # DEBUG
            # print(f"{datetime} FOUND pool {puuid} END\tterm {term} rank {rank}\t{host}\t"
            #      f"PID {pid}\t{fname}")
            return True
        return False

    def match_ps_pmap_update(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_pmap_update.match(msg)
        if match:
            puuid = match.group(1)
            from_ver = int(match.group(2))
            to_ver = int(match.group(3))
            # ignore if this engine is not the leader
            if not self.is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            self._pools[puuid][term]["maps"][to_ver] = {
                "carryover": False,
                "from_ver": from_ver,
                "time": datetime,
                "rb_gens": {}
                }
            # DEBUG
            #print(f"FOUND pool {puuid} map update {from_ver}->{to_ver} rank {rank}\t{host}\t"
            #      f"PID {pid}\t{fname}")
            return True
        return False

    def get_rb_components(self, match):
        return match.group(1), int(match.group(2)), int(match.group(3)), match.group(4)

    def match_ps_rb_start(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_rebuild_ldr_start.match(msg)
        if match:
            puuid, ver, gen, op = self.get_rb_components(match)
            if not self.is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            if term < 1:
                self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
                return True
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
            # TODO: keep timestamps for overall/scan start, pull start, completed
            #       convert to float and store component durations too
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = {
                "op": op,
                "start_time": datetime,
                "time": "xx/xx-xx:xx:xx.xx",
                "started": True,
                "scanning": False,
                "pulling": False,
                "completed": False,
                "aborted": False,
                "duration": 0
                }
            # DEBUG
            #print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
            #      f"rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def match_legacy_ps_rb_start(self, fname, line, pid, rank):
        #old_ldr_start_re = r"rebuild_leader_start.*([0-9a-fA-F]{8}).*version=(\d+)/(\d+).*op=(\w+)"
        msg, host, datetime = self.get_line_components(line)
        match = self.re_old_ldr_start.match(msg)
        if match:
            puuid = match.group(1)
            ver = int(match.group(2))
            gen = int(match.group(3))
            op = match.group(4)
            if not self.is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            if term < 1:
                self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
                return True
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
            # TODO: keep timestamps for overall/scan start, pull start, completed
            #       convert to float and store component durations too
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = {
                "op": op,
                "start_time": datetime,
                "time": "xx/xx-xx:xx:xx.xx",
                "started": True,
                "scanning": False,
                "pulling": False,
                "completed": False,
                "aborted": False,
                "duration": 0
                }
            # DEBUG
            #print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
            #      f"rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def match_ps_rb_status(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_rebuild_ldr_status.match(msg)
        if match:
            puuid, ver, gen, op = self.get_rb_components(match)
            status = match.group(5)
            dur = int(match.group(6))
            if not self.is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            if term < 1:
                self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
                return True
            if ver not in self._pools[puuid][term]["maps"]:
                self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                           fname, line)
                self._pools[puuid][term]["maps"][ver] = {
                    "carryover": False,
                    "from_ver": ver,
                    "time": "xx/xx-xx:xx:xx.xx",
                    "rb_gens": {}
                    }
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = {
                    "op": op,
                    "start_time": "xx/xx-xx:xx:xx.xx",
                    "time": "xx/xx-xx:xx:xx.xx",
                    "started": True,
                    "scanning": False,
                    "pulling": False,
                    "completed": False,
                    "aborted": False,
                    "duration": 0
                    }
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                existing_op = self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["op"]
                if op != existing_op:
                    self._warn(f"rb={puuid}/{ver}/{gen}/{existing_op} != line op {op}", fname, line)
            if status == "scanning":
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scanning"] = True
            elif status == "pulling":
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pulling"] = True
            elif status == "completed":
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["completed"] = True
            elif status == "aborted":
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["aborted"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["time"] = datetime
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["duration"] = dur
            # DEBUG
            # print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
            #      f"STATUS={status}, DUR={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def match_legacy_ps_rb_status(self, fname, line, pid, rank):
        msg, host, datetime = self.get_line_components(line)
        match = self.re_old_ldr_status.match(msg)
        if match:
            op = match.group(1)
            status = match.group(2)
            puuid = match.group(3)
            log_ldr = int(match.group(4))
            log_term = int(match.group(5))
            ver = int(match.group(6))
            gen = int(match.group(7))
            dur = int(match.group(8))
            if not self.is_leader(puuid, rank, pid):
                return True
            if rank != log_ldr:
                self._warn(f"pool {puuid} my rank {rank} != leader {log_ldr}", fname, line)
            term = self._cur_term[puuid]
            if term < 1 or term != log_term:
                self._warn(f"pool {puuid} I don't know what term it is ({term}), {log_term}!",
                           fname, line)
                return True
            if ver not in self._pools[puuid][term]["maps"]:
                self._warn(f"pool {puuid} term {term} ver {ver} not in maps - add placeholder",
                           fname, line)
                self._pools[puuid][term]["maps"][ver] = {
                    "carryover": False,
                    "from_ver": ver,
                    "time": "xx/xx-xx:xx:xx.xx",
                    "rb_gens": {}
                    }
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen] = {
                    "op": op,
                    "start_time": "xx/xx-xx:xx:xx.xx",
                    "time": "xx/xx-xx:xx:xx.xx",
                    "started": True,
                    "scanning": False,
                    "pulling": False,
                    "completed": False,
                    "aborted": False,
                    "duration": 0
                    }
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                existing_op = self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["op"]
                if op != existing_op:
                    self._warn(f"rb={puuid}/{ver}/{gen}/{existing_op} != line op {op}", fname, line)
            if (status == "scanning"):
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["scanning"] = True
            elif (status == "pulling"):
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["pulling"] = True
            elif (status == "completed"):
                self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["completed"] = True
            elif (status == "aborted"):
                    self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["aborted"] = True
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["time"] = datetime
            self._pools[puuid][term]["maps"][ver]["rb_gens"][gen]["duration"] = dur
            # DEBUG
            #print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
            #      f"STATUS={status}, DUR={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def scan_file(self, log_iter, rank=-1):
        fname = log_iter.fname

        # Find rank assignment log line for this file. Can't do much without it.
        self._file_to_rank[fname] = rank
        if rank == -1 and not self.find_rank(log_iter):
            self._warn(f"cannot find rank assignment in log file - skipping", fname)
            return
        rank = self._file_to_rank[fname]

        for pid in log_iter.get_pids():
            print(f"INFO: scanning file {fname} rank {rank}, PID {pid}")
            for line in log_iter.new_iter(pid=pid):
                msg = line.get_msg()
                host = line.hostname
                datetime = line.time_stamp

                # Find pool term begin (PS leader step_up)
                if self.match_ps_step_up(fname, line, pid, rank):
                    continue

                # Find pool term end (PS leader step_down)
                if self.match_ps_step_down(fname, line, pid, rank):
                    continue

                # TODO: find target status updates (precursor to pool map updates)

                # Find pool map updates
                if self.match_ps_pmap_update(fname, line, pid, rank):
                    continue

                # Find rebuild start by the PS leader
                if self.match_ps_rb_start(fname, line, pid, rank):
                    continue

                if self.match_legacy_ps_rb_start(fname, line, pid, rank):
                    continue

                if self.match_ps_rb_status(fname, line, pid, rank):
                    continue

                if self.match_legacy_ps_rb_status(fname, line, pid, rank):
                    continue

                # TODO: look for scan/migrate activity on all pool engines, count and correlate
                #       to PS leader activity?
            # TODO: For a PID that is killed, clear any associated cur_ldr_rank / cur_ldr_pid.
            # At logfile end, it could be due to engine killed, or could just be log rotation.

    def print_pools(self):
        for puuid in self._pools:
            print(f"===== Pool {puuid}:")
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
                for v in self._pools[puuid][term]["maps"]:
                    # TODO: print tgt state changes

                    # Print map updates
                    t = self._pools[puuid][term]["maps"][v]["time"]
                    from_ver = self._pools[puuid][term]["maps"][v]["from_ver"]
                    print(f"{t} {puuid} MAPVER {from_ver}->{v}")

                    # Print rebuilds
                    for g in self._pools[puuid][term]["maps"][v]["rb_gens"]:
                        op = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["op"]
                        dur = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["duration"]
                        # line len
                        started = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["started"]
                        # TODO: scan_done, pull_done booleans
                        # line len
                        scan = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["scanning"]
                        pull = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["pulling"]
                        # line len
                        comp = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["completed"]
                        abrt = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["aborted"]
                        # line len
                        st = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["start_time"]
                        ut = self._pools[puuid][term]["maps"][v]["rb_gens"][g]["time"]
                        status = "started"
                        if abrt:
                            status = "aborted"
                        if comp:
                            status = "completed"
                        elif pull:
                            status = "pulling"
                        elif scan:
                            status = "scanning"
                        print(f"{st} {puuid} RBSTRT {v}/{g}/{op}")
                        updated = scan or pull or comp or abrt
                        if updated:
                            print(f"{ut} {puuid} RBUPDT {v}/{g}/{op} {status} {dur} seconds")

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

    rank_in_fname_re = re.compile(r"\.rank=(\d+)\.")

    sp = SysPools()

    for fname in args.filelist:
        if fname.endswith("cart_logtest"):
            continue

        rank = -1
        match = rank_in_fname_re.search(fname)
        if match:
            rank = int(match.group(1))

        try:
            log_iter = cart_logparse.LogIter(fname)
        except UnicodeDecodeError:
            log_iter = cart_logparse.LogIter(args.file, check_encoding=True)

        if log_iter.file_corrupt:
            sys.exit(1)
        sp.scan_file(log_iter, rank=rank)

    print(f"\n========== Pools Report ({len(sp._warnings)} warnings during scanning) ==========\n")
    sp.sort()
    sp.print_pools()

if __name__ == '__main__':
    run()
