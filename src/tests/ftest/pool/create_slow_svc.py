"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import socket
import time

from apricot import TestWithServers
from run_utils import run_local

# Fault injection locations (DAOS_FAIL_UNIT_TEST_GROUP_LOC | id) armed with DAOS_FAIL_ONCE, see
# src/include/daos/common.h. An engine holds a single fail_loc at a time.
DAOS_FAIL_ONCE = 0x1000000
DAOS_RSVC_CREATE_SLOW = 0x10000 | 0xa6
DAOS_MGMT_TGT_DESTROY_SLOW = 0x10000 | 0xa7

# Debug messages logged by dmg on its stderr (dmg runs with -d)
DMG_DEBUG_ENABLED = r"debug output enabled"
DMG_RETRYING_MS_REQUEST = r"retrying MS request"

# Error messages logged by the engine hosting the management service leader when a pool create
# attempt fails in ds_mgmt_create_pool() (src/mgmt/srv_pool.c); {uuid} is the pool UUID prefix
ENGINE_SVC_CREATE_TIMEDOUT = r"create pool {uuid} svc failed: rc DER_TIMEDOUT"
ENGINE_ROLLBACK_TIMEDOUT = r"{uuid}: failed to clean up failed pool: DER_TIMEDOUT"


class PoolCreateSlowSvc(TestWithServers):
    """Test pool creation with slow pool service replica creation (DAOS-19608).

    The RSVC_START collective RPC creating the pool service replicas is bound by the size-tiered
    pool create timeout (at least 15s) rather than by the generic cart RPC timeout (crt_timeout),
    which may be configured much lower. With a tight crt_timeout, a slow engine used to make the
    pool create fail with DER_TIMEDOUT and the control plane retried the create.

    When the replica creation outlives the RSVC_START timeout, the pool create fails with
    DER_TIMEDOUT and rolls back the pool targets: the rollback destroy is granted at least the
    time granted to the replica creation and a retry of the create with the same pool UUID waits
    for an in-flight destroy of that pool instead of failing with DER_EXIST (or DER_BUSY).

    The faults are only armed on engines other than the one hosting the management service
    leader, which drives the pool create: the RPCs the leader engine sends to itself are handled
    locally and cannot time out. The pool is created on an odd number of ranks with as many
    service replicas, so that every pool rank hosts a replica: the replica creation is delayed on
    one or more remote pool ranks and, optionally, the target destroy on another remote pool rank.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.leader_rank = self.get_leader_rank()
        ranks = sorted(self.server_managers[0].ranks)
        remote_ranks = [rank for rank in ranks if rank != self.leader_rank]
        if len(ranks) % 2 == 0:
            # Drop the highest remote rank to get an odd number of pool ranks
            ranks.remove(remote_ranks.pop())
        if len(remote_ranks) < 2:
            self.fail("At least 2 engines besides the MS leader's one are needed")
        crt_timeout = self.server_managers[0].get_config_value("crt_timeout")
        if crt_timeout is None:
            self.fail("The server_config crt_timeout must be set")
        self.crt_timeout_ms = crt_timeout * 1000
        self.pool_ranks = ranks
        self.destroy_rank = remote_ranks[-1]
        self.slow_ranks = remote_ranks[:-1]
        self.log.info(
            "MS leader rank: %s, pool ranks: %s, slow replica creation ranks: %s, "
            "slow target destroy rank: %s, crt_timeout: %s ms",
            self.leader_rank, self.pool_ranks, self.slow_ranks, self.destroy_rank,
            self.crt_timeout_ms)
        self.register_cleanup(self.disarm_faults)

    def get_leader_rank(self):
        """Get the rank of the engine hosting the management service leader.

        Returns:
            int: the engine rank hosting the MS leader
        """
        leader = self.get_dmg_command().system_leader_query()["response"]["current_leader"]
        leader_host = socket.gethostbyaddr(leader.split(":")[0])[0].split(".")[0]
        for rank, host in self.server_managers[0].ranks.items():
            if host.split(".")[0] == leader_host:
                return rank
        self.fail(f"Rank of the MS leader {leader} not found in {self.server_managers[0].ranks}")
        return None

    def set_fail_loc(self, ranks, fail_loc, fail_value=0):
        """Set the fail_loc and fail_value of the given engines.

        Args:
            ranks (list): ranks of the engines to set
            fail_loc (int): fault injection location to set (0 to disable fault injection)
            fail_value (int, optional): fault injection value to set. Defaults to 0.

        Returns:
            bool: whether the parameters were set on every engine
        """
        passed = True
        for rank in ranks:
            command = " ".join([
                os.path.join(self.bin, "daos_debug_set_params"), "-r", str(rank),
                "-v", str(fail_loc), "-V", str(fail_value)])
            passed &= run_local(self.log, command, timeout=60).passed
        return passed

    def arm_faults(self, create_rsvc_delay_ms, destroy_tgt_delay_ms):
        """Delay the next replica creation of the slow ranks and/or destroy of the destroy rank.

        Without a target destroy delay the replica creation of the destroy rank is delayed as well.

        Args:
            create_rsvc_delay_ms (int): RSVC create fault delay in milliseconds
            destroy_tgt_delay_ms (int): target destroy fault delay in milliseconds
        """
        slow_ranks = self.slow_ranks + ([self.destroy_rank] if destroy_tgt_delay_ms == 0 else [])
        self.log_step(
            f"Arming a {create_rsvc_delay_ms} ms pool service replica creation delay on ranks "
            f"{slow_ranks}")

        if not self.set_fail_loc(
                slow_ranks, DAOS_RSVC_CREATE_SLOW | DAOS_FAIL_ONCE, create_rsvc_delay_ms):
            self.fail("Failed to arm the DAOS_RSVC_CREATE_SLOW fault on the slow ranks")

        if destroy_tgt_delay_ms == 0:
            return

        self.log_step(
            f"Arming a {destroy_tgt_delay_ms} ms target destroy delay on rank {self.destroy_rank}")
        if not self.set_fail_loc(
                [self.destroy_rank], DAOS_MGMT_TGT_DESTROY_SLOW | DAOS_FAIL_ONCE,
                destroy_tgt_delay_ms):
            self.fail("Failed to arm the DAOS_MGMT_TGT_DESTROY_SLOW fault on the destroy rank")

    def disarm_faults(self):
        """Disable fault injection on every engine.

        Returns:
            list: a list of any errors detected when disabling fault injection
        """
        if not self.set_fail_loc(sorted(self.server_managers[0].ranks), 0):
            return ["Failed to disable fault injection on the engines"]
        return []

    def create_pool_with_slow_svc(self, create_rsvc_delay_ms, destroy_tgt_delay_ms=0):
        """Create a pool while the creation of its service replicas is delayed.

        Args:
            create_rsvc_delay_ms (int): RSVC create fault delay in milliseconds
            destroy_tgt_delay_ms (int, optional): target destroy fault delay in milliseconds.
                Defaults to 0 (disabled).

        Returns:
            tuple: the created pool (TestPool) and the number of times dmg retried the
                pool create request (int)
        """
        if create_rsvc_delay_ms <= 0:
            self.fail(f"Invalid create_rsvc_delay_ms={create_rsvc_delay_ms}: must be > 0")
        if destroy_tgt_delay_ms < 0:
            self.fail(f"Invalid destroy_tgt_delay_ms={destroy_tgt_delay_ms}: must be >= 0")

        self.arm_faults(create_rsvc_delay_ms, destroy_tgt_delay_ms)

        self.log_step(f"Creating a pool on ranks {self.pool_ranks} with as many service replicas")
        pool = self.get_pool(create=False, connect=False)
        pool.target_list.update(self.pool_ranks, "pool.target_list")
        pool.svcn.update(len(self.pool_ranks), "pool.svcn")
        start = time.time()
        pool.create()
        elapsed = time.time() - start
        retries = self.get_create_retries(pool.dmg.result)
        self.log.info(
            "Pool %s created in %.1f seconds with %d dmg retries", pool.identifier, elapsed,
            retries)
        # The delayed replica creation is always waited for, and the delayed rollback destroy only
        # starts once the first create attempt has timed out, i.e. at least crt_timeout after the
        # create was issued (15 s with the fix, which raises the RSVC_START timeout to a floor).
        min_elapsed_ms = create_rsvc_delay_ms
        if destroy_tgt_delay_ms > 0:
            min_elapsed_ms = max(min_elapsed_ms, destroy_tgt_delay_ms + self.crt_timeout_ms)
        if elapsed * 1000 < min_elapsed_ms:
            self.fail(
                f"Pool created in {elapsed:.1f}s, faster than the {min_elapsed_ms} ms implied by "
                "the injected delays: a fault was not injected")

        self.log_step("Verifying the pool service replicas and enabled ranks (dmg pool query)")
        svc_ranks = sorted(pool.svc_ranks)
        if svc_ranks != self.pool_ranks:
            self.fail(f"Invalid pool service replicas: want={self.pool_ranks}, got={svc_ranks}")
        data = pool.query(show_enabled=True)
        enabled_ranks = data["response"].get("enabled_ranks")
        if enabled_ranks != self.pool_ranks:
            self.fail(f"Invalid enabled ranks: want={self.pool_ranks}, got={enabled_ranks}")

        return pool, retries

    def get_create_retries(self, result):
        """Get the number of times dmg retried the pool create request.

        The dmg pool create is run with debug output enabled and logs a 'retrying MS request'
        message each time the management service request is retried, e.g. after a DER_TIMEDOUT.

        Args:
            result (CmdResult): result of the dmg pool create command

        Returns:
            int: the number of retries of the pool create request
        """
        stderr = result.stderr_text
        if DMG_DEBUG_ENABLED not in stderr:
            self.fail("The dmg pool create was not run with debug output enabled")
        return stderr.count(DMG_RETRYING_MS_REQUEST)

    def verify_create_retried(self, retries):
        """Verify that dmg retried the pool create request exactly once.

        The first attempt must have timed out, and the retried create must not have timed out too
        (which happens when the rollback of the first attempt outlives the retried create).

        Args:
            retries (int): the number of times dmg retried the pool create request
        """
        self.log_step("Verifying that dmg retried the pool create exactly once")
        if not retries:
            self.fail("The pool create was not retried by dmg: its first attempt did not time out")
        if retries > 1:
            self.fail(
                f"The pool create was retried {retries} times by dmg: its retry timed out too")

    def verify_leader_log(self, pool, message, expected):
        """Verify whether the engine hosting the MS leader logged a message about the pool create.

        Args:
            pool (TestPool): the pool created by the test
            message (str): message to search for in the engine logs, with a {uuid} placeholder for
                the pool UUID prefix printed by the engine
            expected (bool): whether the message must (True) or must not (False) have been logged
        """
        pattern = message.format(uuid=pool.uuid.lower()[:8])
        result = self.server_managers[0].search_engine_logs(pattern)
        found = any(data.passed and not data.timeout for data in result.output)
        if found != expected:
            self.fail(
                f"'{pattern}' {'not ' if expected else ''}found in the engine logs of "
                f"{self.server_managers[0].hosts}")

    def verify_rollback(self, pool, timed_out):
        """Verify the rollback of the timed-out first pool create attempt in the engine logs.

        Args:
            pool (TestPool): the pool created by the test
            timed_out (bool): whether the rollback destroy is expected to have timed out
        """
        self.log_step(
            "Verifying that the first create attempt timed out and that its rollback "
            f"{'timed out' if timed_out else 'completed in time'} (engine logs)")
        self.verify_leader_log(pool, ENGINE_SVC_CREATE_TIMEDOUT, True)
        self.verify_leader_log(pool, ENGINE_ROLLBACK_TIMEDOUT, timed_out)

    def destroy_pool(self, pool):
        """Destroy the pool and log the time it took.

        Args:
            pool (TestPool): the pool to destroy
        """
        self.log_step("Destroying the pool (dmg pool destroy)")
        start = time.time()
        pool.destroy()
        self.log.info("Pool destroyed in %.1f seconds", time.time() - start)

    def test_pool_create_slow_svc(self):
        """Create a pool whose service replica creation outlives crt_timeout.

        Test Description:
            Delay the creation of the pool service replicas longer than crt_timeout, but less than
            the minimum RSVC_START create timeout (15s). The pool create must succeed on its first
            attempt, waiting for the slow replicas: dmg must not have retried the create.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create,fault_injection
        :avocado: tags=PoolCreateSlowSvc,test_pool_create_slow_svc
        """
        delay_ms = self.params.get("create_rsvc_delay_ms", "/run/slow_svc/*")
        pool, retries = self.create_pool_with_slow_svc(delay_ms)

        self.log_step("Verifying that dmg did not retry the pool create")
        if retries:
            self.fail(f"The pool create was retried {retries} time(s) by dmg")

        self.destroy_pool(pool)
        self.log.info("Test passed")

    def test_pool_create_slow_svc_retry(self):
        """Create a pool whose service replica creation outlives the RSVC_START timeout.

        Test Description:
            Delay the creation of the pool service replicas longer than the minimum RSVC_START
            create timeout (15s). The first create attempt fails with DER_TIMEDOUT and its rollback
            has to wait for the slow replicas longer than crt_timeout, but must complete within the
            timeout granted to it (at least the one of the replica creation); the retry of the
            create with the same pool UUID must then succeed.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create,fault_injection
        :avocado: tags=PoolCreateSlowSvc,test_pool_create_slow_svc_retry
        """
        delay_ms = self.params.get("create_rsvc_delay_ms", "/run/slow_svc_retry/*")
        pool, retries = self.create_pool_with_slow_svc(delay_ms)
        self.verify_create_retried(retries)
        self.verify_rollback(pool, timed_out=False)
        self.destroy_pool(pool)
        self.log.info("Test passed")

    def test_pool_create_slow_svc_slow_destroy(self):
        """Create a pool whose failed first attempt is still being rolled back on the retry.

        Test Description:
            Delay the creation of the pool service replicas longer than the minimum RSVC_START
            create timeout (15s) and the target destroy of another engine longer than the rollback
            destroy timeout (at least the 15s granted to the replica creation). The first create
            attempt fails with DER_TIMEDOUT, its rollback times out on the engine with the slow
            target destroy and the retry of the create with the same pool UUID must wait for that
            rollback to complete and succeed.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create,fault_injection
        :avocado: tags=PoolCreateSlowSvc,test_pool_create_slow_svc_slow_destroy
        """
        create_rsvc_delay_ms = self.params.get("create_rsvc_delay_ms",
                                               "/run/slow_svc_slow_destroy/*")
        destroy_tgt_delay_ms = self.params.get("destroy_tgt_delay_ms",
                                               "/run/slow_svc_slow_destroy/*")
        pool, retries = self.create_pool_with_slow_svc(create_rsvc_delay_ms, destroy_tgt_delay_ms)
        self.verify_create_retried(retries)
        self.verify_rollback(pool, timed_out=True)
        self.destroy_pool(pool)
        self.log.info("Test passed")
