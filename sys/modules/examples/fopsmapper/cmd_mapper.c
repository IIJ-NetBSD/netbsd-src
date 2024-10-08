/*	$NetBSD: cmd_mapper.c,v 1.3 2024/04/17 18:10:27 riastradh Exp $	*/

/*-
 * Copyright (c) 2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: cmd_mapper.c,v 1.3 2024/04/17 18:10:27 riastradh Exp $");

#include <sys/mman.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	_PATH_DEV_MAPPER	"/dev/fopsmapper"

int main(int argc, char **argv)
{
	int devfd;
	char *map = NULL;

	if ((devfd = open(_PATH_DEV_MAPPER, O_RDONLY)) == -1)
		err(EXIT_FAILURE, "Cannot open %s", _PATH_DEV_MAPPER);

	map = mmap(0, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, devfd, 0);
	if (map == MAP_FAILED)
		err(EXIT_FAILURE, "Mapping failed");

	printf("Message from device: %s\n", map);

	if (munmap(map, sysconf(_SC_PAGESIZE)) == -1)
		err(EXIT_FAILURE, "Unmap failed");

	if (close(devfd) == -1)
		err(EXIT_FAILURE, "Cannot close %s", _PATH_DEV_MAPPER);

	return EXIT_SUCCESS;
}
