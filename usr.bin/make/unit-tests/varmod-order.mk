# $NetBSD: varmod-order.mk,v 1.17 2024/08/29 20:20:36 rillig Exp $
#
# Tests for the :O variable modifier and its variants, which either sort the
# words of the value or shuffle them.

WORDS=		one two three four five six seven eight nine ten
NUMBERS=	8 5 4 9 1 7 6 10 3 2	# in English alphabetical order

.if ${WORDS:O} != "eight five four nine one seven six ten three two"
.  error ${WORDS:O}
.endif

# expect+1: Bad modifier ":OX"
_:=	${WORDS:OX}

# expect+1: Bad modifier ":OxXX"
_:=	${WORDS:OxXX}

# expect+1: Unclosed expression, expecting '}' for modifier "O"
_:=	${WORDS:O
# expect+1: Unclosed expression, expecting '}' for modifier "On"
_:=	${NUMBERS:On
# expect+1: Unclosed expression, expecting '}' for modifier "Onr"
_:=	${NUMBERS:Onr

# Shuffling numerically doesn't make sense, so don't allow 'x' and 'n' to be
# combined.
#
# expect+2: Bad modifier ":Oxn"
# expect+1: Malformed conditional '${NUMBERS:Oxn}'
.if ${NUMBERS:Oxn}
.  error
.else
.  error
.endif

# Extra characters after ':On' are detected and diagnosed.
#
# expect+2: Bad modifier ":On_typo"
# expect+1: Malformed conditional '${NUMBERS:On_typo}'
.if ${NUMBERS:On_typo}
.  error
.else
.  error
.endif

# Extra characters after ':Onr' are detected and diagnosed.
#
# expect+2: Bad modifier ":Onr_typo"
# expect+1: Malformed conditional '${NUMBERS:Onr_typo}'
.if ${NUMBERS:Onr_typo}
.  error
.else
.  error
.endif

# Extra characters after ':Orn' are detected and diagnosed.
#
# expect+2: Bad modifier ":Orn_typo"
# expect+1: Malformed conditional '${NUMBERS:Orn_typo}'
.if ${NUMBERS:Orn_typo}
.  error
.else
.  error
.endif

# Repeating the 'n' is not supported.  In the typical use cases, the sorting
# criteria are fixed, not computed, therefore allowing this redundancy does
# not make sense.
#
# expect+2: Bad modifier ":Onn"
# expect+1: Malformed conditional '${NUMBERS:Onn}'
.if ${NUMBERS:Onn}
.  error
.else
.  error
.endif

# Repeating the 'r' is not supported as well, for the same reasons as above.
#
# expect+2: Bad modifier ":Onrr"
# expect+1: Malformed conditional '${NUMBERS:Onrr}'
.if ${NUMBERS:Onrr}
.  error
.else
.  error
.endif

# Repeating the 'r' is not supported as well, for the same reasons as above.
#
# expect+2: Bad modifier ":Orrn"
# expect+1: Malformed conditional '${NUMBERS:Orrn}'
.if ${NUMBERS:Orrn}
.  error
.else
.  error
.endif


# If a modifier that starts with ':O' is not one of the known sort or shuffle
# forms, it is a parse error.  Several other modifiers such as ':H' or ':u'
# fall back to the SysV modifier, for example, ':H=new' is not the standard
# ':H' modifier but instead replaces a trailing 'H' with 'new' in each word.
# There is no such fallback for the ':O' modifiers.
SWITCH=	On
# expect+2: Bad modifier ":On=Off"
# expect+1: Malformed conditional '${SWITCH:On=Off} != "Off"'
.if ${SWITCH:On=Off} != "Off"
.  error
.else
.  error
.endif

all:
