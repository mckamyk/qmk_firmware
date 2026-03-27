VIA_ENABLE = no
LTO_ENABLE = no

# Suppress LTO type mismatch warnings in vendor code
CFLAGS += -Wno-lto-type-mismatch
CXXFLAGS += -Wno-lto-type-mismatch
