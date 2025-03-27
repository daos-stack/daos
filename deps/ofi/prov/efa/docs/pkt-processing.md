## EFA Libfabric Send/Receive/Completion Paths

### Overview

The EFA provider supports two different endpoint types, `FI_EP_RDM` and
`FI_EP_DGRAM`. This document covers `FI_EP_RDM` as it implements a wire
protocol and software support for some of the Libfabric API such as tag
matching, send after send ordering guarantees, segmentation and reassembly for
large messages, emulation for RMA and atomics, and more.

There are a couple key data structures that are used to implement these
software-level features. The wire protocol that we implement is covered in a
separate document.

### Relevant data structures and functions

`efa_rdm_ep` contains device information and structures for the endpoint including
the device/shm endpoints and completion queues and their state, the packet
pools for recv/send, outstanding app receives to be matched, outstanding sends
in progress, sends and receives queued due to resource exhaustion, unexpected
messages, and structures to track out of order packets and remote peer
capabilities and status.

`efa_rdm_ope` contains information and structures used in send/receive operations. 
It is used in send operation for send posted directly by the app or indirectly 
by emulated read/write operations. When the send is completed a send completion 
will be written and the txe (TX entry) will be released.
It is used in  receive operation for a receive posted by the app. This structure 
is used for tag matching, to queue unexpected messages to be matched later, and to 
keep track of whether long message receives are completed. Just like the txe,
when a receive operation is completed a receive completion is written to the app 
and the `rxe` (RX entry) is released.

`efa_rdm_cq_progress` is the progress handler we register when the completion queue
is created and is called via the util completion queue functions. While the EFA
device will progress sends and receives posted to it, the Libfabric provider
has to process those device completions, potentially copy data out of a bounce
buffer into the application buffer, and write the application completions. This
all happens in this function. The progress handler also progresses long
messages and queued messages.

### Dealing with device resource exhaustion

The EFA device has fixed send and receive queue sizes which the Libfabric
provider has to manage. In general, we try to write an error to the app when
resources are exhausted as the app can manage resource exhaustion better than
the provider. However, there are some cases where we have to queue packets or
store state about a send or receive to be acted on later.

The first case is control messages that have to be queued, for example, we may
send parts of a message and then hit the device limit when sending a segmented,
medium message, or fail to send a control packet containing information that
can't be reconstructed in the future. `efa_rdm_ope_post_send_or_queue` handles
those cases.

We also may queue an rxe/te if we're unable to continue sending segments
or if we fail to post a control message for that entry. You'll find the lists
where those are queued and progressed in `efa_domain_progress_rdm_peers_and_queues`.

### Dealing with receiver not ready errors (RNR)

Finally, the EFA device may write an error completion for RNR, meaning there is
no receive buffer available for the device to place the payload. This can
happen when the application is not posting receive buffers fast enough, but for
the `FI_EP_RDM` receive buffers are pre posted as packets are processed. When
we get RNR in that case, this means that a peer is overloaded.  This can happen
for any control or data packet we post, so to handle this we queue these
packets to be sent later after we backoff for the remote peer.

The occasional RNR is expected so we configure the device to retransmit a
handful of times without writing an error to the host. This is to avoid the
latency penalty of the device writing an error completion, the provider
processing that completion, and trying the send again. However, once the
Libfabric provider receives an RNR for the same packet that we already tried to
retransmit we start random exponential backoff for that peer. We stop sending
to that peer until the peer exits backoff, meaning we either received a
successful send completion for that peer or the backoff timer expires.

See `efa_rdm_ep_queue_rnr_pkt` for where the packets are queued and backoff timers are
set, and see `efa_domain_progress_rdm_peers_and_queues` for where those timers are
checked and we allow sends to that remote peer again.
