.PATH.S: ${.PARSEDIR}
SHA_SRCS = sha1-armv8.S sha512-armv8.S keccak1600-armv8.S
SHACPPFLAGS = -DSHA1_ASM -DSHA256_ASM -DKECCAK1600_ASM
KECCAKNI = yes
.include "../../sha.inc"
