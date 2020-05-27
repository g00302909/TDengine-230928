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

#ifndef TDENGINE_MNODE_H
#define TDENGINE_MNODE_H

#ifdef __cplusplus
extern "C" {
#endif

struct SAcctObj;
struct SDnodeObj;
struct SUserObj;
struct SDbObj;
struct SVgObj;
struct STableObj;
struct SRpcMsg;

typedef struct {
  int   len;
  void *rsp;
} SMnodeRsp;

typedef struct SMnodeMsg {
  SMnodeRsp rpcRsp;
  uint8_t   msgType;
  int8_t    received;
  int8_t    successed;
  int8_t    expected;
  int8_t    retry;
  int8_t    maxRetry;
  int32_t   contLen;
  int32_t   code;
  void *    ahandle;
  void *    thandle;
  void *    pCont;
  struct SAcctObj * pAcct;
  struct SDnodeObj *pDnode;
  struct SUserObj * pUser;
  struct SDbObj *   pDb;
  struct SVgObj *   pVgroup;
  struct STableObj *pTable;
} SMnodeMsg;

void    mnodeCreateMsg(SMnodeMsg *pMsg, struct SRpcMsg *rpcMsg);
int32_t mnodeInitMsg(SMnodeMsg *pMsg);
void    mnodeCleanupMsg(SMnodeMsg *pMsg);

int32_t mnodeInitSystem();
int32_t mnodeStartSystem();
void    mnodeCleanupSystem();
void    mgmtStopSystem();
void    sdbUpdateSync();
bool    mnodeIsRunning();
int32_t mnodeProcessRead(SMnodeMsg *pMsg);
int32_t mnodeProcessWrite(SMnodeMsg *pMsg);
int32_t mnodeProcessPeerReq(SMnodeMsg *pMsg);
void    mnodeProcessPeerRsp(struct SRpcMsg *pMsg);
int32_t mnodeRetriveAuth(char *user, char *spi, char *encrypt, char *secret, char *ckey);

#ifdef __cplusplus
}
#endif

#endif
