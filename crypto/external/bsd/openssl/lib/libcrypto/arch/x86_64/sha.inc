.PATH.S: ${.PARSEDIR}
SHA_SRCS = sha1-x86_64.S sha1-mb-x86_64.S keccak1600-x86_64.S
SHACPPFLAGS = -DSHA1_ASM -DKECCAK1600_ASM
KECCAKNI = yes
SHA_SRCS += sha256-x86_64.S sha512-x86_64.S sha256-mb-x86_64.S
SHACPPFLAGS+= -DSHA256_ASM -DSHA512_ASM
.include "../../sha.inc"
