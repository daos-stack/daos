/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_OBJ_CLASS_H__
#define __DAOS_OBJ_CLASS_H__

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_OBJ_CLASS_NAME_LEN       24

#define MAX_NUM_GROUPS               ((1 << 16UL) - 1)
#define OC_REDUN_SHIFT               24
#define OBJ_CLASS_DEF(redun, grp_nr) ((redun << OC_REDUN_SHIFT) | grp_nr)

/**
 * Define the object data redundancy method. Encoded over 8 bits in the object ID.
 * The number of redundancy groups is encoded separately in the object metadata.
 */
enum daos_obj_redun {
	OC_UNKNOWN = 0,

	/**
	 * No data protection, aka "single replica".
	 */
	OR_RP_1 = 1,

	/**
	 * Static N-way replicated object (OC_RP_N).
	 * The number of redundancy group is hardcoded in the object metadata.
	 */
	OR_RP_2 = 8,
	OR_RP_3,
	OR_RP_4,
	OR_RP_5,
	OR_RP_6,
	OR_RP_8,
	OR_RP_12,
	OR_RP_16,
	OR_RP_24,
	OR_RP_32,
	OR_RP_48,
	OR_RP_64,
	OR_RP_128,

	/**
	 * N+K reed solomon erasure-coded object (OC_EC_NPK).
	 * - the first number is data cells number within a redundancy group
	 * - the number after P is parity cells number within a redundancy group
	 * C_S1
	 *
	 * Examples:
	 * - 2P1: 2+1 EC object
	 * - 4P2: 4+2 EC object
	 * - 8P2: 8+2 EC object
	 * - 16P2: 16+2 EC object
	 */
	OR_RS_2P1 = 32,
	OR_RS_2P2,
	OR_RS_4P1,
	OR_RS_4P2,
	OR_RS_8P1,
	OR_RS_8P2,
	OR_RS_16P1,
	OR_RS_16P2,

	/*
	 * Predefined object classes that can be used directly by the API user.
	 * It describes schema of data distribution & protection.
	 */
	/**
	 * Object classes with no data protection
	 * NB: The first 50 IDs are reserved for backward compatibility
	 */
	OC_BACK_COMPAT = 50,
	/** Single shard object */
	OC_TINY,
	/**
	 * Object with small number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_SMALL,
	/**
	 * Object with large number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_LARGE,
	/**
	 * Object with maximum number of shards.
	 * Number of shards of the class is chosen by DAOS based on the
	 * current size of the pool.
	 */
	OC_MAX,

	/**
	 * object classes protected by replication
	 */
	/**
	 * Tiny object protected by replication
	 * This object class has one redundancy group
	 */
	OC_RP_TINY = 60,
	/**
	 * Replicated object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SMALL,
	/**
	 * Replicated object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_LARGE,
	/**
	 * Replicated object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_MAX,

	/**
	 * Object classes protected by replication which supports Scalable
	 * Fetch (SF)
	 * SF classes have more replicas, so they are slower on update, but more
	 * scalable on fetch because they have more replicas to serve fetches.
	 */
	/**
	 * Tiny object protected by replication
	 * This object class has one redundancy group
	 */
	OC_RP_SF_TINY = 70,
	/**
	 * (SF) Replicated object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_SMALL,
	/**
	 * (SF) Replicated object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_LARGE,
	/**
	 * (SF) Replicated object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_RP_SF_MAX,

	/**
	 * Replicated object class which is extremely scalable for fetch.
	 * It has many replicas so it is very slow for update.
	 */
	OC_RP_XSF = 80,

	/**
	 * Object classes protected by erasure code
	 */
	/**
	 * Tiny object protected by EC
	 * This object class has one redundancy group
	 */
	OC_EC_TINY = 100,
	/**
	 * EC object with small number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_SMALL,
	/**
	 * EC object with large number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_LARGE,
	/**
	 * EC object with maximum number of redundancy groups.
	 * Number of redundancy groups of the class is chosen by DAOS
	 * based on the current size of the pool.
	 */
	OC_EC_MAX,
	/**
	 * Object classes with explicit layout
	 */
	/**
	 * Object classes with explicit layout but no data protection
	 * Examples:
	 * S1 : shards=1, S2 means shards=2, ...
	 * SX : spreading across all targets within the pool
	 */
	OC_S1  = OBJ_CLASS_DEF(OR_RP_1, 1ULL),
	OC_S2  = OBJ_CLASS_DEF(OR_RP_1, 2ULL),
	OC_S4  = OBJ_CLASS_DEF(OR_RP_1, 4ULL),
	OC_S6  = OBJ_CLASS_DEF(OR_RP_1, 6ULL),
	OC_S8  = OBJ_CLASS_DEF(OR_RP_1, 8ULL),
	OC_S12 = OBJ_CLASS_DEF(OR_RP_1, 12ULL),
	OC_S16 = OBJ_CLASS_DEF(OR_RP_1, 16ULL),
	OC_S32 = OBJ_CLASS_DEF(OR_RP_1, 32ULL),
	OC_SX  = OBJ_CLASS_DEF(OR_RP_1, MAX_NUM_GROUPS),

	/**
	 * Replicated object with explicit layout
	 * The first number is number of replicas, the number after G stands
	 * for number of redundancy Groups.
	 *
	 * Examples:
	 * 2G1 : 2 replicas group=1
	 * 3G2 : 3 replicas groups=2, ...
	 * 8GX : 8 replicas, it spreads across all targets within the pool
	 */
	/** 2-way replicated object classes */
	OC_RP_2G1  = OBJ_CLASS_DEF(OR_RP_2, 1ULL),
	OC_RP_2G2  = OBJ_CLASS_DEF(OR_RP_2, 2ULL),
	OC_RP_2G4  = OBJ_CLASS_DEF(OR_RP_2, 4ULL),
	OC_RP_2G6  = OBJ_CLASS_DEF(OR_RP_2, 6ULL),
	OC_RP_2G8  = OBJ_CLASS_DEF(OR_RP_2, 8ULL),
	OC_RP_2G12 = OBJ_CLASS_DEF(OR_RP_2, 12ULL),
	OC_RP_2G16 = OBJ_CLASS_DEF(OR_RP_2, 16ULL),
	OC_RP_2G32 = OBJ_CLASS_DEF(OR_RP_2, 32ULL),
	OC_RP_2GX  = OBJ_CLASS_DEF(OR_RP_2, MAX_NUM_GROUPS),

	/** 3-way replicated object classes */
	OC_RP_3G1  = OBJ_CLASS_DEF(OR_RP_3, 1ULL),
	OC_RP_3G2  = OBJ_CLASS_DEF(OR_RP_3, 2ULL),
	OC_RP_3G4  = OBJ_CLASS_DEF(OR_RP_3, 4ULL),
	OC_RP_3G6  = OBJ_CLASS_DEF(OR_RP_3, 6ULL),
	OC_RP_3G8  = OBJ_CLASS_DEF(OR_RP_3, 8ULL),
	OC_RP_3G12 = OBJ_CLASS_DEF(OR_RP_3, 12ULL),
	OC_RP_3G16 = OBJ_CLASS_DEF(OR_RP_3, 16ULL),
	OC_RP_3G32 = OBJ_CLASS_DEF(OR_RP_3, 32ULL),
	OC_RP_3GX  = OBJ_CLASS_DEF(OR_RP_3, MAX_NUM_GROUPS),

	/** 4-way replicated object classes */
	OC_RP_4G1  = OBJ_CLASS_DEF(OR_RP_4, 1ULL),
	OC_RP_4G2  = OBJ_CLASS_DEF(OR_RP_4, 2ULL),
	OC_RP_4G4  = OBJ_CLASS_DEF(OR_RP_4, 4ULL),
	OC_RP_4G6  = OBJ_CLASS_DEF(OR_RP_4, 6ULL),
	OC_RP_4G8  = OBJ_CLASS_DEF(OR_RP_4, 8ULL),
	OC_RP_4G12 = OBJ_CLASS_DEF(OR_RP_4, 12ULL),
	OC_RP_4G16 = OBJ_CLASS_DEF(OR_RP_4, 16ULL),
	OC_RP_4G32 = OBJ_CLASS_DEF(OR_RP_4, 32ULL),
	OC_RP_4GX  = OBJ_CLASS_DEF(OR_RP_4, MAX_NUM_GROUPS),

	/** 5-way replicated object classes */
	OC_RP_5G1  = OBJ_CLASS_DEF(OR_RP_5, 1ULL),
	OC_RP_5G2  = OBJ_CLASS_DEF(OR_RP_5, 2ULL),
	OC_RP_5G4  = OBJ_CLASS_DEF(OR_RP_5, 4ULL),
	OC_RP_5G6  = OBJ_CLASS_DEF(OR_RP_5, 6ULL),
	OC_RP_5G8  = OBJ_CLASS_DEF(OR_RP_5, 8ULL),
	OC_RP_5G12 = OBJ_CLASS_DEF(OR_RP_5, 12ULL),
	OC_RP_5G16 = OBJ_CLASS_DEF(OR_RP_5, 16ULL),
	OC_RP_5G32 = OBJ_CLASS_DEF(OR_RP_5, 32ULL),
	OC_RP_5GX  = OBJ_CLASS_DEF(OR_RP_5, MAX_NUM_GROUPS),

	/** 6-way replicated object classes */
	OC_RP_6G1  = OBJ_CLASS_DEF(OR_RP_6, 1ULL),
	OC_RP_6G2  = OBJ_CLASS_DEF(OR_RP_6, 2ULL),
	OC_RP_6G4  = OBJ_CLASS_DEF(OR_RP_6, 4ULL),
	OC_RP_6G6  = OBJ_CLASS_DEF(OR_RP_6, 6ULL),
	OC_RP_6G8  = OBJ_CLASS_DEF(OR_RP_6, 8ULL),
	OC_RP_6G12 = OBJ_CLASS_DEF(OR_RP_6, 12ULL),
	OC_RP_6G16 = OBJ_CLASS_DEF(OR_RP_6, 16ULL),
	OC_RP_6G32 = OBJ_CLASS_DEF(OR_RP_6, 32ULL),
	OC_RP_6GX  = OBJ_CLASS_DEF(OR_RP_6, MAX_NUM_GROUPS),

	/* OC_XSF will map to one of these */
	OC_RP_12G1  = OBJ_CLASS_DEF(OR_RP_12, 1ULL),
	OC_RP_16G1  = OBJ_CLASS_DEF(OR_RP_16, 1ULL),
	OC_RP_24G1  = OBJ_CLASS_DEF(OR_RP_24, 1ULL),
	OC_RP_32G1  = OBJ_CLASS_DEF(OR_RP_32, 1ULL),
	OC_RP_48G1  = OBJ_CLASS_DEF(OR_RP_48, 1ULL),
	OC_RP_64G1  = OBJ_CLASS_DEF(OR_RP_64, 1ULL),
	OC_RP_128G1 = OBJ_CLASS_DEF(OR_RP_128, 1ULL),

	/**
	 * Erasure coded object with explicit layout
	 * - the first number is data cells number within a redundancy group
	 * - the number after P is parity cells number within a redundancy group
	 * - the number after G is number of redundancy Groups.
	 *
	 * Examples:
	 * - 2P1G1: 2+1 EC object with one redundancy group
	 * - 4P2G8: 4+2 EC object with 8 redundancy groups
	 * - 8P2G2: 8+2 EC object with 2 redundancy groups
	 * - 16P2GX: 16+2 EC object spreads across all targets within the pool
	 */
	/** EC 2+1 object classes */
	OC_EC_2P1G1  = OBJ_CLASS_DEF(OR_RS_2P1, 1ULL),
	OC_EC_2P1G2  = OBJ_CLASS_DEF(OR_RS_2P1, 2ULL),
	OC_EC_2P1G4  = OBJ_CLASS_DEF(OR_RS_2P1, 4ULL),
	OC_EC_2P1G6  = OBJ_CLASS_DEF(OR_RS_2P1, 6ULL),
	OC_EC_2P1G8  = OBJ_CLASS_DEF(OR_RS_2P1, 8ULL),
	OC_EC_2P1G12 = OBJ_CLASS_DEF(OR_RS_2P1, 12ULL),
	OC_EC_2P1G16 = OBJ_CLASS_DEF(OR_RS_2P1, 16ULL),
	OC_EC_2P1G32 = OBJ_CLASS_DEF(OR_RS_2P1, 32ULL),
	OC_EC_2P1GX  = OBJ_CLASS_DEF(OR_RS_2P1, MAX_NUM_GROUPS),

	/** EC 2+2 object classes */
	OC_EC_2P2G1  = OBJ_CLASS_DEF(OR_RS_2P2, 1ULL),
	OC_EC_2P2G2  = OBJ_CLASS_DEF(OR_RS_2P2, 2ULL),
	OC_EC_2P2G4  = OBJ_CLASS_DEF(OR_RS_2P2, 4ULL),
	OC_EC_2P2G6  = OBJ_CLASS_DEF(OR_RS_2P2, 6ULL),
	OC_EC_2P2G8  = OBJ_CLASS_DEF(OR_RS_2P2, 8ULL),
	OC_EC_2P2G12 = OBJ_CLASS_DEF(OR_RS_2P2, 12ULL),
	OC_EC_2P2G16 = OBJ_CLASS_DEF(OR_RS_2P2, 16ULL),
	OC_EC_2P2G32 = OBJ_CLASS_DEF(OR_RS_2P2, 32ULL),
	OC_EC_2P2GX  = OBJ_CLASS_DEF(OR_RS_2P2, MAX_NUM_GROUPS),

	/** EC 4+1 object classes */
	OC_EC_4P1G1  = OBJ_CLASS_DEF(OR_RS_4P1, 1ULL),
	OC_EC_4P1G2  = OBJ_CLASS_DEF(OR_RS_4P1, 2ULL),
	OC_EC_4P1G4  = OBJ_CLASS_DEF(OR_RS_4P1, 4ULL),
	OC_EC_4P1G6  = OBJ_CLASS_DEF(OR_RS_4P1, 6ULL),
	OC_EC_4P1G8  = OBJ_CLASS_DEF(OR_RS_4P1, 8ULL),
	OC_EC_4P1G12 = OBJ_CLASS_DEF(OR_RS_4P1, 12ULL),
	OC_EC_4P1G16 = OBJ_CLASS_DEF(OR_RS_4P1, 16ULL),
	OC_EC_4P1G32 = OBJ_CLASS_DEF(OR_RS_4P1, 32ULL),
	OC_EC_4P1GX  = OBJ_CLASS_DEF(OR_RS_4P1, MAX_NUM_GROUPS),

	/** EC 4+2 object classes */
	OC_EC_4P2G1  = OBJ_CLASS_DEF(OR_RS_4P2, 1ULL),
	OC_EC_4P2G2  = OBJ_CLASS_DEF(OR_RS_4P2, 2ULL),
	OC_EC_4P2G4  = OBJ_CLASS_DEF(OR_RS_4P2, 4ULL),
	OC_EC_4P2G6  = OBJ_CLASS_DEF(OR_RS_4P2, 6ULL),
	OC_EC_4P2G8  = OBJ_CLASS_DEF(OR_RS_4P2, 8ULL),
	OC_EC_4P2G12 = OBJ_CLASS_DEF(OR_RS_4P2, 12ULL),
	OC_EC_4P2G16 = OBJ_CLASS_DEF(OR_RS_4P2, 16ULL),
	OC_EC_4P2G32 = OBJ_CLASS_DEF(OR_RS_4P2, 32ULL),
	OC_EC_4P2GX  = OBJ_CLASS_DEF(OR_RS_4P2, MAX_NUM_GROUPS),

	/** EC 8+1 object classes */
	OC_EC_8P1G1  = OBJ_CLASS_DEF(OR_RS_8P1, 1ULL),
	OC_EC_8P1G2  = OBJ_CLASS_DEF(OR_RS_8P1, 2ULL),
	OC_EC_8P1G4  = OBJ_CLASS_DEF(OR_RS_8P1, 4ULL),
	OC_EC_8P1G6  = OBJ_CLASS_DEF(OR_RS_8P1, 6ULL),
	OC_EC_8P1G8  = OBJ_CLASS_DEF(OR_RS_8P1, 8ULL),
	OC_EC_8P1G12 = OBJ_CLASS_DEF(OR_RS_8P1, 12ULL),
	OC_EC_8P1G16 = OBJ_CLASS_DEF(OR_RS_8P1, 16ULL),
	OC_EC_8P1G32 = OBJ_CLASS_DEF(OR_RS_8P1, 32ULL),
	OC_EC_8P1GX  = OBJ_CLASS_DEF(OR_RS_8P1, MAX_NUM_GROUPS),

	/** EC 8+2 object classes */
	OC_EC_8P2G1  = OBJ_CLASS_DEF(OR_RS_8P2, 1ULL),
	OC_EC_8P2G2  = OBJ_CLASS_DEF(OR_RS_8P2, 2ULL),
	OC_EC_8P2G4  = OBJ_CLASS_DEF(OR_RS_8P2, 4ULL),
	OC_EC_8P2G6  = OBJ_CLASS_DEF(OR_RS_8P2, 6ULL),
	OC_EC_8P2G8  = OBJ_CLASS_DEF(OR_RS_8P2, 8ULL),
	OC_EC_8P2G12 = OBJ_CLASS_DEF(OR_RS_8P2, 12ULL),
	OC_EC_8P2G16 = OBJ_CLASS_DEF(OR_RS_8P2, 16ULL),
	OC_EC_8P2G32 = OBJ_CLASS_DEF(OR_RS_8P2, 32ULL),
	OC_EC_8P2GX  = OBJ_CLASS_DEF(OR_RS_8P2, MAX_NUM_GROUPS),

	/** EC 16+1 object classes */
	OC_EC_16P1G1  = OBJ_CLASS_DEF(OR_RS_16P1, 1ULL),
	OC_EC_16P1G2  = OBJ_CLASS_DEF(OR_RS_16P1, 2ULL),
	OC_EC_16P1G4  = OBJ_CLASS_DEF(OR_RS_16P1, 4ULL),
	OC_EC_16P1G6  = OBJ_CLASS_DEF(OR_RS_16P1, 6ULL),
	OC_EC_16P1G8  = OBJ_CLASS_DEF(OR_RS_16P1, 8ULL),
	OC_EC_16P1G12 = OBJ_CLASS_DEF(OR_RS_16P1, 12ULL),
	OC_EC_16P1G16 = OBJ_CLASS_DEF(OR_RS_16P1, 16ULL),
	OC_EC_16P1G32 = OBJ_CLASS_DEF(OR_RS_16P1, 32ULL),
	OC_EC_16P1GX  = OBJ_CLASS_DEF(OR_RS_16P1, MAX_NUM_GROUPS),

	/** EC 16+2 object classes */
	OC_EC_16P2G1  = OBJ_CLASS_DEF(OR_RS_16P2, 1ULL),
	OC_EC_16P2G2  = OBJ_CLASS_DEF(OR_RS_16P2, 2ULL),
	OC_EC_16P2G4  = OBJ_CLASS_DEF(OR_RS_16P2, 4ULL),
	OC_EC_16P2G6  = OBJ_CLASS_DEF(OR_RS_16P2, 6ULL),
	OC_EC_16P2G8  = OBJ_CLASS_DEF(OR_RS_16P2, 8ULL),
	OC_EC_16P2G12 = OBJ_CLASS_DEF(OR_RS_16P2, 12ULL),
	OC_EC_16P2G16 = OBJ_CLASS_DEF(OR_RS_16P2, 16ULL),
	OC_EC_16P2G32 = OBJ_CLASS_DEF(OR_RS_16P2, 32ULL),
	OC_EC_16P2GX  = OBJ_CLASS_DEF(OR_RS_16P2, MAX_NUM_GROUPS),

	/** Class ID equal or higher than this is reserved */
	OC_RESERVED = 1 << 30,

};

enum daos_obj_schema {
	DAOS_OS_SINGLE,		/**< Single stripe object */
	DAOS_OS_STRIPED,	/**< Fix striped object */
	DAOS_OS_DYN_STRIPED,	/**< Dynamically striped object */
	DAOS_OS_DYN_CHUNKED,	/**< Dynamically chunked object */
};

enum daos_obj_resil {
	DAOS_RES_EC,            /**< Erasure code */
	DAOS_RES_REPL,          /**< Replication */
};

/** Object class attributes */
struct daos_oclass_attr {
	/** reserved: object placement schema, used by placement algorithm */
	enum daos_obj_schema		 ca_schema;
	/** Resilience method, replication or erasure code */
	enum daos_obj_resil		 ca_resil;
	/** reserved */
	unsigned int			 ca_resil_degree;
	/** Initial # redundancy group, unnecessary for some schemas */
	unsigned int			 ca_grp_nr;
	/** replication or erasure coding attributes based on #ca_resil */
	union {
		/** Replication attributes */
		struct daos_rp_attr {
			/** Protocol of replicating, reserved */
			unsigned int	 r_proto;
			/** Number of replicas */
			unsigned int	 r_num;
		} rp;

		/** Erasure coding attributes */
		struct daos_ec_attr {
			/** number of data cells (k) */
			unsigned short	 e_k;
			/** number of parity cells (p) */
			unsigned short	 e_p;
			/** length of each block of data (cell) */
			unsigned int	 e_len;
		} ec;
	} u;
	/** TODO: add more attributes */
};

/** object class ID */
typedef uint32_t		daos_oclass_id_t;
/** object class hints */
typedef uint16_t		daos_oclass_hints_t;

/** List of object classes, used for class enumeration */
struct daos_oclass_list {
	/** List length, actual buffer size */
	uint32_t		 cl_nr;
	/** List length, returned buffer size */
	uint32_t		 cl_nr_out;
	/** Actual list of class IDs */
	daos_oclass_id_t	*cl_cids;
	/** Attributes of each listed class, optional */
	struct daos_oclass_attr	*cl_cattrs;
};

/**
 * Return the Object class ID given the object class name in string format.
 *
 * \param[in]	name	Object class name.
 *
 * \return		The Object class ID, 0 / OC_UNKNOWN if unknown.
 */
int
daos_oclass_name2id(const char *name);

/**
 * Return the list of object class.
 *
 * \param[in]	size	length in bytes of str buffer.
 * \param[out]	str	buffer to get all registered oclass names
 *
 * \return		>= 0 on success and required length of str, < 0 if error.
 */
ssize_t
daos_oclass_names_list(size_t size, char *str);

/**
 *
 * Return total number of object classes
 *
 * \param[in]	opts	reserved options
 *
 * \return		> 0 Number object classes
 */
int
daos_oclass_nr(int opts);

/**
 * Return the object class name given it's ID.
 *
 * \param[in]	oc_id	Object class ID.
 * \param[out]	name	buffer for the name of the object class to be copied
 *			into it.
 *
 * \return		0 on success, -1 if invalid class.
 */
int
daos_oclass_id2name(daos_oclass_id_t oc_id, char *name);

/**
 * Register a new object class in addition to the default ones (see DAOS_OC_*).
 * An object class cannot be unregistered for the time being.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	ID for the new object class.
 * \param[in]	attr	Attributes for the new object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			struct daos_oclass_attr *attr, daos_event_t *ev);

/**
 * Query attributes of an object class by its ID.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	Class ID to query.
 * \param[out]	attr	Returned attributes of the object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Nonexistent class ID
 */
int
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     struct daos_oclass_attr *attr, daos_event_t *ev);

/**
 * List existing object classes.
 *
 * \param[in]	coh	Container open handle.
 * \param[out]	list	Sink buffer for returned class list.
 * \param[in,out]
 *		anchor	Hash anchor for the next call. It should be set to
 *			zeroes for the first call. It should not be altered
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_list_class(daos_handle_t coh, struct daos_oclass_list *list,
		    daos_anchor_t *anchor, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_OBJ_CLASS_H__ */
