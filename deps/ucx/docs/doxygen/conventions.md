Conventions and Notations
=========================

This section describes the conventions and notations in the UCX specification.

\section Blocking Blocking Behavior
The blocking UCX routines return only when a UCX operation is complete.
After the return, the resources used in the UCX routine are available
for reuse.

\section Non-blocking Non-blocking Behavior
The non-blocking UCX routines return immediately, independent of operation
completion. After the return, the resources used for the routines are not
necessarily available for reuse.

\section Fairness Fairness
UCX routines do not guarantee fairness. However, the routines
enable UCX consumers to write efficient and fair programs.

\section Interaction Interaction with Signal Handler Functions
If UCX routines are invoked from a signal handler function,
the behavior of the program is undefined.
