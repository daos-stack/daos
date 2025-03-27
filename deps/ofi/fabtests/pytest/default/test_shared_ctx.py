import pytest

@pytest.mark.parametrize("shared_ctx_type", ["default", "no_tx", "no_rx"])
@pytest.mark.parametrize("endpoint_type", ["default", "msg", "rdm"])
def test_shared_ctx(cmdline_args, endpoint_type, shared_ctx_type):
    from common import ClientServerTest
    command = "fi_shared_ctx"

    if endpoint_type != "default":
        command += " -e " + endpoint_type

    if shared_ctx_type == "no_tx":
        command += " --no-tx-shared-ctx"
    elif shared_ctx_type == "no_rx":
        command += " --no-rx-shared-ctx"

    test = ClientServerTest(cmdline_args, command)
    test.run()
