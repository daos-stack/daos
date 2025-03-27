import pytest
from shm.shm_common import shm_run_client_server_test

SHM_DEFAULT_MAX_INJECT_SIZE = 4096
SHM_DEFAULT_RX_SIZE = 1024


@pytest.mark.functional
@pytest.mark.parametrize("msg_size", [1, 512, 9000]) # cover various switch points of shm protocols
@pytest.mark.parametrize("msg_count", [1, 1024, 2048]) # below and above shm's default rx size
def test_unexpected_msg(cmdline_args, msg_size, msg_count, memory_type, completion_semantic):
    from common import ClientServerTest
    if (msg_size > SHM_DEFAULT_MAX_INJECT_SIZE or memory_type != "host_to_host" or completion_semantic == "delivery_complete") and msg_count > SHM_DEFAULT_RX_SIZE:
        pytest.skip("SHM's CMA/IPC protocol currently cannot handle > rx size number of unexpected messages")
    shm_run_client_server_test(cmdline_args, f"fi_unexpected_msg -e rdm -M {msg_count}", iteration_type="short",
                               completion_semantic=completion_semantic, memory_type=memory_type,
                               message_size=msg_size, completion_type="queue", timeout=1800)