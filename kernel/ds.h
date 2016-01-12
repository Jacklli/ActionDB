/*
* Copyright 2015, Liyinlong.  All rights reserved.
* https://github.com/Jacklli/ActionDB
*
* Use of this source code is governed by GPL v2 license
* that can be found in the License file.
*
* Author: Liyinlong (yinlong.lee at hotmail.com)
*/

#ifndef __OBJECT_H__
#define __OBJECT_H__

int rocksSet(char *key, char *val);
int rocksGet(char *key);
void rocksShutdown();

#endif
