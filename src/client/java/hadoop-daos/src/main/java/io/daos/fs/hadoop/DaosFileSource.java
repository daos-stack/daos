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

import io.daos.dfs.DaosFile;

public abstract class DaosFileSource {

  protected DaosFile daosFile;

  public DaosFileSource(DaosFile daosFile) {
    this.daosFile = daosFile;
  }

  public abstract long limit();

  public abstract void position(int i);

  public abstract void get(byte[] buf, int off, int actualLen);

  public abstract void clear();

  public abstract void close();

  public abstract int read(long nextReadPos, int length);

  public abstract void write(int b);

  public abstract void write(byte[] buf, int off, int len);

  public abstract void flush();
}
