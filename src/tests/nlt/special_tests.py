"""NLT: multi-mount, overlay, pydaos and perf tests.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import pickle  # nosec
import subprocess  # nosec
import tempfile
import time
import uuid
from os.path import join

import tabulate

from .base import BoolRatchet, umount
from .client import create_cont, import_daos, run_daos_cmd
from .dfuse import DFuse
from .helpers import check_no_file, create_and_read_via_il, run_tests, stat_and_check
from .logging_utils import log_test


def run_duns_overlay_test(server, conf):
    """Create a DUNS entry point, and then start fuse over it

    Fuse should use the pool/container IDs from the entry point,
    and expose the container.
    """
    # pylint: disable=consider-using-with

    parent_dir = tempfile.TemporaryDirectory(dir=conf.dfuse_parent_dir, prefix='dnt_uns_')

    uns_dir = join(parent_dir.name, 'uns_ep')

    create_cont(conf, pool=server.get_test_pool_obj(), path=uns_dir)

    dfuse = DFuse(server, conf, mount_path=uns_dir, caching=False)

    dfuse.start(v_hint='uns-overlay')
    # To show the contents.
    # getfattr -d <file>

    # This should work now if the container was correctly found
    create_and_read_via_il(dfuse, uns_dir)

    return dfuse.stop()


def run_dfuse(server, conf):
    """Run several dfuse instances"""
    fatal_errors = BoolRatchet()

    pool = server.get_test_pool_obj()

    dfuse = DFuse(server, conf, caching=False)
    try:
        pre_stat = os.stat(dfuse.dir)
    except OSError:
        umount(dfuse.dir)
        raise
    dfuse.start(v_hint='no_pool')
    print(os.statvfs(dfuse.dir))
    subprocess.run(['df', '-h'], check=True)  # nosec
    subprocess.run(['df', '-i', dfuse.dir], check=True)  # nosec
    print('Running dfuse with nothing')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)

    pool_stat = os.stat(join(dfuse.dir, pool.uuid))
    print(f'stat for {pool}')
    print(pool_stat)
    container = create_cont(server.conf, pool, ctype="POSIX")
    cdir = join(dfuse.dir, pool.uuid, container.uuid)
    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, pool=pool, caching=False)
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_only')
    print('Running dfuse with pool only')
    stat_and_check(dfuse, pre_stat)
    check_no_file(dfuse)
    container2 = create_cont(server.conf, pool, ctype="POSIX")
    cpath = join(dfuse.dir, container2.id())
    print(os.listdir(cpath))
    cdir = join(dfuse.dir, container.id())
    create_and_read_via_il(dfuse, cdir)

    fatal_errors.add_result(dfuse.stop())

    dfuse = DFuse(server, conf, container=container, caching=False)
    dfuse.cores = 2
    pre_stat = os.stat(dfuse.dir)
    dfuse.start(v_hint='pool_and_cont')
    print('Running fuse with both')

    stat_and_check(dfuse, pre_stat)

    create_and_read_via_il(dfuse, dfuse.dir)

    run_tests(dfuse)

    fatal_errors.add_result(dfuse.stop())

    if fatal_errors.errors:
        print('Errors from dfuse')
    else:
        print('Reached the end, no errors')
    return fatal_errors.errors


def run_in_fg(server, conf, args):
    """Run dfuse in the foreground.

    Block until Control-C is pressed.
    """
    pool = server.get_test_pool_obj()
    label = 'foreground_cont'
    container = None

    conts = pool.fetch_containers()
    for cont in conts:
        if cont.label == label:
            container = cont.uuid
            break

    # Set to False to run dfuse without a pool.
    pool_on_cmd_line = True

    if not container:
        container = create_cont(conf, pool, label=label, ctype="POSIX")

        # Only set the container cache attributes when the container is initially created so they
        # can be modified later.
        cont_attrs = {'dfuse-data-cache': False,
                      'dfuse-attr-time': 67,
                      'dfuse-dentry-time': 19,
                      'dfuse-dentry-dir-time': 31,
                      'dfuse-ndentry-time': 61,
                      'dfuse-direct-io-disable': False}
        container.set_attrs(cont_attrs)
        container = container.uuid

    dargs = {"caching": True,
             "wbcache": True,
             "multi_user": args.multi_user}

    if pool_on_cmd_line:
        dargs['pool'] = pool.uuid

    dfuse = DFuse(server,
                  conf,
                  **dargs)

    dfuse.log_flush = True
    dfuse.start()

    if pool_on_cmd_line:
        t_dir = join(dfuse.dir, container)
    else:
        t_dir = join(dfuse.dir, pool.uuid, container)

    print(f'Running at {t_dir}')
    print(f'export PATH={join(conf["PREFIX"], "bin")}:$PATH')
    print(f'export LD_PRELOAD={join(conf["PREFIX"], "lib64", "libioil.so")}')
    print(f'export DAOS_AGENT_DRPC_DIR={conf.agent_dir}')
    print('export D_IL_REPORT=-1')
    if args.multi_user:
        print(f'dmg pool --insecure update-acl -e A::root@:rw {pool.id()}')
    print(f'daos container create --type POSIX --path {t_dir}/uns-link')
    print(f'daos container destroy --path {t_dir}/uns-link')
    print(f'daos cont list {pool.label}')

    try:
        if args.launch_cmd:
            start = time.perf_counter()
            # Set the PATH and agent dir.
            agent_env = os.environ.copy()
            agent_env['DAOS_AGENT_DRPC_DIR'] = conf.agent_dir
            agent_env['PATH'] = f'{join(conf["PREFIX"], "bin")}:{agent_env["PATH"]}'
            rc = subprocess.run(args.launch_cmd, check=False, cwd=t_dir, env=agent_env)
            elapsed = time.perf_counter() - start
            dfuse.stop()
            (minutes, seconds) = divmod(elapsed, 60)
            print(f'Completed in {int(minutes):d}:{int(seconds):02d}')
            print(rc)
        else:
            dfuse.wait_for_exit()
    except KeyboardInterrupt:
        pass


def check_readdir_perf(server, conf):
    """Check and report on readdir performance

    Loop over number of files, measuring the time taken to
    populate a directory, and to read the directory contents,
    measure both files and directories as contents, and
    readdir both with and without stat, restarting dfuse
    between each test to avoid cache effects.

    Continue testing until five minutes have passed, and print
    a table of results.
    """
    headers = ['count', 'create\ndirs', 'create\nfiles']
    headers.extend(['dirs', 'files', 'dirs\nwith stat', 'files\nwith stat'])
    headers.extend(['caching\n1st', 'caching\n2nd'])

    results = []

    def make_dirs(parent, count):
        """Populate the test directory"""
        print(f'Populating to {count}')
        dir_dir = join(parent, f'dirs.{count}.in')
        t_dir = join(parent, f'dirs.{count}')
        file_dir = join(parent, f'files.{count}.in')
        t_file = join(parent, f'files.{count}')

        start_all = time.perf_counter()
        if not os.path.exists(t_dir):
            try:
                os.mkdir(dir_dir)
            except FileExistsError:
                pass
            for idx in range(count):
                try:
                    os.mkdir(join(dir_dir, str(idx)))
                except FileExistsError:
                    pass
            dir_time = time.perf_counter() - start_all
            print(f'Creating {count} dirs took {dir_time:.2f}')
            os.rename(dir_dir, t_dir)

        if not os.path.exists(t_file):
            try:
                os.mkdir(file_dir)
            except FileExistsError:
                pass
            start = time.perf_counter()
            for idx in range(count):
                with open(join(file_dir, str(idx)), 'w'):
                    pass
            file_time = time.perf_counter() - start
            print(f'Creating {count} files took {file_time:.2f}')
            os.rename(file_dir, t_file)

        return [dir_time, file_time]

    def print_results():
        """Display the results"""
        print(tabulate.tabulate(results, headers=headers, floatfmt=".2f"))

    pool = server.get_test_pool_obj().uuid

    container = str(uuid.uuid4())

    dfuse = DFuse(server, conf, pool=pool)

    print('Creating container and populating')
    count = 1024
    dfuse.start()
    parent = join(dfuse.dir, container)
    try:
        os.mkdir(parent)
    except FileExistsError:
        pass
    create_times = make_dirs(parent, count)
    dfuse.stop()

    all_start = time.perf_counter()

    while True:

        row = [count]
        row.extend(create_times)
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dir_dir = join(dfuse.dir, f'dirs.{count}')
        file_dir = join(dfuse.dir, f'files.{count}')
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', dir_dir], stdout=subprocess.PIPE, check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()

        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', dir_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()
        dfuse = DFuse(server, conf, pool=pool, container=container,
                      caching=False)
        dfuse.start()
        start = time.perf_counter()
        # Use sort by time here so ls calls stat, if you run ls -l then it will
        # also call getxattr twice which skews the figures.
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        dfuse.stop()

        # Test with caching enabled.  Check the file directory, and do it twice
        # without restarting, to see the effect of populating the cache, and
        # reading from the cache.
        dfuse = DFuse(server,
                      conf,
                      pool=pool,
                      container=container,
                      caching=True)
        dfuse.start()
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        start = time.perf_counter()
        subprocess.run(['/bin/ls', '-t', file_dir], stdout=subprocess.PIPE,
                       check=True)
        elapsed = time.perf_counter() - start
        print(f'processed {count} dirs in {elapsed:.2f} seconds')
        row.append(elapsed)
        results.append(row)

        elapsed = time.perf_counter() - all_start
        if elapsed > 5 * 60:
            dfuse.stop()
            break

        print_results()
        count *= 2
        create_times = make_dirs(dfuse.dir, count)
        dfuse.stop()

    run_daos_cmd(conf, ['container',
                        'destroy',
                        pool,
                        container])
    print_results()


def test_pydaos_kv(server, conf):
    """Test the KV interface"""
    # pylint: disable=consider-using-with

    pydaos_log_file = tempfile.NamedTemporaryFile(prefix='dnt_pydaos_',
                                                  suffix='.log',
                                                  delete=False)

    os.environ['D_LOG_FILE'] = pydaos_log_file.name
    daos = import_daos(server)

    pool = server.get_test_pool_obj()

    cont = create_cont(conf, pool, ctype="PYTHON", label="PYDAOS_NLT")

    container = daos.DCont(pool.uuid, cont.uuid)

    kv = container.dict('my_test_kv')
    kv['a'] = 'a'
    kv['b'] = 'b'
    kv['list'] = pickle.dumps(list(range(1, 100000)))
    for key in range(1, 100):
        kv[str(key)] = pickle.dumps(list(range(1, 10)))
    print(type(kv))
    print(kv)
    print(kv['a'])

    print("First iteration")
    data = {}
    for key in kv:
        print(f'key is {key}, len {len(kv[key])}')
        print(type(kv[key]))
        data[key] = None

    print("Bulk loading")

    data['no-key'] = None

    kv.value_size = 32
    kv.bget(data, value_size=16)
    print("Default get value size %d", kv.value_size)
    print("Second iteration")
    failed = False
    for key, value in data.items():
        if value:
            print(f'key is {key}, len {len(value)}')
        elif key == 'no-key':
            pass
        else:
            failed = True
            print(f'Key is None {key}')

    if failed:
        print("That's not good")

    del kv
    container.destroy('my_test_kv')
    del container

    print('Running PyDAOS container checker')
    daos.check(pool.label, "PYDAOS_NLT")
    # pylint: disable=protected-access
    daos._cleanup()
    log_test(conf, pydaos_log_file.name)


def test_pydaos_kv_obj_class(server, conf):
    """Test the predefined object class works with KV"""
    with tempfile.NamedTemporaryFile(prefix='kv_objclass_pydaos_',
                                     suffix='.log',
                                     delete=False) as tmp_file:
        log_name = tmp_file.name
        os.environ['D_LOG_FILE'] = log_name

    daos = import_daos(server)

    pool = server.get_test_pool_obj()

    cont = create_cont(conf, pool, ctype="PYTHON", label='pydaos_cont')

    container = daos.DCont(pool.label, cont.label)
    failed = False
    # Write kv1 dictionary with OC_S2 object type
    kv1 = container.dict('object1', {"Monday": "1"}, "OC_S2")
    if len(kv1) != 1:
        failed = True
        print(f'Expected length of kv object is 1 but got {len(kv1)}')

    # Write kv2 dictionary without any object type,
    # so in this case we have 4 targets so default object type should be S4
    kv2 = container.dict('object2', {"Monday": "1", "Tuesday": "2"})
    if len(kv2) != 2:
        failed = True
        print(f'Expected length of kv object is 2 but got {len(kv2)}')

    # Run a command to list the objects
    cmd = ['cont', 'list-objects', pool.label, cont.label]
    print('list the objects from container')
    rc = run_daos_cmd(conf, cmd, use_json=True)

    data = rc.json
    assert data['status'] == 0, rc
    assert data['error'] is None, rc
    assert data['response'] is not None, rc

    # Run a command to get the object layout
    print('query the object layout')
    actual_obj_layout = []
    for obj in data['response']:
        cmd = ['object', 'query', pool.label, cont.label, obj]
        rc = run_daos_cmd(conf, cmd, use_json=True)

        query_data = rc.json
        assert query_data['status'] == 0, rc
        assert query_data['error'] is None, rc
        assert query_data['response'] is not None, rc
        actual_obj_layout.append(query_data['response']['class'])

    # Verify the object has the correct layout used during kv dictionary creation.
    expected_obj_layout = ['S2', 'S4']
    for obj in expected_obj_layout:
        if obj not in actual_obj_layout:
            failed = True
            print(f'Expected obj {obj} not found in all {actual_obj_layout}')

    if failed:
        conf.wf.add_test_case('pydaos kv object test', failure='test failed')
    else:
        conf.wf.add_test_case('pydaos kv object test')

    # pylint: disable=protected-access
    del kv1
    del kv2
    del container
    daos._cleanup()
    log_test(conf, log_name)
