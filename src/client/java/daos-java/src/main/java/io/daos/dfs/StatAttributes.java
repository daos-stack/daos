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

package io.daos.dfs;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Java attributes representing DAOS <code>struct stat</code> which has below information
 * * objId     long             (8 bytes)
 * * mode_t    st_mode;         (4 bytes)
 * * uid_t     st_uid;          (4 bytes)
 * * gid_t     st_gid;          (4 bytes)
 * * off_t     st_size;         (8 bytes)
 * * blkcnt_t  st_blocks        (8 bytes)
 * * blksize_t st_blocksize     (8 bytes)
 * * struct timespec st_atim;   (16 bytes)
 * * struct timespec st_mtim;   (16 bytes)
 * * struct timespec st_ctim;   (16 bytes)
 * * file      boolean          (1 byte)
 * * length of username         (4)
 * * username string            (32 bytes max)
 * * length of groupname        (4)
 * * groupname string           (32 bytes max)
 *
 * <p>
 * This Java representative adds two more fields, object id and file (is file).
 */
public class StatAttributes {

  private final long objId;

  private final int mode;

  private final int uid;

  private final int gid;

  private final long blockCnt;

  private final long blockSize;

  private final long length;

  private final TimeSpec accessTime;

  private final TimeSpec modifyTime;

  private final TimeSpec createTime;

  private final boolean file;

  private final String username;

  private final String groupname;

  private static final ByteOrder DEFAULT_ORDER = ByteOrder.nativeOrder();

  protected StatAttributes(ByteBuffer buffer) {
    buffer.order(DEFAULT_ORDER);
    objId = buffer.getLong();
    mode = buffer.getInt();
    uid = buffer.getInt();
    gid = buffer.getInt();
    blockCnt = buffer.getLong();
    blockSize = buffer.getLong();
    length = buffer.getLong();
    accessTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    modifyTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    createTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    file = buffer.get() > 0;
    username = getName(buffer);
    groupname = getName(buffer);
  }

  private String getName(ByteBuffer buffer) {
    int len = buffer.getInt();
    if (len > 0) {
      byte[] bytes = new byte[len];
      buffer.get(bytes);
      return new String(bytes);
    }
    return "";
  }

  public int getMode() {
    return mode;
  }

  public long getObjId() {
    return objId;
  }

  public TimeSpec getAccessTime() {
    return accessTime;
  }

  public TimeSpec getCreateTime() {
    return createTime;
  }

  public int getGid() {
    return gid;
  }

  public long getBlockCnt() {
    return blockCnt;
  }

  public long getBlockSize() {
    return blockSize;
  }

  public long getLength() {
    return length;
  }

  public TimeSpec getModifyTime() {
    return modifyTime;
  }

  public int getUid() {
    return uid;
  }

  public boolean isFile() {
    return file;
  }

  public String getUsername() {
    return username;
  }

  public String getGroupname() {
    return groupname;
  }

  /**
   * buffer size in bytes to hold all fields in binary.
   * see the class description for size of each field.
   *
   * @return total buffer size
   */
  public static int objectSize() {
    return 4 * 8 + 5 * 4 + 3 * 16 + 1 + 64; //165
  }

  /**
   * Java corresponding to C TimeSpec.
   */
  public static class TimeSpec {
    private final long seconds;
    private final long nano;

    public TimeSpec(long seconds, long nano) {
      this.seconds = seconds;
      this.nano = nano;
    }

    public long getNano() {
      return nano;
    }

    public long getSeconds() {
      return seconds;
    }
  }
}
