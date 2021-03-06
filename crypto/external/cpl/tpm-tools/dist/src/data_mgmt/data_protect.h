/*
 * The Initial Developer of the Original Code is International
 * Business Machines Corporation. Portions created by IBM
 * Corporation are Copyright (C) 2005 International Business
 * Machines Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Common Public License as published by
 * IBM Corporation; either version 1 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Common Public License for more details.
 *
 * You should have received a copy of the Common Public License
 * along with this program; if not, a copy can be viewed at
 * http://www.opensource.org/licenses/cpl1.0.php.
 */

#ifndef __DATA_PROTECT_H
#define __DATA_PROTECT_H

#define TOKEN_AES_BLOCKSIZE		16
#define TOKEN_BUFFER_SIZE		TOKEN_AES_BLOCKSIZE * 256

#define TOKEN_INPUT_FILE_ERROR		_("Error, an input file must be specified\n" )
#define TOKEN_OUTPUT_FILE_ERROR		_("Error, an output file must be specified\n" )
#define TOKEN_NO_KEY_ERROR		_("Error, protection key is not available\n")

#endif
