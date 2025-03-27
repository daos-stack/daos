import pytest
from efa.efa_common import efa_run_client_server_test


SHM_DEFAULT_MAX_INJECT_SIZE = 4096
SHM_DEFAULT_RX_SIZE = 1024


@pytest.mark.functional
@pytest.mark.parametrize("msg_size", [1, 512, 9000, 1048576]) # cover various switch points of shm/efa protocols
@pytest.mark.parametrize("msg_count", [1, 1024, 2048]) # below and above shm's default rx size
def test_unexpected_msg(cmdline_args, msg_size, msg_count, memory_type, completion_semantic):
    from common import ClientServerTest
    if cmdline_args.server_id == cmdline_args.client_id:
        if (msg_size > SHM_DEFAULT_MAX_INJECT_SIZE or memory_type != "host_to_host" or completion_semantic == "delivery_complete") and msg_count > SHM_DEFAULT_RX_SIZE:
            pytest.skip("SHM's CMA/IPC protocol currently cannot handle > rx size number of unexpected messages")
    # This fabtests will allocate msg_size * 2 * msg_count memory for send/recv
    allocated_memory = msg_size * 2 * msg_count
    # The limit size (4 GB) of neuron_tensor_alloc
    neuron_maximal_buffer_size = 2**32
    if "neuron" in memory_type and allocated_memory >= neuron_maximal_buffer_size:
        pytest.skip("Cannot hit neuron allocation limit")
    efa_run_client_server_test(cmdline_args, f"fi_unexpected_msg -e rdm -M {msg_count}", iteration_type="short",
                               completion_semantic=completion_semantic, memory_type=memory_type,
                               message_size=msg_size, completion_type="queue", timeout=1800)
