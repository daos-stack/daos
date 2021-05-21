/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileAlreadyExistsException;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.AbstractContractCreateTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;
import org.apache.hadoop.fs.contract.ContractTestUtils;
import org.junit.Test;
import org.junit.internal.AssumptionViolatedException;

import java.io.FileNotFoundException;
import java.io.IOException;

import static org.apache.hadoop.fs.contract.ContractTestUtils.*;
import static org.apache.hadoop.fs.contract.ContractTestUtils.skip;

public class DaosContractCreateIT extends AbstractContractCreateTest {
  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    DaosFSFactory.config(configuration);
    return new DaosContractIT(configuration);
  }

  @Override
  public void testOverwriteNonEmptyDirectory() throws Throwable {
    describe("verify trying to create a file over an empty dir fails, " +
        "use builder API=" + false);
    Path path = path("testOverwriteEmptyDirectory");
    mkdirs(path);
    assertIsDirectory(path);
    byte[] data = dataset(256, 'a', 'z');
    try {
      writeDataset(getFileSystem(), path, data, data.length, 1024, true,
          false);
      assertIsDirectory(path);
      fail("write of file over empty dir succeeded");
    } catch (FileAlreadyExistsException expected) {
      //expected
      handleExpectedException(expected);
    } catch (FileNotFoundException e) {
      handleRelaxedException("overwriting a dir with a file ",
          "FileAlreadyExistsException",
          e);
    } catch (IOException e) {
      handleRelaxedException("overwriting a dir with a file ",
          "FileAlreadyExistsException",
          e);
    }
    assertIsDirectory(path);
  }

  @Override
  public void testOverwriteEmptyDirectory() throws Throwable {
    describe("verify trying to create a file over a non-empty dir fails, " +
        "use builder API=" + false);
    Path path = path("testOverwriteNonEmptyDirectory");
    mkdirs(path);
    try {
      assertIsDirectory(path);
    } catch (AssertionError failure) {
      if (isSupported(CREATE_OVERWRITES_DIRECTORY)) {
        // file/directory hack surfaces here
        throw new AssumptionViolatedException(failure.toString(), failure);
      }
      // else: rethrow
      throw failure;
    }
    Path child = new Path(path, "child");
    writeTextFile(getFileSystem(), child, "child file", true);
    byte[] data = dataset(256, 'a', 'z');
    try {
      writeDataset(getFileSystem(), path, data, data.length, 1024,
          true, false);
      FileStatus status = getFileSystem().getFileStatus(path);

      boolean isDir = status.isDirectory();
      if (!isDir && isSupported(CREATE_OVERWRITES_DIRECTORY)) {
        // For some file systems, downgrade to a skip so that the failure is
        // visible in test results.
        skip("This Filesystem allows a file to overwrite a directory");
      }
      fail("write of file over dir succeeded");
    } catch (FileAlreadyExistsException expected) {
      //expected
      handleExpectedException(expected);
    } catch (FileNotFoundException e) {
      handleRelaxedException("overwriting a dir with a file ",
          "FileAlreadyExistsException",
          e);
    } catch (IOException e) {
      handleRelaxedException("overwriting a dir with a file ",
          "FileAlreadyExistsException",
          e);
    }
    assertIsDirectory(path);
    assertIsFile(child);
  }

  @Override
  public void testCreateFileOverExistingFileNoOverwrite() throws Throwable {
    describe("Verify overwriting an existing file fails, using builder API=" +
        false);
    Path path = path("testCreateFileOverExistingFileNoOverwrite", false);
    byte[] data = dataset(256, 'a', 'z');
    writeDataset(getFileSystem(), path, data, data.length, 1024, false);
    byte[] data2 = dataset(10 * 1024, 'A', 'Z');
    try {
      writeDataset(getFileSystem(), path, data2, data2.length, 1024, false,
          false);
      fail("writing without overwrite unexpectedly succeeded");
    } catch (FileAlreadyExistsException expected) {
      //expected
      handleExpectedException(expected);
    } catch (IOException relaxed) {
      handleRelaxedException("Creating a file over a file with overwrite==false",
          "FileAlreadyExistsException",
          relaxed);
    }
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
}
