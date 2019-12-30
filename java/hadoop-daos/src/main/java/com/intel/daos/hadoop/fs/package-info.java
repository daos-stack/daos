/**
 *
 * DAOS Implementation of Hadoop File System.
 *
 * To get instance of DAOS Implementation, {@link com.intel.daos.hadoop.fs.DaosFileSystem}, user just needs to make
 * below statements after proper hadoop configuration.
 * <code>
 *   Configuration cfg = new Configuration();
 *   cfg.set(Constants.DAOS_POOL_UUID, poolUuid);
 *   cfg.set(Constants.DAOS_CONTAINER_UUID, containerUuid);
 *   cfg.set(Constants.DAOS_POOL_SVC, svc);
 *   FileSystem fileSystem = FileSystem.get(URI.create("daos://ip:port/"), cfg);
 * </code>
 *
 * Be noted the schema is {@link com.intel.daos.hadoop.fs.Constants#DAOS_SCHEMA}
 *
 * For hadoop configuration, please refer {@linkplain com.intel.daos.hadoop.fs.DaosFileSystem DaosFileSystem}
 *
 * @see com.intel.daos.hadoop.fs.DaosFileSystem
 */


package com.intel.daos.hadoop.fs;