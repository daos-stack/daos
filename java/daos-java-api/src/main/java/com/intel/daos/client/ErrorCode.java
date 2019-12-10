package com.intel.daos.client;

public class ErrorCode {
  private final int code;
  private final String msg;

  public ErrorCode(int code, String msg){
    this.code = code;
    this.msg = msg;
  }

  public int getCode() {
    return code;
  }

  public String getMsg() {
    return msg;
  }
}
