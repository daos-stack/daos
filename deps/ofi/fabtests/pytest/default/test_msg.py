import pytest

@pytest.mark.unit
def test_msg_g00n13s(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_msg g00n13s", is_negative=True)
    test.run()

@pytest.mark.functional
def test_msg(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_msg")
    test.run()

@pytest.mark.functional
def test_msg_epoll(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_msg_epoll")
    test.run()

@pytest.mark.functional
def test_msg_sockets(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_msg_sockets")
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_msg_pingpong(cmdline_args, iteration_type, prefix_type, datacheck_type):
    from common import ClientServerTest
    command = "fi_msg_pingpong"
    test = ClientServerTest(cmdline_args, "fi_msg_pingpong", iteration_type,
                            prefix_type=prefix_type, datacheck_type=datacheck_type)
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_msg_bw(cmdline_args, iteration_type, datacheck_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_msg_bw", iteration_type,
                            datacheck_type=datacheck_type)
    test.run()
