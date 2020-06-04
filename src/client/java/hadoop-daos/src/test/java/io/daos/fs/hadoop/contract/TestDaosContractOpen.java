package io.daos.fs.hadoop.contract;

import io.daos.fs.hadoop.Constants;
import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.contract.AbstractContractOpenTest;
import org.apache.hadoop.fs.contract.AbstractFSContract;
import org.junit.Test;

import static org.apache.hadoop.fs.contract.ContractTestUtils.createFile;
import static org.apache.hadoop.fs.contract.ContractTestUtils.dataset;

public class TestDaosContractOpen extends AbstractContractOpenTest {

  @Override
  protected AbstractFSContract createContract(Configuration configuration) {
    configuration.addResource("daos-site.xml");
    configuration.set(Constants.DAOS_POOL_UUID, DaosFSFactory.pooluuid);
    configuration.set(Constants.DAOS_CONTAINER_UUID, DaosFSFactory.contuuid);
    configuration.set(Constants.DAOS_POOL_SVC, DaosFSFactory.svc);
    return new io.daos.fs.hadoop.contract.DaosContract(configuration);
  }
}
