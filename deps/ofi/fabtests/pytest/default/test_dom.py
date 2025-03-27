import pytest

@pytest.mark.unit
def test_dom(cmdline_args):
    from common import UnitTest
    test = UnitTest(cmdline_args, "fi_dom_test -n 2")
    test.run()
