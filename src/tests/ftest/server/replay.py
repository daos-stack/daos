"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
import time

from apricot import TestWithServers

from dfuse_utils import get_dfuse, start_dfuse, stop_dfuse
from general_utils import join
from ior_utils import get_ior
from job_manager_utils import get_job_manager
from test_utils_pool import add_pool


class ReplayTests(TestWithServers):
    """Shutdown/restart/replay test cases.

    Restarting engines with volatile SCM will include loading the blob from the SSD and re-applying
    any changes from the WAL.

    :avocado: recursive
    """

    def create_container(self, details=None, **pool_params):
        """Create a pool and container.

        Args:
            details (str, optional): additional log_step messaging
            pool_params (dict, optional): named arguments to add_pool()

        Returns:
            TestContainer: the created container with a reference to the created pool
        """
        self.log_step(join(' ', 'Creating a pool (dmg pool create)', '-', details))
        pool = add_pool(self, **pool_params)
        self.log_step(join(' ', 'Creating a container (daos container create)', '-', details))
        return self.get_container(pool)

    def write_data(self, container, ppn, dfuse=None):
        """Write data to the container/dfuse using ior.

        Args:
            container (TestContainer): the container to populate
            ppn (int): processes per node to use with the ior command
            dfuse (Dfuse, optional): dfuse object defining the dfuse mount point. Defaults to None.

        Returns:
            Ior: the Ior object used to populate the container
        """
        job_manager = get_job_manager(self, subprocess=False, timeout=60)
        ior = get_ior(
            self, job_manager, self.hostlist_clients, self.workdir, None,
            namespace='/run/ior_write/*')
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)
        return ior

    def stop_engines(self):
        """Stop each server engine and verify they are not running."""
        self.log_step('Shutting down the engines (dmg system stop)')
        self.get_dmg_command().system_stop(True)

        # Verify all ranks have stopped
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['stopped', 'excluded'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to stop', rank_check)
            self.fail('Failed to stop ranks cleanly')

    def restart_engines(self):
        """Restart each server engine and verify they are running."""
        self.log_step('Restarting the engines (dmg system start)')
        self.get_dmg_command().system_start()

        # Verify all ranks have started
        all_ranks = self.server_managers[0].get_host_ranks(self.server_managers[0].hosts)
        rank_check = self.server_managers[0].check_rank_state(all_ranks, ['joined'], 5)
        if rank_check:
            self.log.info('Ranks %s failed to start', rank_check)
            self.fail('Failed to start ranks cleanly')

    def read_data(self, ior, container, ppn, dfuse=None):
        """Verify the data used to populate the container.

        Args:
            ior (Ior): the ior command used to populate the container
            container (TestContainer): the container to verify
            ppn (int): processes per node to use with the ior command
            dfuse (Dfuse, optional): dfuse object defining the dfuse mount point. Defaults to None.
        """
        ior.update('flags', self.params.get('flags', '/run/ior_read/*'))
        ior.run(self.server_group, container.pool, container, None, ppn, dfuse=dfuse)

    def verify_snapshots(self, container, expected):
        """Verify the snapshots listed for the container match the expected list of snapshots.

        Args:
            container (TestContainer): the container from which to get the detected snapshots
            expected (list): the expected lists of snapshots

        Raises:
            TestFail: if the detected list of snapshots does not match the detected list
        """
        self.log.debug("Expected list of snapshots: %s", expected)
        detected = [entry["epoch"] for entry in container.list_snaps()["response"]]
        self.assertListEqual(
            sorted(expected), sorted(detected), 'Detected snapshots does not match expected')

    def test_restart(self):
        """Verify data access after engine restart w/ WAL replay + w/ check pointing (DAOS-13009).

        Tests un-synchronized WAL & VOS

        Steps:
            0) Start 2 DAOS servers with 1 engines on each server (setup)
            1) Create a single pool and container
            2) Run ior w/ DFS to populate the container with data
            3) After ior has completed, shutdown every engine cleanly (dmg system stop)
            4) Remove VOS file manually/temporarily (umount tmpfs; remount tmpfs)
            5) Restart each engine (dmg system start)
            6) Verify the previously written data matches with an ior read

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_restart
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        container = self.create_container()

        self.log_step('Write data to the container (ior)')
        ior = self.write_data(container, ppn)

        self.stop_engines()
        self.restart_engines()

        self.log_step('Verifying data previously written to the container (ior)')
        self.read_data(ior, container, ppn)
        self.log_step('Test passed')

    def test_replay_posix(self):
        """Verify POSIX data access after engine restart (DAOS-13010).

        Steps:
            0) Start 2 DAOS servers with 1 engines on each server (setup)
            1) Create a single pool and a POSIX container
            2) Start dfuse
            3) Write and then read data to the dfuse mount point
            4) After the read has completed, unmount dfuse
            5) Shutdown every engine cleanly (dmg system stop)
            6) Restart each engine (dmg system start)
            7) Remount dfuse
            8) Verify the previously written data exists
            9) Verify more data can be written

        :avocado: tags=all,pr
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_restart_posix
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        container = self.create_container()

        self.log_step('Start dfuse')
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, container.pool, container)

        self.log_step('Write data to the dfuse mount point (ior)')
        ior = self.write_data(container, ppn, dfuse)

        self.log_step('After the read has completed, unmount dfuse')
        stop_dfuse(self, dfuse)

        self.stop_engines()
        self.restart_engines()

        self.log_step('Remount dfuse')
        start_dfuse(self, dfuse)

        self.log_step('Verifying data previously written to the dfuse mount point (ior)')
        self.read_data(ior, container, ppn, dfuse)

        self.log_step('Write additional data to the dfuse mount point (ior)')
        ior = self.write_data(container, ppn, dfuse)

        self.log.info('Test passed')

    def test_replay_snapshots(self):
        """Verify POSIX data access after engine restart (DAOS-13010).

        Steps:
            0) Start 2 DAOS servers with 1 engine per server
            1) Create a single pool and container in the pool
            2) Run ior w/ DFS to populate the container with persistent data
            3) Creating a snapshot (daos container create-snap)
            4) Repeat steps #2 & #3 three times.
            5) Verify all three snapshots exist (daos container list-snaps)
            6) Remove the second snapshot (daos container destroy-snap)
            7) Verify two snapshots exist (daos container list-snaps)
            8) Shutdown every engine cleanly (dmg system stop --force)
            9) Restart each engine (dmg system start)
            10) Verify all engines have joined (dmg system query)
            11) Verify two snapshots exist (daos container list-snaps)
            12) Remove the remaining snapshots (daos container destroy-snap)
            13) Verify no snapshots exist (daos container list-snaps)

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_replay_snapshots
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        container = self.create_container()

        snapshots = []
        for index in range(1, 4):
            step = join(' ', index, 'of', 3)
            self.log_step(join(' ', 'Write data to the container (ior)', '-', step))
            self.write_data(container, ppn)

            self.log_step(join(' ', 'Creating a snapshot (daos container create-snap)', '-', step))
            snapshots.append(container.create_snap()['response']['epoch'])

        self.log_step('Verifying all three snapshots exist (daos container list-snaps)')
        self.verify_snapshots(container, snapshots)

        self.log_step('Removing the second snapshot (daos container destroy-snap)')
        container.destroy_snap(epc=snapshots.pop(1))

        self.log_step('Verifying two snapshots exist (daos container list-snaps)')
        self.verify_snapshots(container, snapshots)

        self.stop_engines()
        self.restart_engines()

        self.log_step('Verifying two snapshots exist after restart (daos container list-snaps)')
        self.verify_snapshots(container, snapshots)

        self.log_step('Remove the remaining snapshots (daos container destroy-snap)')
        while snapshots:
            container.destroy_snap(epc=snapshots.pop(0))

        self.log_step('Verifying no snapshots exist (daos container list-snaps)')
        self.verify_snapshots(container, snapshots)

        self.log_step('Test passed')

    def test_replay_attributes(self):
        """Verify POSIX data access after engine restart (DAOS-13010).

        Steps:
            0) Start 3 DAOS servers with 1 engine on each server
            1) Create a multiple pools and containers
            2) List the current pool and container attributes
            3) Modify at least one different attribute on each pool and container
            4) Shutdown every engine cleanly (dmg system stop)
            5) Restart each engine (dmg system start)
            6) Verify each modified pool and container attribute is still set

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_replay_attributes
        """
        containers = []
        for index in range(1, 4):
            containers.append(self.create_container(join(' ', index, 'of', 3)))

        self.log_step('List the current pool and container attributes')
        expected = {}
        for container in containers:
            for item in (container.pool, container):
                expected[item.identifier] = item.get_prop()['response']

        self.log_step('Modify at least one different attribute on each pool and container')
        modify_attributes = [
            # Settable pool attributes
            {'checkpoint_freq': list(range(1, 10)),
             'checkpoint_thresh': list(range(25, 75)),
             'scrub-freq': list(range(604200, 605200))},
            # Settable container attributes
            {'label': [join('_', 'RenamedContainer', str(num)) for num in range(10, 20)]},
        ]
        for container in containers:
            for index, item in enumerate((container.pool, container)):
                # Modify a random pool/container property value
                name = random.choice(list(modify_attributes[index].keys()))  # nosec
                modified = False
                for entry in expected[item.identifier]:
                    if entry['name'] == name:
                        original = entry['value']
                        while entry['value'] == original:
                            entry['value'] = random.choice(modify_attributes[index][name])  # nosec
                        self.log.info(
                            'Modifying %s property: %s -> %s',
                            item.identifier, entry['name'], entry['value'])
                        if index == 0:
                            kwargs = {'properties': join(':', entry['name'], entry['value'])}
                        else:
                            kwargs = {'prop': entry['name'], 'value': entry['value']}
                        item.set_prop(**kwargs)
                        modified = True
                        if entry['name'] == 'label':
                            item.label.update(entry['value'], join('.', item.identifier, 'label'))
                            expected[entry['value']] = expected[original]
                        break
                if not modified:
                    self.fail('Missing {} {} attribute to modify'.format(item.identifier, name))

        self.stop_engines()
        self.restart_engines()

        self.log_step('Verify each modified pool and container attribute is still set')
        errors = []
        for container in containers:
            for item in (container.pool, container):
                detected = item.get_prop()['response']
                for entry in expected[item.identifier]:
                    if entry not in detected:
                        errors.append(
                            join(' ', 'Expected', item.identifier, 'property not detected:', entry))
                for entry in detected:
                    if entry not in expected[item.identifier]:
                        errors.append(
                            join(' ', 'Detected', item.identifier, 'property not expected:', entry))
        if errors:
            for error in errors:
                self.log.info(error)
            self.fail('Errors detected with pool/container properties after engine restart')

        self.log_step('Test passed')

    def test_replay_no_check_pointing(self):
        """Verify data access after engine restart w/ WAL replay + w/o check pointing (DAOS-13013).

        Steps:
            0) Start 3 DAOS servers with 1 engine on each server
            1) Create a single pool and container
            2) Disable check pointing
            3) Run ior w/ DFS to populate the container with small amount of data
            4) After ior has completed, shutdown every engine cleanly (dmg system stop)
            5) Restart each engine (dmg system start)
            6) Verify the previously written data matches with an ior read

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_replay_no_check_pointing
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        container = self.create_container()

        self.log_step('Disabling check pointing on {}'.format(container.pool))
        container.pool.set_prop(properties='checkpoint:disabled')
        response = container.pool.get_prop(name='checkpoint')['response']
        if response[0]['value'] != 'disabled':
            self.fail('Pool check pointing not disabled before engine restart')

        self.log_step('Write data to the container (ior)')
        ior = self.write_data(container, ppn)

        self.stop_engines()
        self.restart_engines()

        self.log_step(
            'Verifying check pointing is disabled on {} after engine restart'.format(
                container.pool))
        response = container.pool.get_prop(name='checkpoint')['response']
        if response[0]['value'] != 'disabled':
            self.fail('Pool check pointing not disabled after engine restart')

        self.log_step('Verifying data previously written to the container (ior)')
        self.read_data(ior, container, ppn)
        self.log_step('Test passed')

    def test_replay_check_pointing(self):
        """Verify data access after engine restart w/ WAL replay + w/ check pointing (DAOS-13012).

        Steps:
            0) Start 3 DAOS servers with 1 engine on each server
            1) Create a single pool and container
            2) Determine the check pointing interval
            3) Run ior w/ DFS to populate the container with small amount of data
            4) After ior has completed, wait for the check pointing to complete
            5) Shutdown every engine cleanly (dmg system stop)
            6) Restart each engine (dmg system start)
            7) Verify the previously written data matches with an ior read

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=server,replay
        :avocado: tags=ReplayTests,test_replay_check_pointing
        """
        ppn = self.params.get('ppn', '/run/ior_write/*', 1)
        frequency = 5
        container = self.create_container(
            properties=f'checkpoint:timed,checkpoint_freq:{frequency}')
        self.log.info('%s check point frequency: %s seconds', container.pool, frequency)

        self.log_step('Write data to the container (ior)')
        ior = self.write_data(container, ppn)

        self.log_step('Waiting for check pointing to complete (sleep {})'.format(frequency * 2))
        time.sleep(frequency * 2)

        self.stop_engines()
        self.restart_engines()

        self.log_step('Verifying data previously written to the container (ior)')
        self.read_data(ior, container, ppn)
        self.log_step('Test passed')
