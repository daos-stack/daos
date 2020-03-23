/* Copyright (c) 2016 UChicago Argonne, LLC
 * Copyright (C) 2018-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @file
 *
 * SWIM:
 * Scalable Weakly-consistent Infection-style Process Group Membership Protocol.
 */
#ifndef __SWIM_H__
#define __SWIM_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWIM_ID_INVALID ((uint64_t)-1)

typedef uint64_t swim_id_t;

enum swim_member_status {
	SWIM_MEMBER_ALIVE = 0,
	SWIM_MEMBER_SUSPECT,
	SWIM_MEMBER_DEAD,
	SWIM_MEMBER_INACTIVE
};

/** This chars should represent values of enum swim_member_status to print */
#define SWIM_STATUS_CHARS "ASDI"

/** SWIM state associated with each group member */
struct swim_member_state {
	uint64_t		 sms_incarnation; /**< incarnation number */
	enum swim_member_status	 sms_status;	  /**< status of member */
	uint32_t		 sms_padding;
};

struct swim_member_update {
	uint64_t		 smu_id;
	struct swim_member_state smu_state;
};

/** opaque SWIM context type */
struct swim_context;

/** @defgroup SWIM SWIM API */

/** @addtogroup SWIM
 * @{
 */
/** SWIM callbacks for integrating with an overlying group management layer */
struct swim_ops {
	/**
	 * Send a SWIM message to other group member.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  to     IDs of selected target for message
	 * @param[in]  upds   SWIM updates to other group member
	 * @param[in]  nupds  the count of SWIM updates
	 * @returns           0 on success, negative error ID otherwise
	 */
	int (*send_message)(struct swim_context *ctx, swim_id_t to,
			    struct swim_member_update *upds, size_t nupds);

	/**
	 * Retrieve a (non-dead) random group member from the group
	 * management layer to send a direct ping request to.
	 *
	 * NOTE: to ensure time-bounded detection of faulty members,
	 * round-robin selection of members is required.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @returns           ID of selected direct ping target,
	 *                    SWIM_ID_INVALID if no available target
	 */
	swim_id_t (*get_dping_target)(struct swim_context *ctx);

	/**
	 * Retrieve a set of (non-dead) random group members from the group
	 * management layer to send indirect ping requests to.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @returns           ID of selected indirect ping target,
	 *                    SWIM_ID_INVALID if no available target
	 */
	swim_id_t (*get_iping_target)(struct swim_context *ctx);

	/**
	 * Get the SWIM protocol state corresponding to a given member ID.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  id     member ID to query
	 * @param[out] state  pointer to given member's SWIM state
	 * @returns           0 on success, negative error ID otherwise
	 */
	int (*get_member_state)(struct swim_context *ctx,
				swim_id_t id, struct swim_member_state *state);

	/**
	 * Set the SWIM protocol state corresponding to a given member ID.
	 *
	 * @param[in]  ctx    SWIM context pointer from swim_init()
	 * @param[in]  id     member ID to set
	 * @param[in]  state  pointer to given member's SWIM state
	 * @returns           0 on success, negative error ID otherwise
	 */
	int (*set_member_state)(struct swim_context *ctx,
				swim_id_t id, struct swim_member_state *state);
};

/**
 * Initialize the SWIM protocol.
 *
 * @param[in]  self_id   Self member ID
 * @param[in]  swim_ops  SWIM callbacks to group management layer
 * @param[in]  data      Private data which associated with group members
 * @returns              SWIM context pointer on success, NULL otherwise
 */
struct swim_context *swim_init(swim_id_t self_id, struct swim_ops *swim_ops,
			       void *data);

/**
 * Finalize the SWIM protocol.
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 */
void swim_fini(struct swim_context *ctx);

/**
 * Get private data which associated with group members.
 * Originally it's passed to swim_init().
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 * @returns         Private data which associated with group members,
 *                  NULL if not set
 */
void *swim_data(struct swim_context *ctx);

/**
 * Get self member ID.
 *
 * @param[in]  ctx  SWIM context pointer from swim_init()
 * @returns         self ID,
 *                  SWIM_ID_INVALID if not set
 */
swim_id_t swim_self_get(struct swim_context *ctx);

/**
 * Set self member ID.
 *
 * @param[in]  ctx     SWIM context pointer from swim_init()
 * @param[in]  self_id Self member ID
 */
void swim_self_set(struct swim_context *ctx, swim_id_t self_id);

/**
 * Parse a SWIM message from other group member.
 *
 * @param[in]  ctx   SWIM context pointer from swim_init()
 * @param[in]  from  IDs of selected target for message
 * @param[in]  upds  SWIM updates from other group member
 * @param[in]  nupds the count of SWIM updates
 * @returns         0 on success, negative error ID otherwise
 */
int swim_parse_message(struct swim_context *ctx, swim_id_t from,
			struct swim_member_update *upds, size_t nupds);

/**
 * Progress the state machine of SWIM protocol.
 *
 * @param[in]  ctx     SWIM context pointer from swim_init()
 * @param[in]  timeout The amount of time in milliseconds available for
 *                     processing. If timeout <= 0 then returns immediately or
 *                     after the state change (progress minimal required
 *                     changes).
 * @returns            0 on success, negative error ID otherwise
 */
int swim_progress(struct swim_context *ctx, int64_t timeout);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __SWIM_H__ */
