package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.DaosUtils;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.contract.AbstractBondedFSContract;

import java.io.IOException;

public class DaosContractIT extends AbstractBondedFSContract {
  private static final String CONTRACT_XML = "contract/daos.xml";
  protected DaosContractIT(Configuration conf) {
        super(conf);
        addConfResource(CONTRACT_XML);
    }

  @Override
  public String getScheme() {
        return "daos";
    }
}
