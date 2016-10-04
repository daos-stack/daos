#include <stdlib.h>

/**
 * Start simulating RAS events. Each service process creates a named pipe
 * /tmp/fake_event_pipe_{zero padded rank number}. For example, rank 3 will
 * create the file /tmp/fake_event_pipe_03. To send a event notification to rank
 * 3 with the content "rank 2 has failed", one would run
 *	echo "0 2" > /tmp/fake_event_pipe_03
 * where 0 is the event code, 0 means process failure; 2 is the rank number
 * related to the event.
 *
 * \param rank [IN]		Rank in the primary group
 *
 * \return			0 on success, negative value on error
 */
int
crt_fake_event_init(int rank);

/**
 * Stop simulating RAS events.
 *
 * \param rank [IN]		Rank in the primary group
 *
 * \return			0 on success, negative value on error
 */
int
crt_fake_event_fini(int rank);
