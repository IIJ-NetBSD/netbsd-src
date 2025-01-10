# $NetBSD: cond-token-var.mk,v 1.10 2025/01/10 23:00:38 rillig Exp $
#
# Tests for expressions in .if conditions.
#
# Note the fine distinction between a variable and an expression.
# A variable has a name and a value.  To access the value, one writes an
# expression of the form ${VAR}.  This is a simple
# expression.  Expressions can get more complicated by adding
# variable modifiers such as in ${VAR:Mpattern}.
#
# XXX: Strictly speaking, variable modifiers should be called expression
# modifiers instead since they only modify the expression, not the variable.
# Well, except for the assignment modifiers, these do indeed change the value
# of the variable.

D=	defined
DEF=	defined


# A defined variable may appear on either side of the comparison.
.if ${DEF} == ${DEF}
# expect+1: ok
.  info ok
.else
.  error
.endif

# A variable that appears on the left-hand side must be defined.
# expect+1: Malformed conditional '${UNDEF} == ${DEF}'
.if ${UNDEF} == ${DEF}
.  error
.endif

# A variable that appears on the right-hand side must be defined.
# expect+1: Malformed conditional '${DEF} == ${UNDEF}'
.if ${DEF} == ${UNDEF}
.  error
.endif

# A defined variable may appear as an expression of its own.
.if ${DEF}
.endif

# An undefined variable on its own generates a parse error.
# expect+1: Malformed conditional '${UNDEF}'
.if ${UNDEF}
.endif

# The :U modifier turns an undefined expression into a defined expression.
# Since the expression is defined now, it doesn't generate any parse error.
.if ${UNDEF:U}
.endif


# The same as above, for single-letter variables without braces or
# parentheses.

# A defined variable may appear on either side of the comparison.
.if $D == $D
.endif

# A variable on the left-hand side must be defined.
# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional '$U == $D'
.if $U == $D
.endif

# A variable on the right-hand side must be defined.
# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional '$D == $U'
.if $D == $U
.endif

# A defined variable may appear as an expression of its own.
.if $D
.endif

# An undefined variable without a comparison operator generates a parse error.
# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional '$U'
.if $U
.endif


# If the value of the expression is a number, it is compared against
# zero.
.if ${:U0}
.  error
.endif
.if !${:U1}
.  error
.endif

# If the value of the expression is not a number, any non-empty
# value evaluates to true, even if there is only whitespace.
.if ${:U}
.  error
.endif
.if !${:U }
.  error
.endif
.if !${:Uanything}
.  error
.endif

.MAKEFLAGS: -dv
# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional 'x${UNDEF1}y == "${UNDEF2}" || 0x${UNDEF3}'
.if x${UNDEF1}y == "${UNDEF2}" || 0x${UNDEF3}
.endif

# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional 'x${DEF}y == "${UNDEF2}" || 0x${UNDEF3}'
.if x${DEF}y == "${UNDEF2}" || 0x${UNDEF3}
.endif

# FIXME: Replace "Malformed" with "Undefined variable".
# expect+1: Malformed conditional 'x${DEF}y == "${DEF}" || 0x${UNDEF3}'
.if x${DEF}y == "${DEF}" || 0x${UNDEF3}
.endif
.MAKEFLAGS: -d0
