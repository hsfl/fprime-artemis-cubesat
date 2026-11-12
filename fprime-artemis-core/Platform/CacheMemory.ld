ASSERT(ADDR(OCRAM) == 0x20280000, "Cache OCRAM address mismatch")
ASSERT(SIZEOF(OCRAM) == 196608, "Exactly one 192 KiB product cache is required")
ASSERT(SIZEOF(OCRAM) <= 262144, "Product cache exceeds internal OCRAM")
ASSERT(_end >= ORIGIN(RAM) && _end <= ORIGIN(RAM) + LENGTH(RAM), "malloc arena escapes main RAM")
