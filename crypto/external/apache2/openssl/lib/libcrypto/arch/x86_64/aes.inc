.PATH.S: ${.PARSEDIR}
AES_SRCS += \
aes-gcm-avx512.S \
aesni-xts-avx512.S \
aes-x86_64.S \
aesni-mb-x86_64.S \
aesni-sha1-x86_64.S \
aesni-sha256-x86_64.S \
aesni-x86_64.S \
bsaes-x86_64.S \
vpaes-x86_64.S

AESCPPFLAGS = -DAES_ASM -DVPAES_ASM -DBSAES_ASM
AESNI = yes
.include "../../aes.inc"
