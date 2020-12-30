/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
   * not used in DAOS. Just return 1 as fake port.
   *
   * @return 1
   */
  @Override
  public int getUriDefaultPort() {
    return 1;
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
