import pytest

@pytest.mark.unit
def test_dgram_g00n13s(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_dgram g00n13s", is_negative=True)
    test.run()

@pytest.mark.functional
def test_dgram(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_dgram")
    test.run()

@pytest.mark.functional
def test_dgram_waitset(cmdline_args):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_dgram_waitset")
    test.run()

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_dgram_pingpong(cmdline_args, iteration_type, prefix_type):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args, "fi_dgram_pingpong", iteration_type,
                            prefix_type=prefix_type)
    test.run()


