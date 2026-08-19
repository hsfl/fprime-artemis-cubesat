# ======================================================================
# \title  config/FpConfig.fpp
# \brief  Project FPP alias configuration.
#
# This project-owned copy overrides the F Prime default configuration.
# FwSizeStoreType is U32 so one full Boson 320 image fits in one standard
# F Prime data-product container.
# ======================================================================

####
# Integer type aliases:
# Used for the project to override types supplied by the platform for things like sizes, indices, etc.
####

@ The unsigned type of larger sizes internal to the software,
@ e.g., memory buffer sizes, file sizes. Must be unsigned.
type FwSizeType = PlatformSizeType

@ The signed type of larger sizes internal to the software, used
@ for signed offsets, e.g., file seek offsets. Must be signed.
type FwSignedSizeType = PlatformSignedSizeType

@ The type of smaller indices internal to the software, used
@ for array indices, e.g., port indices. Must be signed.
type FwIndexType = PlatformIndexType

@ The type of arguments to assert functions.
type FwAssertArgType = PlatformAssertArgType

@ The type of task priorities used.
type FwTaskPriorityType = PlatformTaskPriorityType

@ The type of queue priorities used.
type FwQueuePriorityType = PlatformQueuePriorityType

@ The id type.
type FwIdType = U32

@ The type of task priorities used.
type FwTaskIdType = PlatformTaskIdType

####
# GDS type aliases:
# Used for the project to override types shared with GDSes and other remote systems.
####

@ The type of a telemetry channel identifier
dictionary type FwChanIdType = FwIdType

@ The type of a data product identifier
type FwDpIdType = FwIdType

@ The type of a data product priority
type FwDpPriorityType = U32

@ The type of an event identifier
dictionary type FwEventIdType = FwIdType

@ The type of a command opcode
dictionary type FwOpcodeType = FwIdType

@ The type of a parameter identifier
type FwPrmIdType = FwIdType

@ The type used to serialize a size value.
@ U32 is required for a single full-resolution Boson 320 data product.
dictionary type FwSizeStoreType = U32

@ The type used to serialize a time context value
dictionary type FwTimeContextStoreType = U8

@ The type of a telemetry packet identifier
type FwTlmPacketizeIdType = U16

@ The type of a trace identifier
type FwTraceIdType = U32

@ The type used to serialize a C++ enumeration constant
@ FPP enumerations are serialized according to their representation types
type FwEnumStoreType = I32

@ The type used to serialize a time base value
type FwTimeBaseStoreType = U16

@ Define enumeration for Time base types
dictionary enum TimeBase : FwTimeBaseStoreType {
    TB_NONE = 0              @< No time base has been established (Required)
    TB_PROC_TIME = 1         @< Indicates processor cycle time. Not tied to external time
    TB_WORKSTATION_TIME = 2  @< Time reported by a workstation. Used for testing. (Required)
    TB_SC_TIME = 3,          @< Time reported by the spacecraft clock
    TB_DONT_CARE = 0xFFFF    @< Don't care value in sequences (Required)
} default TB_NONE;
