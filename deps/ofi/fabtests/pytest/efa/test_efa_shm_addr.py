from common import MultinodeTest
import pytest


@pytest.mark.multinode
def test_efa_shm_addr(cmdline_args):
    server_id = cmdline_args.server_id
    client_id = cmdline_args.client_id
    if client_id == server_id:
        pytest.skip("This test requires two nodes")
    # First start a client on remote host, then start
    # a client on local host, so the shm fi_addr
    # inserted for the 2nd client could be different
    # from its efa fi_addr.
    client_hostname_list = [client_id, server_id]
    client_base_command = "fi_rdm"
    server_base_command = client_base_command + " -C {}".format(len(client_hostname_list))
    test = MultinodeTest(cmdline_args, server_base_command, client_base_command,
                         client_hostname_list, run_client_asynchronously=False)
    test.run()
