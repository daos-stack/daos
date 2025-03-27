import pytest
import copy
from efa.efa_common import efa_retrieve_hw_counter_value

# this test must be run in serial mode because it check hw counter
@pytest.mark.serial
@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_dgram_pingpong(cmdline_args, iteration_type):
    from common import ClientServerTest

    # dgram is unreliable, therefore it is expected that receiver does not always get all the messages
    # when that happened, the test will return -FI_ENODATA
    # Disable the strict mode to mark such return code as pass
    cmdline_args_copy = copy.copy(cmdline_args)
    cmdline_args_copy.strict_fabtests_mode = False

    server_tx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes")
    client_tx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes")
    server_rx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "rx_bytes")
    client_rx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "rx_bytes")

    # efa's dgram endpoint requires prefix therefore must always test with prefix mode on
    test = ClientServerTest(cmdline_args_copy, "fi_dgram_pingpong", iteration_type,
                            prefix_type="with_prefix")
    test.run()

    server_tx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes")
    client_tx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes")
    server_rx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "rx_bytes")
    client_rx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "rx_bytes")

    # verify there is EFA traffic
    assert server_tx_bytes_before_test < server_tx_bytes_after_test
    assert client_tx_bytes_before_test < client_tx_bytes_after_test
    assert server_rx_bytes_before_test < server_rx_bytes_after_test
    assert client_rx_bytes_before_test < client_rx_bytes_after_test
