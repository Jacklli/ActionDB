/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/Action
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#ifndef __EXECCOMMAND_H__ 

#include "object.h"
#include "buffer.h"

int execSetCommand(char (*argv)[ARGUMENTCNT]);
valObject *execGetCommand(char (*argv)[ARGUMENTCNT]);
void execShutdownCommand();

#endif
