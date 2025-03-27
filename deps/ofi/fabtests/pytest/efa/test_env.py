import pytest
import subprocess
import ctypes


def is_ibv_fork_support_needed():
    '''
    determine whether ibv(rdma-core)'s fork support is needed
    '''
    IBV_FORK_UNNEEDED = 2    # this value was taken from /usr/include/infiniband/verbs.h
    libibverbs = ctypes.cdll.LoadLibrary("libibverbs.so")
    # ibv (rdma-core)'s fork support is needed when
    # kernel space fork support is not available.
    # Newer version of rdma-core provided an API
    #      ibv_is_fork_initialized()
    # to query whether rdma-core's fork support is needed.
    # Older verison of rdma-core does not support this
    # API, so we always consider the fork support to
    # be needed.
    if hasattr(libibverbs, "ibv_is_fork_initialized"):
        return libibverbs.ibv_is_fork_initialized() != IBV_FORK_UNNEEDED
    else:
        return True


@pytest.mark.unit
def test_fork_huge_page_both_set(cmdline_args):
    """
    verify that when FI_EFA_FORK_SAFE and FI_EFA_USE_HUGE_PAGE was both set,
    application will abort
    """
    command = cmdline_args.populate_command("fi_mr_test", "host", additional_environment="FI_EFA_FORK_SAFE=1 FI_EFA_USE_HUGE_PAGE=1")
    process = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if is_ibv_fork_support_needed():
        assert process.returncode != 0
        err_msg = process.stderr.decode("utf-8")
        assert "The usage of huge page is incompatible with rdma-core's fork support" in err_msg
        assert "Your application will now abort" in err_msg
    else:
        assert process.returncode == 0