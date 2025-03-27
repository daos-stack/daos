import copy
import pytest
from efa.efa_common import efa_retrieve_hw_counter_value, get_efa_device_names
from common import ClientServerTest

# This test must be run in serial mode because it checks the hw counter
@pytest.mark.serial
@pytest.mark.functional
def test_efa_device_selection(cmdline_args):

    if cmdline_args.server_id == cmdline_args.client_id:
        pytest.skip("EFA device selection test requires 2 nodes")
        return

    server_device_names = get_efa_device_names(cmdline_args.server_id)
    client_device_names = get_efa_device_names(cmdline_args.client_id)

    server_num_devices = len(server_device_names)
    client_num_devices = len(server_device_names)

    for i in range(max(server_num_devices, client_num_devices)):
        server_device_idx = i % server_num_devices
        client_device_idx = i % client_num_devices

        server_device_name = server_device_names[server_device_idx]
        client_device_name = client_device_names[client_device_idx]

        for suffix in ["rdm", "dgrm"]:
            server_tx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "tx_bytes", server_device_name)
            client_tx_bytes_before_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes", client_device_name)

            prefix_type = "wout_prefix"
            strict_fabtests_mode = True
            if suffix == "rdm":
                command = "fi_rdm_pingpong"
            else:
                prefix_type = "with_prefix"  # efa provider requires prefix mode for dgram provider, hence "-k"
                strict_fabtests_mode = False  # # dgram is unreliable
                command = "fi_dgram_pingpong"

            server_domain_name = server_device_name + "-" + suffix
            client_domain_name = client_device_name + "-" + suffix

            cmdline_args_copy = copy.copy(cmdline_args)
            cmdline_args_copy.additional_server_arguments = "-d " + server_domain_name
            cmdline_args_copy.additional_client_arguments = "-d " + client_domain_name
            cmdline_args_copy.strict_fabtests_mode = strict_fabtests_mode

            test = ClientServerTest(cmdline_args_copy, command, message_size="1000", prefix_type=prefix_type, timeout=300)
            test.run()

            server_tx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.server_id, "tx_bytes", server_device_name)
            client_tx_bytes_after_test = efa_retrieve_hw_counter_value(cmdline_args.client_id, "tx_bytes", client_device_name)

            # Verify EFA traffic
            assert server_tx_bytes_before_test < server_tx_bytes_after_test
            assert client_tx_bytes_before_test < client_tx_bytes_after_test
