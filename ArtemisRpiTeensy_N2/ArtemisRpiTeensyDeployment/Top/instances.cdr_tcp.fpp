module ArtemisRpiTeensyDeployment {
  enum Ports_RateGroups {
    rateGroup1
    rateGroup2
    rateGroup3
  }

  instance tcpServer: Drv.TcpServer base id 0x10027000
}
