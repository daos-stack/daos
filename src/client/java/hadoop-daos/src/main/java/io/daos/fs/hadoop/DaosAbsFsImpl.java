/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.CommonConfigurationKeysPublic;
import org.apache.hadoop.fs.DelegateToFileSystem;
import org.apache.hadoop.fs.FsServerDefaults;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.util.DataChecksum;

/**
 * {@link org.apache.hadoop.fs.AbstractFileSystem} impl delegated to {@link DaosFileSystem}
 */
public class DaosAbsFsImpl extends DelegateToFileSystem {

  public DaosAbsFsImpl(URI theUri, Configuration conf)
          throws IOException, URISyntaxException {
    super(theUri, new DaosFileSystem(), conf, Constants.DAOS_SCHEMA, false);
  }

  /**
   * not used in DAOS. Just return -1 as fake port.
   *
   * @return -1
   */
  @Override
  public int getUriDefaultPort() {
    return -1;
  }

  @Override
  @Deprecated
  public FsServerDefaults getServerDefaults() throws IOException {
    Configuration conf = fsImpl.getConf();
    // CRC32 is chosen as default as it is available in all
    // releases that support checksum.
    // The client trash configuration is ignored.
    return new FsServerDefaults(
            conf.getInt(Constants.DAOS_BLOCK_SIZE, Constants.DEFAULT_DAOS_BLOCK_SIZE),
            conf.getInt("io.bytes.per.checksum", 512),
            conf.getInt(Constants.DAOS_WRITE_BUFFER_SIZE, Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE),
            (short)1,
            conf.getInt(Constants.DAOS_READ_BUFFER_SIZE, Constants.DEFAULT_DAOS_READ_BUFFER_SIZE),
            false,
            CommonConfigurationKeysPublic.FS_TRASH_INTERVAL_DEFAULT,
            DataChecksum.Type.CRC32);
  }

  @Override
  public FsServerDefaults getServerDefaults(final Path f) throws IOException {
    return getServerDefaults();
  }

  @Override
  public Path resolvePath(final Path p) throws IOException {
    return fsImpl.resolvePath(p);
  }

  @Override
  public String toString() {
    final StringBuilder sb = new StringBuilder("DaosAbsFsImpl{");
    sb.append("URI =").append(fsImpl.getUri());
    sb.append("; fsImpl=").append(fsImpl);
    sb.append('}');
    return sb.toString();
  }
}
