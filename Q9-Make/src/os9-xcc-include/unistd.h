/*--------------------------------------------------------------------------------------
 * File:        unistd.h
 * Description: Expose target SDK declarations required by the POSIX adapter.
 * Owner:       cla / afoe
 * Version:     0.01
 *
 * This file is part of the Q9-Forge repository. See https://github.com/Q9-Forge.
 *
 * Edition History
 * Date    | Ver. | Description                                               | By
 * 26-11-26| 0.01 | Add file header and revise comments.                      | cla
 *--------------------------------------------------------------------------------------*/
/* Enable prototypes and define FILE before the target declarations are included. */
#define _OPT_PROTOS
#include <stdio.h>
#include <modes.h>
#include <UNIX/os9def.h>

#define STDOUT_FILENO 1
