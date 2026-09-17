module CdhCore{

    # Uncomment the following block and comment the above block to use TlmPacketizer instead of TlmChan
    instance tlmSend: Svc.TlmPacketizer base id CdhCoreConfig.BASE_ID + 0x06000 \
       queue size CdhCoreConfig.QueueSizes.tlmSend \
       stack size CdhCoreConfig.StackSizes.tlmSend \
       priority CdhCoreConfig.Priorities.tlmSend \
       cpu CdhCoreConfig.CpuAffinities.tlmSend \
    {
       
       # This phase text is emitted verbatim into every deployment's
       # TopologyAc.cpp, so it cannot name a deployment-specific packet list:
       # more than one deployment shares a build tree (ReferenceDeployment and
       # FlightControllerDeployment both build for zephyr), and a config
       # override replaces this file for the whole tree, not per deployment.
       # Instead it calls an accessor that each deployment declares in its own
       # TopologyDefs.hpp and defines in its own Topology.cpp, returning that
       # deployment's packet list. A new deployment must supply one too.
       phase Fpp.ToCpp.Phases.configComponents """
       CdhCore::tlmSend.setPacketList(
           FprimeArtemisConfig::tlmPacketList(),
           Svc::IGNORE_OMIT_LIST, // Allows smaller MAX_PACKETIZER_CHANNELS as ignored packets are not stored
           1
       );
       """
    }
}
