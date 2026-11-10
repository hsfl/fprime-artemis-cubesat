module ArtemisRpiTeensyDeployment {
  include "instances.shared.fppi"

  instance payloadManager: Components.PayloadManager base id 0x1000B000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 33

  instance payloadDriverNeutronSim: Components.PayloadDriver_NeutronSim base id 0x10021000 \
    queue size Default.QUEUE_SIZE \
    stack size Default.STACK_SIZE \
    priority 27
}
