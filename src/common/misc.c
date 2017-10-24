/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define DDSUBSYS	DDFAC(common)

#include <daos/common.h>

/**
 * Initialise a scatter/gather list, create an array to store @nr iovecs.
 */
int
daos_sgl_init(daos_sg_list_t *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->sg_nr.num = nr;
	D__ALLOC(sgl->sg_iovs, nr * sizeof(*sgl->sg_iovs));

	return sgl->sg_iovs == NULL ? -DER_NOMEM : 0;
}

/**
 * Finalise a scatter/gather list, it can also free iovecs if @free_iovs
 * is true.
 */
void
daos_sgl_fini(daos_sg_list_t *sgl, bool free_iovs)
{
	int	i;

	if (sgl->sg_iovs == NULL)
		return;

	for (i = 0; free_iovs && i < sgl->sg_nr.num; i++) {
		if (sgl->sg_iovs[i].iov_buf != NULL) {
			D__FREE(sgl->sg_iovs[i].iov_buf,
			       sgl->sg_iovs[i].iov_buf_len);
		}
	}

	D__FREE(sgl->sg_iovs, sgl->sg_nr.num * sizeof(*sgl->sg_iovs));
	memset(sgl, 0, sizeof(*sgl));
}

daos_size_t
daos_sgl_data_len(daos_sg_list_t *sgl)
{
	daos_size_t	len;
	int		i;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (i = 0, len = 0; i < sgl->sg_nr.num; i++)
		len += sgl->sg_iovs[i].iov_len;

	return len;
}

daos_size_t
daos_sgl_buf_len(daos_sg_list_t *sgl)
{
	daos_size_t	len;
	int		i;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (i = 0, len = 0; i < sgl->sg_nr.num; i++)
		len += sgl->sg_iovs[i].iov_buf_len;

	return len;
}

daos_size_t
daos_iod_len(daos_iod_t *iod)
{
	uint64_t	len;
	int		i;

	if (iod->iod_size == DAOS_REC_ANY)
		return -1; /* unknown size */

	len = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		len += iod->iod_size;
	} else {
		if (iod->iod_recxs == NULL)
			return 0;

		for (i = 0, len = 0; i < iod->iod_nr; i++)
			len += iod->iod_size * iod->iod_recxs[i].rx_nr;
	}

	return len;
}

/**
 * Trim white space inplace for a string, it returns NULL if the string
 * only has white spaces.
 */
char *
daos_str_trimwhite(char *str)
{
	char	*end = str + strlen(str);

	while (isspace(*str))
		str++;

	if (str == end)
		return NULL;

	while (isspace(end[-1]))
		end--;

	*end = 0;
	return str;
}

int
daos_iov_copy(daos_iov_t *dst, daos_iov_t *src)
{
	D__ALLOC(dst->iov_buf, src->iov_buf_len);
	if (dst->iov_buf == NULL)
		return -DER_NOMEM;
	dst->iov_buf_len = src->iov_buf_len;
	memcpy(dst->iov_buf, src->iov_buf, src->iov_len);
	dst->iov_len = src->iov_len;
	return 0;
}

void
daos_iov_free(daos_iov_t *iov)
{
	if (iov->iov_buf == NULL)
		return;
	D__ASSERT(iov->iov_buf_len > 0);

	D__FREE(iov->iov_buf, iov->iov_buf_len);
	iov->iov_buf = NULL;
	iov->iov_buf_len = 0;
	iov->iov_len = 0;
}

d_rank_list_t *
daos_rank_list_parse(const char *str, const char *sep)
{
	d_rank_t	       *buf;
	int			cap = 8;
	d_rank_list_t       *ranks;
	char		       *s;
	char		       *p;
	int			n = 0;

	D__ALLOC(buf, sizeof(*buf) * cap);
	if (buf == NULL)
		D__GOTO(out, ranks = NULL);
	s = strdup(str);
	if (s == NULL)
		D__GOTO(out_buf, ranks = NULL);

	while ((s = strtok_r(s, sep, &p)) != NULL) {
		if (n == cap) {
			d_rank_t    *buf_new;
			int		cap_new;

			/* Double the buffer. */
			cap_new = cap * 2;
			D__ALLOC(buf_new, sizeof(*buf_new) * cap_new);
			if (buf_new == NULL)
				D__GOTO(out_s, ranks = NULL);
			memcpy(buf_new, buf, sizeof(*buf_new) * n);
			D__FREE(buf, sizeof(*buf) * cap);
			buf = buf_new;
			cap = cap_new;
		}
		buf[n] = atoi(s);
		n++;
		s = NULL;
	}

	ranks = daos_rank_list_alloc(n);
	if (ranks == NULL)
		D__GOTO(out_s, ranks = NULL);
	memcpy(ranks->rl_ranks, buf, sizeof(*buf) * n);

out_s:
	free(s);
out_buf:
	D__FREE(buf, sizeof(*buf) * cap);
out:
	return ranks;
}
