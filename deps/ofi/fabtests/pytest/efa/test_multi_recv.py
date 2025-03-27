import pytest

@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
@pytest.mark.parametrize("message_size", ["1024", "8192"])
def test_multi_recv(cmdline_args, iteration_type, message_size):
    from common import ClientServerTest
    test = ClientServerTest(cmdline_args,
            "fi_multi_recv -e rdm",
            iteration_type,
            message_size=message_size)
    test.run()
