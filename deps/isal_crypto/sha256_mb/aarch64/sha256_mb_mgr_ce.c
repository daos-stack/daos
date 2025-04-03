/**********************************************************************
  Copyright(c) 2019 Arm Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Arm Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/
#include <stddef.h>
#include <sha256_mb.h>
#include <assert.h>

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#define SHA256_MB_CE_MAX_LANES	3

#if SHA256_MB_CE_MAX_LANES >=4
void sha256_mb_ce_x4(SHA256_JOB *, SHA256_JOB *, SHA256_JOB *, SHA256_JOB *, int);
#endif
#if SHA256_MB_CE_MAX_LANES >=3
void sha256_mb_ce_x3(SHA256_JOB *, SHA256_JOB *, SHA256_JOB *, int);
#endif
#if SHA256_MB_CE_MAX_LANES >=2
void sha256_mb_ce_x2(SHA256_JOB *, SHA256_JOB *, int);
#endif
void sha256_mb_ce_x1(SHA256_JOB *, int);

#define LANE_IS_NOT_FINISHED(state,i)  	\
	(((state->lens[i]&(~0xf))!=0) && state->ldata[i].job_in_lane!=NULL)
#define LANE_IS_FINISHED(state,i)  	\
	(((state->lens[i]&(~0xf))==0) && state->ldata[i].job_in_lane!=NULL)
#define	LANE_IS_FREE(state,i)		\
	(((state->lens[i]&(~0xf))==0) && state->ldata[i].job_in_lane==NULL)
#define LANE_IS_INVALID(state,i)	\
	(((state->lens[i]&(~0xf))!=0) && state->ldata[i].job_in_lane==NULL)
void sha256_mb_mgr_init_ce(SHA256_MB_JOB_MGR * state)
{
	int i;

	state->unused_lanes = 0xf;
	state->num_lanes_inuse = 0;
	for (i = SHA256_MB_CE_MAX_LANES - 1; i >= 0; i--) {
		state->unused_lanes <<= 4;
		state->unused_lanes |= i;
		state->lens[i] = i;
		state->ldata[i].job_in_lane = 0;
	}

	//lanes > SHA1_MB_CE_MAX_LANES is invalid lane
	for (i = SHA256_MB_CE_MAX_LANES; i < SHA256_MAX_LANES; i++) {
		state->lens[i] = 0xf;
		state->ldata[i].job_in_lane = 0;
	}
}

static int sha256_mb_mgr_do_jobs(SHA256_MB_JOB_MGR * state)
{
	int lane_idx, len, i, lanes;

	int lane_idx_array[SHA256_MAX_LANES];

	if (state->num_lanes_inuse == 0) {
		return -1;
	}
#if SHA256_MB_CE_MAX_LANES == 4
	if (state->num_lanes_inuse == 4) {
		len = min(min(state->lens[0], state->lens[1]),
			  min(state->lens[2], state->lens[3]));
		lane_idx = len & 0xf;
		len &= ~0xf;

		sha256_mb_ce_x4(state->ldata[0].job_in_lane,
				state->ldata[1].job_in_lane,
				state->ldata[2].job_in_lane,
				state->ldata[3].job_in_lane, len >> 4);

	} else
#elif SHA256_MB_CE_MAX_LANES == 3
	if (state->num_lanes_inuse == 3) {
		len = min(min(state->lens[0], state->lens[1]), state->lens[2]);
		lane_idx = len & 0xf;
		len &= ~0xf;

		sha256_mb_ce_x3(state->ldata[0].job_in_lane,
				state->ldata[1].job_in_lane,
				state->ldata[2].job_in_lane, len >> 4);

	} else
#elif	SHA256_MB_CE_MAX_LANES == 2
	if (state->num_lanes_inuse == 2) {
		len = min(state->lens[0], state->lens[1]);
		lane_idx = len & 0xf;
		len &= ~0xf;

		sha256_mb_ce_x2(state->ldata[0].job_in_lane,
				state->ldata[1].job_in_lane, len >> 4);

	} else
#endif
	{
		lanes = 0, len = 0;
		for (i = 0; i < SHA256_MAX_LANES && lanes < state->num_lanes_inuse; i++) {
			if (LANE_IS_NOT_FINISHED(state, i)) {
				if (lanes)
					len = min(len, state->lens[i]);
				else
					len = state->lens[i];
				lane_idx_array[lanes] = i;
				lanes++;
			}
		}
		if (lanes == 0)
			return -1;
		lane_idx = len & 0xf;
		len = len & (~0xf);
#if SHA256_MB_CE_MAX_LANES >=4
		if (lanes == 4) {
			sha256_mb_ce_x4(state->ldata[lane_idx_array[0]].job_in_lane,
					state->ldata[lane_idx_array[1]].job_in_lane,
					state->ldata[lane_idx_array[2]].job_in_lane,
					state->ldata[lane_idx_array[3]].job_in_lane, len >> 4);

		} else
#endif
#if SHA256_MB_CE_MAX_LANES >=3
		if (lanes == 3) {
			sha256_mb_ce_x3(state->ldata[lane_idx_array[0]].job_in_lane,
					state->ldata[lane_idx_array[1]].job_in_lane,
					state->ldata[lane_idx_array[2]].job_in_lane, len >> 4);
		} else
#endif
#if SHA256_MB_CE_MAX_LANES >=2
		if (lanes == 2) {
			sha256_mb_ce_x2(state->ldata[lane_idx_array[0]].job_in_lane,
					state->ldata[lane_idx_array[1]].job_in_lane, len >> 4);
		} else
#endif
		{
			sha256_mb_ce_x1(state->ldata[lane_idx_array[0]].job_in_lane, len >> 4);
		}
	}
	//only return the min length job
	for (i = 0; i < SHA256_MAX_LANES; i++) {
		if (LANE_IS_NOT_FINISHED(state, i)) {
			state->lens[i] -= len;
			state->ldata[i].job_in_lane->len -= len;
			state->ldata[i].job_in_lane->buffer += len << 2;
		}
	}

	return lane_idx;

}

static SHA256_JOB *sha256_mb_mgr_free_lane(SHA256_MB_JOB_MGR * state)
{
	int i;
	SHA256_JOB *ret = NULL;

	for (i = 0; i < SHA256_MB_CE_MAX_LANES; i++) {
		if (LANE_IS_FINISHED(state, i)) {

			state->unused_lanes <<= 4;
			state->unused_lanes |= i;
			state->num_lanes_inuse--;
			ret = state->ldata[i].job_in_lane;
			ret->status = STS_COMPLETED;
			state->ldata[i].job_in_lane = NULL;
			break;
		}
	}
	return ret;
}

static void sha256_mb_mgr_insert_job(SHA256_MB_JOB_MGR * state, SHA256_JOB * job)
{
	int lane_idx;
	//add job into lanes
	lane_idx = state->unused_lanes & 0xf;
	//fatal error
	assert(lane_idx < SHA256_MB_CE_MAX_LANES);
	state->lens[lane_idx] = (job->len << 4) | lane_idx;
	state->ldata[lane_idx].job_in_lane = job;
	state->unused_lanes >>= 4;
	state->num_lanes_inuse++;
}

SHA256_JOB *sha256_mb_mgr_submit_ce(SHA256_MB_JOB_MGR * state, SHA256_JOB * job)
{
#ifndef NDEBUG
	int lane_idx;
#endif
	SHA256_JOB *ret;

	//add job into lanes
	sha256_mb_mgr_insert_job(state, job);

	ret = sha256_mb_mgr_free_lane(state);
	if (ret != NULL) {
		return ret;
	}
	//submit will wait all lane has data
	if (state->num_lanes_inuse < SHA256_MB_CE_MAX_LANES)
		return NULL;
#ifndef NDEBUG
	lane_idx = sha256_mb_mgr_do_jobs(state);
	assert(lane_idx != -1);
#else
	sha256_mb_mgr_do_jobs(state);
#endif

	//~ i = lane_idx;
	ret = sha256_mb_mgr_free_lane(state);
	return ret;
}

SHA256_JOB *sha256_mb_mgr_flush_ce(SHA256_MB_JOB_MGR * state)
{
	SHA256_JOB *ret;
	ret = sha256_mb_mgr_free_lane(state);
	if (ret) {
		return ret;
	}

	sha256_mb_mgr_do_jobs(state);
	return sha256_mb_mgr_free_lane(state);

}
