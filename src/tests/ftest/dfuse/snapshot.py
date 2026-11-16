"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from run_utils import run_remote


class DfuseSnapshot(TestWithServers):
    """Test mounting a container snapshot with dfuse.

    :avocado: recursive
    """

    def _run(self, command, env='', should_pass=True):
        """Run a command on the client hosts and check its result.

        Args:
            command (str): the command to run
            env (str, optional): shell code to run first, e.g. to set LD_PRELOAD. Defaults to ''.
            should_pass (bool, optional): whether the command is expected to succeed.
                Defaults to True.

        Returns:
            CommandResult: the result of the command

        """
        result = run_remote(self.log, self.hostlist_clients, env + command)
        if result.passed != should_pass:
            expected = 'succeed' if should_pass else 'fail'
            self.fail(f'Expected "{command}" to {expected} on {self.hostlist_clients}')
        return result

    def _remove_dir(self, path):
        """Remove a directory on the client hosts, used as a test cleanup method.

        Args:
            path (str): directory to remove

        Returns:
            list: a list of any errors detected

        """
        result = run_remote(self.log, self.hostlist_clients, f'rmdir {path}')
        return [] if result.passed else [f'Failed to remove {path} on {result.failed_hosts}']

    def _md5(self, path, env=''):
        """Get the md5 sum of a file.

        Args:
            path (str): file to checksum
            env (str, optional): shell code to run first. Defaults to ''.

        Returns:
            str: the md5 sum

        """
        result = self._run(f'md5sum {path}', env)
        return result.joined_stdout.split()[0]

    def run_snapshot(self, il_lib=None, by_epoch=False):
        """Mount a snapshot with dfuse and verify what it shows.

        Args:
            il_lib (str, optional): interception library to use when accessing the snapshot,
                libioil.so or libpil4dfs.so. Defaults to None, the access is through the kernel.
            by_epoch (bool, optional): select the snapshot by epoch instead of by name.
                Defaults to False.
        """
        snap_name = 'dfuse_snap'
        env = ''
        report_env = ''
        engaged = None
        if il_lib:
            lib_path = os.path.join(self.prefix, 'lib64', il_lib)
            # D_IL_NO_BYPASS makes libpil4dfs intercept every application, by default it skips
            # many commands, such as ls, touch, rm, mv and truncate used below
            env = f'export LD_PRELOAD={lib_path}; export D_IL_NO_BYPASS=1; '
            report_env = env + 'export D_IL_REPORT=1; '
            engaged = '[libioil] Performed' if il_lib == 'libioil.so' else 'interception ON'

        # The interception libraries only intercept when the kernel caches are off
        dfuse_params = {'disable_caching': True, 'disable_wb_cache': True} if il_lib else {}

        self.log_step('Creating a pool and a POSIX container')
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.log_step('Mounting the container and creating the original content')
        live = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, live, pool, container, **dfuse_params)
        live_dir = live.mount_dir.value
        self._run(f'mkdir {live_dir}/dir && '
                  f'seq 1 200000 > {live_dir}/a.txt && '
                  f'dd if=/dev/urandom of={live_dir}/dir/big.bin bs=1M count=4 && '
                  f'echo "deleted later" > {live_dir}/b.txt && '
                  f'sync')
        original = {name: self._md5(f'{live_dir}/{name}') for name in ('a.txt', 'dir/big.bin',
                                                                      'b.txt')}
        original_listing = self._run(f'ls -R {live_dir} | sed "s|^{live_dir}|ROOT|"').joined_stdout

        self.log_step('Creating a snapshot')
        container.create_snap(snap_name=snap_name)
        self.log.info('Snapshot %s created at epoch %s', snap_name, container.epoch)

        self.log_step('Changing the live container after the snapshot')
        self._run(f'seq 1 10 > {live_dir}/a.txt && '
                  f'rm {live_dir}/b.txt && '
                  f'echo "new" > {live_dir}/new.txt && '
                  f'dd if=/dev/urandom of={live_dir}/dir/big.bin bs=1M count=1 && '
                  f'sync')
        modified_a = self._md5(f'{live_dir}/a.txt')
        if modified_a == original['a.txt']:
            self.fail('The live a.txt was not modified')

        self.log_step(f'Mounting the snapshot {"by epoch" if by_epoch else "by name"}')
        snap = get_dfuse(self, self.hostlist_clients)
        if by_epoch:
            dfuse_params['snap_epoch'] = container.epoch
        else:
            dfuse_params['snap'] = snap_name
        start_dfuse(self, snap, pool, container, **dfuse_params)
        snap_dir = snap.mount_dir.value

        self.log_step('Verifying the snapshot shows the original content')
        for name, checksum in original.items():
            found = self._md5(f'{snap_dir}/{name}', env)
            if found != checksum:
                self.fail(f'{name} in the snapshot has md5 {found}, expected {checksum}')
        listing = self._run(f'ls -R {snap_dir} | sed "s|^{snap_dir}|ROOT|"', env).joined_stdout
        if listing != original_listing:
            self.fail(f'Snapshot listing differs from the original:\n{listing}\n'
                      f'!=\n{original_listing}')
        self._run(f'test ! -e {snap_dir}/new.txt', env)

        self.log_step('Verifying the live container still shows the changes')
        self._run(f'test ! -e {live_dir}/b.txt && test -e {live_dir}/new.txt')

        if il_lib:
            self.log_step('Verifying the interception library is used on the snapshot')
            result = self._run(f'out=$(md5sum {snap_dir}/a.txt 2>&1); echo "$out"', report_env)
            if engaged not in result.joined_stdout:
                self.fail(f'No "{engaged}" in the output of {il_lib}:\n{result.joined_stdout}')

        self.log_step('Verifying the live container and the snapshot in the same process')
        result = self._run(f'md5sum {live_dir}/a.txt {snap_dir}/a.txt', env)
        hashes = [line.split()[0] for line in result.joined_stdout.splitlines()]
        if hashes != [modified_a, original['a.txt']]:
            self.fail(f'Expected md5 sums {[modified_a, original["a.txt"]]}, got {hashes}')

        self.log_step('Verifying the snapshot cannot be modified')
        for command in (f'touch {snap_dir}/created.txt',
                        f'rm {snap_dir}/b.txt',
                        f'mkdir {snap_dir}/newdir',
                        f'mv {snap_dir}/a.txt {snap_dir}/moved.txt',
                        f'truncate -s 0 {snap_dir}/a.txt',
                        f'bash -c "echo changed > {snap_dir}/a.txt"',
                        f'bash -c "echo changed >> {snap_dir}/b.txt"'):
            self.log_step(f'Running {command}, which must fail')
            self._run(command, env, should_pass=False)
        for name, checksum in original.items():
            found = self._md5(f'{snap_dir}/{name}', env)
            if found != checksum:
                self.fail(f'{name} in the snapshot has md5 {found} after the write attempts, '
                          f'expected {checksum}')

        self.log.info('Test passed')

    def test_dfuse_snapshot(self):
        """Mount a snapshot by name.

        Test Description:
            Create files, take a snapshot, change the files, mount the snapshot with
            dfuse --snap and check that it shows the original content and is read-only.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse
        :avocado: tags=DfuseSnapshot,test_dfuse_snapshot
        """
        self.run_snapshot()

    def test_dfuse_snapshot_epoch(self):
        """Mount a snapshot by epoch.

        Test Description:
            Same as test_dfuse_snapshot but the snapshot is selected with --snap-epoch.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse
        :avocado: tags=DfuseSnapshot,test_dfuse_snapshot_epoch
        """
        self.run_snapshot(by_epoch=True)

    def test_dfuse_snapshot_ioil(self):
        """Access a snapshot with the libioil interception library.

        Test Description:
            Same as test_dfuse_snapshot but the snapshot is accessed with libioil and the live
            container and the snapshot, which share a container uuid, are read by one process.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse,ioil
        :avocado: tags=DfuseSnapshot,test_dfuse_snapshot_ioil
        """
        self.run_snapshot(il_lib='libioil.so')

    def test_dfuse_snapshot_pil4dfs(self):
        """Access a snapshot with the libpil4dfs interception library.

        Test Description:
            Same as test_dfuse_snapshot but the snapshot is accessed with libpil4dfs and the live
            container and the snapshot, which share a container uuid, are read by one process.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse,pil4dfs
        :avocado: tags=DfuseSnapshot,test_dfuse_snapshot_pil4dfs
        """
        self.run_snapshot(il_lib='libpil4dfs.so')

    def test_dfuse_snapshot_options(self):
        """Check that invalid snapshot options are rejected by dfuse.

        Test Description:
            dfuse must refuse --snap together with --snap-epoch, a bad epoch, a snapshot without
            a container and a snapshot that does not exist, without mounting anything.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,dfuse
        :avocado: tags=DfuseSnapshot,test_dfuse_snapshot_options
        """
        pool = self.get_pool(connect=False)
        container = self.get_container(pool)
        mount_dir = f'/tmp/daos_dfuse_snap_options_{self.test_id}'
        dfuse = os.path.join(self.bin, 'dfuse')
        base = f'{dfuse} --mountpoint={mount_dir} --pool={pool.identifier}'
        full = f'{base} --container={container.identifier}'

        self._run(f'mkdir -p {mount_dir}')
        self.register_cleanup(self._remove_dir, path=mount_dir)
        for name, command, message in (
                ('both --snap and --snap-epoch', f'{full} --snap=a --snap-epoch=5',
                 'cannot be used together'),
                ('a non numeric epoch', f'{full} --snap-epoch=abc', 'Invalid snapshot epoch'),
                ('a negative epoch', f'{full} --snap-epoch=-1', 'Invalid snapshot epoch'),
                ('a null epoch', f'{full} --snap-epoch=0', 'Invalid snapshot epoch'),
                ('a snapshot without a container', f'{base} --snap=a',
                 'requires both a pool and a container'),
                ('an unknown snapshot', f'{full} --snap=does_not_exist',
                 'Failed to connect to container')):
            self.log_step(f'Mounting with {name}, which must fail')
            result = self._run(command, should_pass=False)
            if message not in result.joined_stdout:
                self.fail(f'Expected "{message}" in the output of "{command}":\n'
                          f'{result.joined_stdout}')
            self._run(f'! mountpoint -q {mount_dir}')
