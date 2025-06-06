.. Copyright (C) Internet Systems Consortium, Inc. ("ISC")
..
.. SPDX-License-Identifier: MPL-2.0
..
.. This Source Code Form is subject to the terms of the Mozilla Public
.. License, v. 2.0.  If a copy of the MPL was not distributed with this
.. file, you can obtain one at https://mozilla.org/MPL/2.0/.
..
.. See the COPYRIGHT file distributed with this work for additional
.. information regarding copyright ownership.

.. _build_bind:

Building BIND 9
---------------

To build on a Unix or Linux system, use:

::

    $ autoreconf -fi ### (only if building from the git repository)
    $ ./configure
    $ make

Several environment variables affect compilation, and they can be set
before running ``configure``. The most significant ones are:

+--------------------+-------------------------------------------------+
| Variable           | Description                                     |
+====================+=================================================+
| ``CC``             | The C compiler to use. ``configure`` tries to   |
|                    | figure out the right one for supported systems. |
+--------------------+-------------------------------------------------+
| ``CFLAGS``         | The C compiler flags. Defaults to include -g    |
|                    | and/or -O2 as supported by the compiler. Please |
|                    | include ``-g`` if ``CFLAGS`` needs to be set.   |
+--------------------+-------------------------------------------------+
| ``LDFLAGS``        | The linker flags. Defaults to an empty string.  |
+--------------------+-------------------------------------------------+

Additional environment variables affecting the build are listed at the
end of the ``configure`` help text, which can be obtained by running the
command:

::

    $ ./configure --help

If using Emacs, the ``make tags`` command may be helpful.

.. _build_dependencies:

Required Libraries
~~~~~~~~~~~~~~~~~~

To build BIND 9, the following packages must be installed:

- a C11-compliant compiler
- ``libcrypto``, ``libssl``
- ``liburcu``
- ``libuv``
- ``perl``
- ``pkg-config`` / ``pkgconfig`` / ``pkgconf``

BIND 9.20 requires ``libuv`` 1.34.0 or higher; using ``libuv`` >= 1.40.0
is recommended. Compiling or running with ``libuv`` 1.35.0 or 1.36.0 is
not supported, as this could lead to an assertion failure in the UDP
receive code. On older systems an updated ``libuv`` package needs to be
installed from sources, such as EPEL, PPA, or other native sources. The
other option is to build and install ``libuv`` from source.

OpenSSL 1.0.2e or newer is required. If the OpenSSL library is installed
in a nonstandard location, specify the prefix using
``--with-openssl=<PREFIX>`` on the ``configure`` command line. To use a
PKCS#11 hardware service module for cryptographic operations,
``engine_pkcs11`` from the OpenSC project must be compiled and used.

The Userspace RCU library ``liburcu`` (https://liburcu.org/) is used
for lock-free data structures and concurrent safe memory reclamation.

On Linux, process capabilities are managed in user space using the
``libcap`` library
(https://git.kernel.org/pub/scm/libs/libcap/libcap.git/), which can be
installed on most Linux systems via the ``libcap-dev`` or
``libcap-devel`` package.

To build BIND from the git repository, the following tools must also be
installed:

- ``autoconf`` (includes ``autoreconf``)
- ``automake``
- ``libtool``

Optional Features
~~~~~~~~~~~~~~~~~

To see a full list of configuration options, run ``configure --help``.

To improve performance, use of the ``jemalloc`` library
(https://jemalloc.net/) is strongly recommended. Version 4.0.0 or newer is
required when in use.

To support :rfc:`DNS over HTTPS (DoH) <8484>`, the server must be linked
with ``libnghttp2`` (https://nghttp2.org/). If the library is
unavailable, ``--disable-doh`` can be used to disable DoH support.

To support the HTTP statistics channel, the server must be linked with
at least one of the following libraries: ``libxml2``
(https://gitlab.gnome.org/GNOME/libxml2/-/wikis/home) or ``json-c``
(https://github.com/json-c/json-c).  If these are installed at a
nonstandard location, then:

- for ``libxml2``, specify the prefix using ``--with-libxml2=/prefix``,
- for ``json-c``, adjust ``PKG_CONFIG_PATH``.

To support compression on the HTTP statistics channel, the server must
be linked against ``zlib`` (https://zlib.net/). If this is installed in
a nonstandard location, specify the prefix using
``--with-zlib=/prefix``.

To support storing configuration data for runtime-added zones in an LMDB
database, the server must be linked with ``liblmdb``
(https://github.com/LMDB/lmdb). If this is installed in a nonstandard
location, specify the prefix using ``--with-lmdb=/prefix``.

To support MaxMind GeoIP2 location-based ACLs, the server must be linked
with ``libmaxminddb`` (https://maxmind.github.io/libmaxminddb/). This is
turned on by default if the library is found; if the library is
installed in a nonstandard location, specify the prefix using
``--with-maxminddb=/prefix``. GeoIP2 support can be switched off with
``--disable-geoip``.

For DNSTAP packet logging, ``libfstrm``
(https://github.com/farsightsec/fstrm) and ``libprotobuf-c``
(https://protobuf.dev) must be installed, and
BIND must be configured with ``--enable-dnstap``.

To support internationalized domain names in :iscman:`dig`, ``libidn2``
(https://www.gnu.org/software/libidn/#libidn2) must be installed. If the
library is installed in a nonstandard location, specify the prefix using
``--with-libidn2=/prefix`` or adjust ``PKG_CONFIG_PATH``.

For line editing in :iscman:`nsupdate` and :iscman:`nslookup`, either the
``readline`` (https://tiswww.case.edu/php/chet/readline/rltop.html) or
the ``libedit`` library (https://www.thrysoee.dk/editline/) must be
installed. If these are installed at a nonstandard location, adjust
``PKG_CONFIG_PATH``. ``readline`` is used by default, and ``libedit``
can be explicitly requested using ``--with-readline=libedit``.

On some platforms it is necessary to explicitly request large file
support to handle files bigger than 2GB. This can be done by using
``--enable-largefile`` on the ``configure`` command line.

Support for the “fixed” RRset-order option can be enabled or disabled by
specifying ``--enable-fixed-rrset`` or ``--disable-fixed-rrset`` on the
``configure`` command line. By default, fixed RRset-order is disabled to
reduce memory footprint.

The ``--enable-querytrace`` option causes :iscman:`named` to log every step
while processing every query. The ``--enable-singletrace`` option turns
on the same verbose tracing, but allows an individual query to be
separately traced by setting its query ID to 0. These options should
only be enabled when debugging, because they have a significant negative
impact on query performance.

``make install`` installs :iscman:`named` and the various BIND 9 libraries. By
default, installation is into /usr/local, but this can be changed with
the ``--prefix`` option when running ``configure``.

The option ``--sysconfdir`` can be specified to set the directory where
configuration files such as :iscman:`named.conf` go by default;
``--localstatedir`` can be used to set the default parent directory of
``run/named.pid``. ``--sysconfdir`` defaults to ``$prefix/etc`` and
``--localstatedir`` defaults to ``$prefix/var``.

macOS
~~~~~

Building on macOS assumes that the “Command Tools for Xcode” are
installed. These can be downloaded from
https://developer.apple.com/xcode/resources/ or, if Xcode is already
installed, simply run ``xcode-select --install``. (Note that an Apple ID
may be required to access the download page.)
