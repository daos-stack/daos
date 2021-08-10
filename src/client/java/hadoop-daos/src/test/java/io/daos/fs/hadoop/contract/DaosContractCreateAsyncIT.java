/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.AbstractContractCreateTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;
import org.apache.hadoop.fs.contract.ContractTestUtils;
import org.junit.Test;

import static org.apache.hadoop.fs.contract.ContractTestUtils.dataset;
import static org.apache.hadoop.fs.contract.ContractTestUtils.writeDataset;

public class DaosContractCreateAsyncIT extends AbstractContractCreateTest {
  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    DaosFSFactory.config(configuration, true);
    return new DaosContractIT(configuration);
  }

  @Test
  public void testCreateNewFile() throws Throwable {
    describe("Foundational 'create a file' test, using builder API=" +
        false);
    Path path = path("testCreateNewFile", false);
    byte[] data = dataset(256, 'a', 'z');
    writeDataset(getFileSystem(), path, data, data.length, 1024 * 1024, false,
        false);
    ContractTestUtils.verifyFileContents(getFileSystem(), path, data);
  }

  @Override
  public void testOverwriteExistingFile() throws Throwable {
    describe("Overwrite an existing file and verify the new data is there, " +
        "use builder API=" + false);
    Path path = path("testOverwriteExistingFile", false);
    byte[] data = dataset(256, 'a', 'z');
    writeDataset(getFileSystem(), path, data, data.length, 1024, false,
        false);
    ContractTestUtils.verifyFileContents(getFileSystem(), path, data);
    byte[] data2 = dataset(10 * 1024, 'A', 'Z');
    writeDataset(getFileSystem(), path, data2, data2.length, 1024, true,
        false);
    ContractTestUtils.verifyFileContents(getFileSystem(), path, data2);
  }
}
