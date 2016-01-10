/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/action
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#ifndef __THREADS_H__
#define __THREADS_H__


#include "server.h"

void *acceptThread();
void *workThread();
void *cronThread();
void *runworkThread();


#endif
