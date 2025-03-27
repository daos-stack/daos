/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @file
 * @brief IO class API
 *
 * File contains structures and methods for handling IO Class
 * differentiation features
 */

#ifndef __OCF_IO_CLASS_H__
#define __OCF_IO_CLASS_H__

/**
 * @brief OCF IO class information
 */
struct ocf_io_class_info {
	char name[OCF_IO_CLASS_NAME_MAX];
		/*!< The name of the IO class */

	ocf_cache_mode_t cache_mode;
		/*!< Cache mode of the IO class */

	int16_t priority;
		/*!< IO class priority */

	uint32_t curr_size;
		/*!< Current size of the IO class - number of cache lines which
		 * were assigned into this IO class
		 */

	uint32_t min_size;
		/*!< Minimum number of cache lines that were guaranteed
		 * for specified IO class. If current size reach minimum size
		 * that no more eviction takes place
		 */

	uint32_t max_size;
		/*!< Maximum number of cache lines that might be assigned into
		 * this IO class. If current size reaches maximum size then some
		 * of ioclass's cachelines are evicted.
		 */

	ocf_cleaning_t cleaning_policy_type;
		/*!< The type of cleaning policy for given IO class */
};

/**
 * @brief retrieve io class info
 *
 * function meant to retrieve information pertaining to particular IO class,
 * specifically to fill ocf_io_class_info structure based on input parameters.
 *
 * @param[in] cache cache handle, to which specified request pertains.
 * @param[in] io_class id of an io class which shall be retreived.
 * @param[out] info io class info structures to be filled as a
 *             result of this function call.
 *
 * @return function returns 0 upon successful completion; appropriate error
 *         code is returned otherwise
 */
int ocf_cache_io_class_get_info(ocf_cache_t cache, uint32_t io_class,
		struct ocf_io_class_info *info);

/**
 * @brief helper function for ocf_io_class_visit
 *
 * This function is called back from ocf_io_class_visit for each valid
 * configured io class; henceforth all parameters are input parameters,
 * no exceptions. It is usable to enumerate all the io classes.
 *
 * @param[in] cache cache id of cache for which data is being retrieved
 * @param[in] io_class_id id of an io class for which callback herein
 *            is invoked.
 * @param[in] cntx a context pointer passed herein from within
 *            ocf_io_class_visit down to this callback.
 *
 * @return 0 upon success; Nonzero upon failure (when nonzero is returned,
 *         this callback won't be invoked for any more io classes)
 */
typedef int (*ocf_io_class_visitor_t)(ocf_cache_t cache,
		uint32_t io_class_id, void *cntx);

/**
 * @brief enumerate all of the available IO classes.
 *
 * This function allows enumeration and retrieval of all io class id's that
 * are valid for given cache id via visiting all those with callback function
 * that is supplied by caller.
 *
 * @param[in] cache cache id to which given call pertains
 * @param[in] visitor a callback function that will be issued for each and every
 *            IO class that is configured and valid within given cache instance
 * @param[in] cntx a context variable - structure that shall be passed to a
 *            callback function for every call
 *
 * @return 0 upon successful completion of the function; otherwise nonzero result
 *         shall be returned
 */
int ocf_io_class_visit(ocf_cache_t cache, ocf_io_class_visitor_t visitor,
		void *cntx);

#endif /* __OCF_IO_CLASS_H__ */
