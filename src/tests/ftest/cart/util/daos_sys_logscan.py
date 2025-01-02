#!/usr/bin/env python3
#
# (C) Copyright 2024 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""Scan daos_engine log files to get a summary of pools activity."""

import argparse
import re
import sys

import cart_logparse


class SysPools():
    """Directory of Pools and Summary Activities Found in Engine Log Files"""

    # Future possibilities include:
    # diagram of nested dictionaries constructed in the comments
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

    # Future possibility: target state change events
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

    # Future possibility: match the rebuild preliminary steps
    # rebuild_task_ult() wait for scheduling, and map dist - both would info to match on.
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
        self._check_rb_new_fmt = True
        self._check_rb_legacy_fmt = True
        self._debug = False

    def _warn(self, wmsg, fname, line=None):
        full_msg = f"WARN file={fname}"
        if line:
            full_msg += f", line={line.lineno}"
        full_msg += f": {wmsg}"

        self._warnings.append(full_msg)
        print(full_msg)

    def get_warnings(self):
        """Return all warnings stored when scanning engine log files"""
        return self._warnings

    def _set_rank(self, log_iter):
        print(f"INFO: searching for rank in file {log_iter.fname}")
        found = False
        for line in log_iter.new_iter():
            # when a rank assignment log line found (engine start)
            match = self.re_rank_assign.match(line.get_msg())
            if match:
                self._file_to_rank[log_iter.fname] = int(match.group(1))
                found = True
                break

            # Future enhancement: what about log rotation (not an engine start scenario)?
        return found

    # return log-message, hostname, and date/timestamp components of the line
    def _get_line_components(self, line):
        return line.get_msg(), line.hostname, line.time_stamp

    # is this rank, pid the leader of the pool with uuid puuid?
    def _is_leader(self, puuid, rank, pid):
        if puuid not in self._pools:
            return False
        if self._cur_ldr_rank[puuid] == rank and self._cur_ldr_pid[puuid] == pid:
            return True
        return False

    def _get_ps_leader_components(self, match):
        # puuid, term
        # see re_step_up and re_step_down
        return match.group(1), int(match.group(2))

    def _match_ps_step_up(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_step_up.match(msg)
        if match:
            puuid, term = self._get_ps_leader_components(match)
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
                    last_mapver = max(self._pools[puuid][old_term]["maps"].keys())
                    pmap_versions = self._pools[puuid][old_term]["maps"][last_mapver]
                    pmap_versions["carryover"] = True
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
            if self._debug:
                print(f"{datetime} FOUND pool {puuid} BEGIN\tterm {term} pmap_versions empty: "
                      f"{str(pmap_versions == {})} rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def _match_ps_step_down(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_step_down.match(msg)
        if match:
            puuid, term = self._get_ps_leader_components(match)
            if term != self._cur_term[puuid]:
                self._warn(f"step_down term={term} != cur_term={self._cur_term}", fname, line)
            self._cur_ldr_rank[puuid] = -1
            self._cur_ldr_pid[puuid] = -1
            self._cur_term[puuid] = -1
            self._pools[puuid][term]["end_time"] = datetime
            if self._debug:
                print(f"{datetime} FOUND pool {puuid} END\tterm {term} rank {rank}\t{host}\t"
                      f"PID {pid}\t{fname}")
            return True
        return False

    def _get_pmap_update_components(self, match):
        # puuid, from_version, to_version
        # see re_pmap_update
        return match.group(1), int(match.group(2)), int(match.group(3))

    def _match_ps_pmap_update(self, fname, line, pid, rank):
        msg, host, datetime = self._get_line_components(line)
        match = self.re_pmap_update.match(msg)
        if match:
            puuid, from_ver, to_ver = self._get_pmap_update_components(match)
            # ignore if this engine is not the leader
            if not self._is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            self._pools[puuid][term]["maps"][to_ver] = {
                "carryover": False,
                "from_ver": from_ver,
                "time": datetime,
                "rb_gens": {}
            }
            if self._debug:
                print(f"FOUND pool {puuid} map update {from_ver}->{to_ver} rank {rank}\t{host}\t"
                      f"PID {pid}\t{fname}")
            return True
        return False

    def _get_rb_components(self, match):
        # puuid, map version number, rebuild generation number, rebuild operation string
        # same for new uniform identifier format and legacy log line format
        # see re_rebuild_ldr_start, re_old_ldr_start
        return match.group(1), int(match.group(2)), int(match.group(3)), match.group(4)

    def _match_ps_rb_start(self, fname, line, pid, rank):
        # Do not match on new rebuild log format if we found legacy format
        if not self._check_rb_new_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_rebuild_ldr_start.match(msg)
        if match:
            # Disable checking for legacy rebuild log format, to save execution time
            self._check_rb_legacy_fmt = False
            puuid, ver, gen, op = self._get_rb_components(match)
            if not self._is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            if term < 1:
                self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
                return True
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
            # Future possibility: keep timestamps, durations for scan start, pull start, completed
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
            if self._debug:
                print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
                      f"rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def _match_legacy_ps_rb_start(self, fname, line, pid, rank):
        # Do not match on legacy rebuild log format if we found new format
        if not self._check_rb_legacy_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_old_ldr_start.match(msg)
        if match:
            # Disable checking for new rebuild log format, to save execution time
            self._check_rb_new_fmt = False
            puuid, ver, gen, op = self._get_rb_components(match)
            if not self._is_leader(puuid, rank, pid):
                return True
            term = self._cur_term[puuid]
            if term < 1:
                self._warn(f"pool {puuid} I don't know what term it is ({term})!", fname, line)
                return True
            if gen in self._pools[puuid][term]["maps"][ver]["rb_gens"]:
                self._warn(f"pool {puuid} term {term} ver {ver} already has gen {gen}", fname, line)
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
            if self._debug:
                print(f"{datetime} FOUND rebuild start in term {term}, rb={puuid}/{ver}/{gen}/{op} "
                      f"rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def _get_ps_rb_status_components(self, match):
        # puuid, map version, rebuild-generation, operation, status, duration
        # see re_rebuild_ldr_status
        return self._get_rb_components(match) + (match.group(5), int(match.group(6)))

    def _match_ps_rb_status(self, fname, line, pid, rank):
        # Do not match on new rebuild log format if we found legacy format
        if not self._check_rb_new_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_rebuild_ldr_status.match(msg)
        if match:
            # Disable checking for legacy rebuild log format, to save execution time
            self._check_rb_legacy_fmt = False
            puuid, ver, gen, op, status, dur = self._get_ps_rb_status_components(match)
            if not self._is_leader(puuid, rank, pid):
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
            if self._debug:
                print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
                      f"STATUS={status}, DUR={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def _get_legacy_ps_rb_status_components(self, match):
        # rebuild-op, status, puuid, leader rank, term, map version, rebuild-gen, duration
        # see re_old_ldr_status
        return match.group(1), match.group(2), match.group(3), int(match.group(4)), \
               int(match.group(5)), int(match.group(6)), int(match.group(7)), int(match.group(8))

    def _match_legacy_ps_rb_status(self, fname, line, pid, rank):
        # Do not match on legacy rebuild log format if we found new format
        if not self._check_rb_legacy_fmt:
            return False
        msg, host, datetime = self._get_line_components(line)
        match = self.re_old_ldr_status.match(msg)
        if match:
            # Disable checking for new rebuild log format, to save execution time
            self._check_rb_new_fmt = False
            op, status, puuid, log_ldr, log_term, ver, gen, dur = \
                self._get_legacy_ps_rb_status_components(match)
            if not self._is_leader(puuid, rank, pid):
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
            if self._debug:
                print(f"{datetime} FOUND rebuild UPDATE term={term} rb={puuid}/{ver}/{gen}/{op} "
                      f"STATUS={status}, DUR={dur} seconds rank {rank}\t{host}\tPID {pid}\t{fname}")
            return True
        return False

    def scan_file(self, log_iter, rank=-1):
        """Scan a daos engine log file and insert important pool events into a nested dictionary"""
        fname = log_iter.fname

        # Find rank assignment log line for this file. Can't do much without it.
        self._file_to_rank[fname] = rank
        if rank == -1 and not self._set_rank(log_iter):
            self._warn("cannot find rank assignment in log file - skipping", fname)
            return
        rank = self._file_to_rank[fname]

        for pid in log_iter.get_pids():
            print(f"INFO: scanning file {fname} rank {rank}, PID {pid}")
            for line in log_iter.new_iter(pid=pid):
                # Find pool term begin (PS leader step_up)
                if self._match_ps_step_up(fname, line, pid, rank):
                    continue

                # Find pool term end (PS leader step_down)
                if self._match_ps_step_down(fname, line, pid, rank):
                    continue

                # Find pool map updates
                if self._match_ps_pmap_update(fname, line, pid, rank):
                    continue

                # Find rebuild start by the PS leader
                if self._match_ps_rb_start(fname, line, pid, rank):
                    continue

                if self._match_legacy_ps_rb_start(fname, line, pid, rank):
                    continue

                if self._match_ps_rb_status(fname, line, pid, rank):
                    continue

                if self._match_legacy_ps_rb_status(fname, line, pid, rank):
                    continue

            # Future: for a PID that is killed, clear any associated cur_ldr_rank / cur_ldr_pid.
            # At logfile end, it could be due to engine killed, or could just be log rotation.

    def print_pools(self):
        """Print all pools important events found in a nested dictionary"""

        # pd (pool dictionary): pool UUID -> td
        # td (term dictionary): term number -> "maps" md
        # md (map dictionary): pool map version number -> "rb_gens" rd
        # rd (rebuild dictionary): rebuild generation number -> rebuild operation details
        for puuid, pd in self._pools.items():
            print(f"===== Pool {puuid}:")
            for term, td in pd.items():
                b = td["begin_time"]
                e = td["end_time"]
                r = td["rank"]
                h = td["host"]
                p = td["pid"]
                f = td["logfile"]
                # Print term begin
                print(f"{b} {puuid} BEGIN  term {term}\trank {r}\t{h}\tPID {p}\t{f}")

                # Print pool map updates that happened within the term
                for v, md in td["maps"].items():
                    # Future: print tgt state changes before corresponding map updates

                    # Print map updates
                    t = md["time"]
                    from_ver = md["from_ver"]
                    print(f"{t} {puuid} MAPVER {from_ver}->{v}")

                    # Print rebuilds
                    for g, rd in md["rb_gens"].items():
                        op = rd["op"]
                        dur = rd["duration"]
                        scan = rd["scanning"]
                        pull = rd["pulling"]
                        comp = rd["completed"]
                        abrt = rd["aborted"]
                        st = rd["start_time"]
                        ut = rd["time"]

                        # status is latest status reached for the given rebuild
                        status = "started"
                        if abrt:
                            status = "aborted"
                        elif comp:
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
        """Sort the nested dictionary of pools by pool service term"""
        for puuid, pd in self._pools.items():
            tmp = dict(sorted(pd.items()))
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

    print(f"\n======== Pools Report ({len(sp.get_warnings())} warnings from scanning) ========\n")
    sp.sort()
    sp.print_pools()


if __name__ == '__main__':
    run()
