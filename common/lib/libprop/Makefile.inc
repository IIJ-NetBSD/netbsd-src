#	$NetBSD: Makefile.inc,v 1.13 2025/05/14 03:25:45 thorpej Exp $

.PATH:	${.PARSEDIR}

SRCS+=	prop_array.c prop_array_util.c prop_bool.c prop_data.c \
        prop_dictionary.c prop_dictionary_util.c prop_extern.c prop_ingest.c \
        prop_intern.c prop_kern.c prop_number.c prop_object.c prop_stack.c \
	prop_string.c
