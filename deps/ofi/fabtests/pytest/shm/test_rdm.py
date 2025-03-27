import pytest
from default.test_rdm import test_rdm, \
    test_rdm_bw_functional, test_rdm_atomic
from common import perf_progress_model_cli
from shm.shm_common import shm_run_client_server_test

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_pingpong(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    command = "fi_rdm_pingpong" + " " + perf_progress_model_cli
    shm_run_client_server_test(cmdline_args, command, iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_pingpong(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    command = "fi_rdm_tagged_pingpong" + " " + perf_progress_model_cli
    shm_run_client_server_test(cmdline_args, command, iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_bw(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    command = "fi_rdm_tagged_bw" + " " + perf_progress_model_cli
    shm_run_client_server_test(cmdline_args, command, iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)

@pytest.mark.functional
def test_rdm_tagged_peek(cmdline_args):
    from copy import copy

    from common import ClientServerTest

    test = ClientServerTest(cmdline_args, "fi_rdm_tagged_peek", timeout=1800)
    test.run()