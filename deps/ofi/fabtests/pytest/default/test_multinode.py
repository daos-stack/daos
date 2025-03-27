import pytest
from common import MultinodeTest


@pytest.mark.multinode
@pytest.mark.parametrize("x", ["msg", "rma"])
def test_multinode(cmdline_args, x):

    numproc = 3
    client_hostname_list = [cmdline_args.client_id, ] * (numproc - 1)
    client_base_command = "fi_multinode -x " + x + f" -n {numproc}"
    server_base_command = client_base_command
    test = MultinodeTest(cmdline_args, server_base_command, client_base_command,
                         client_hostname_list, run_client_asynchronously=True)
    test.run()

@pytest.mark.multinode
def test_multinode_coll(cmdline_args):

    numproc = 3
    client_hostname_list = [cmdline_args.client_id, ] * (numproc - 1)
    client_base_command = "fi_multinode_coll"
    server_base_command = client_base_command
    test = MultinodeTest(cmdline_args, server_base_command, client_base_command,
                         client_hostname_list, run_client_asynchronously=True)
    test.run()
