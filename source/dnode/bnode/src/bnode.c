/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "bndInt.h"

SBnode *bndOpen(const SBnodeOpt *pOption) {
  SBnode *pBnode = calloc(1, sizeof(SBnode));
  return pBnode;
}

void bndClose(SBnode *pBnode) { free(pBnode); }

int32_t bndGetLoad(SBnode *pBnode, SBnodeLoad *pLoad) { return 0; }

int32_t bndProcessWMsgs(SBnode *pBnode, SArray *pMsgs) { return 0; }
