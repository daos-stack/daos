/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

package io.daos.obj;

public enum OpenMode {
  // shared read
  DAOS_OO_RO(1 << 1),
  // shared read & write, no cache for write
  DAOS_OO_RW(1 << 2),
  // exclusive write, data can be cached
  DAOS_OO_EXCL(1 << 3),
  // unsupported: random I/O
  DAOS_OO_IO_RAND(1 << 4),
  // unsupported sequential I/O
  DAOS_OO_IO_SEQ(1 << 5);

  private int value;

  OpenMode(int value) {
    this.value = value;
  }
}
