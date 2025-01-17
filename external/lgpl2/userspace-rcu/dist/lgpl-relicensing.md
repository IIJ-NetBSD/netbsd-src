<!--
SPDX-FileCopyrightText: 2009 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>

SPDX-License-Identifier: CC-BY-4.0
-->

# Userspace-RCU LGPL 2.1+ re-licensing

Mathieu Desnoyers
May 13th, 2009

IBM Corporation allowed LGPLv2.1+ licensing of their contribution to the
userspace RCU library in a patch submitted on May 8, 2009 from Paul E.
McKenney and reviewed by Steven L. Bennett:

    https://lists.lttng.org/pipermail/lttng-dev/2009-May/012835.html

I (Mathieu Desnoyers) re-implemented ACCESS_ONCE(), likely(), unlikely() and
barrier() from scratch without reference to the original code.

    commit id : 2dc5fa0f7cfbfb0a64a7a67b39626650e863f16a

Bert Wesarg <bert.wesarg@googlemail.com> approved LGPL re-licensing of his
patch in an email dated May 13, 2009 :

    http://lkml.org/lkml/2009/5/13/16

xchg() primitives has been rewritten from a MIT-licensed cmpxchg for Intel
and powerpc. They are MIT-licensed and therefore usable in LGPL code.
This cmpxchg code was obtained from the atomic_ops project:

    http://www.hpl.hp.com/research/linux/atomic_ops/

I (Mathieu Desnoyers) wrote the remainder of the code.

The license for the library files in this project was therefore changed to
LGPLv2.1 on May 13, 2009, as detailed in LICENSE.
