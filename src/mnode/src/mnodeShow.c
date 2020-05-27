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

#define _DEFAULT_SOURCE
#include "os.h"
#include "taosmsg.h"
#include "taoserror.h"
#include "trpc.h"
#include "tsched.h"
#include "tutil.h"
#include "ttimer.h"
#include "tgrant.h"
#include "tglobal.h"
#include "tcache.h"
#include "mnode.h"
#include "dnode.h"
#include "mnodeDef.h"
#include "mnodeInt.h"
#include "mnodeAcct.h"
#include "mnodeDb.h"
#include "mnodeDnode.h"
#include "mnodeMnode.h"
#include "mnodeProfile.h"
#include "mnodeSdb.h"
#include "mnodeShow.h"
#include "mnodeTable.h"
#include "mnodeUser.h"
#include "mnodeVgroup.h"
#include "mnodeWrite.h"
#include "mnodeRead.h"

static int32_t mnodeProcessShowMsg(SMnodeMsg *mnodeMsg);
static int32_t mnodeProcessRetrieveMsg(SMnodeMsg *mnodeMsg);
static int32_t mnodeProcessHeartBeatMsg(SMnodeMsg *mnodeMsg);
static int32_t mnodeProcessConnectMsg(SMnodeMsg *mnodeMsg);
static int32_t mnodeProcessUseMsg(SMnodeMsg *mnodeMsg);

static void  mnodeFreeShowObj(void *data);
static bool  mnodeCheckQhandle(uint64_t qhandle);
static void *mnodeSaveQhandle(void *qhandle, int32_t size);
static void  mnodeFreeQhandle(void *qhandle, bool forceRemove);

extern void *tsMnodeTmr;
static void *tsQhandleCache = NULL;
static SShowMetaFp     tsMnodeShowMetaFp[TSDB_MGMT_TABLE_MAX]     = {0};
static SShowRetrieveFp tsMnodeShowRetrieveFp[TSDB_MGMT_TABLE_MAX] = {0};

int32_t mnodeInitShow() {
  mnodeAddReadMsgHandle(TSDB_MSG_TYPE_CM_SHOW, mnodeProcessShowMsg);
  mnodeAddReadMsgHandle(TSDB_MSG_TYPE_CM_RETRIEVE, mnodeProcessRetrieveMsg);
  mnodeAddReadMsgHandle(TSDB_MSG_TYPE_CM_HEARTBEAT, mnodeProcessHeartBeatMsg);
  mnodeAddReadMsgHandle(TSDB_MSG_TYPE_CM_CONNECT, mnodeProcessConnectMsg);
  mnodeAddReadMsgHandle(TSDB_MSG_TYPE_CM_USE_DB, mnodeProcessUseMsg);
  
  tsQhandleCache = taosCacheInitWithCb(tsMnodeTmr, 10, mnodeFreeShowObj);
  return 0;
}

void mnodeCleanUpShow() {
  if (tsQhandleCache != NULL) {
    taosCacheCleanup(tsQhandleCache);
    tsQhandleCache = NULL;
  }
}

void mnodeAddShowMetaHandle(uint8_t showType, SShowMetaFp fp) {
  tsMnodeShowMetaFp[showType] = fp;
}

void mnodeAddShowRetrieveHandle(uint8_t msgType, SShowRetrieveFp fp) {
  tsMnodeShowRetrieveFp[msgType] = fp;
}

static char *mnodeGetShowType(int32_t showType) {
  switch (showType) {
    case TSDB_MGMT_TABLE_ACCT:    return "show accounts";
    case TSDB_MGMT_TABLE_USER:    return "show users";
    case TSDB_MGMT_TABLE_DB:      return "show databases";
    case TSDB_MGMT_TABLE_TABLE:   return "show tables";
    case TSDB_MGMT_TABLE_DNODE:   return "show dnodes";
    case TSDB_MGMT_TABLE_MNODE:   return "show mnodes";
    case TSDB_MGMT_TABLE_VGROUP:  return "show vgroups";
    case TSDB_MGMT_TABLE_METRIC:  return "show stables";
    case TSDB_MGMT_TABLE_MODULE:  return "show modules";
    case TSDB_MGMT_TABLE_QUERIES: return "show queries";
    case TSDB_MGMT_TABLE_STREAMS: return "show streams";
    case TSDB_MGMT_TABLE_CONFIGS: return "show configs";
    case TSDB_MGMT_TABLE_CONNS:   return "show connections";
    case TSDB_MGMT_TABLE_SCORES:  return "show scores";
    case TSDB_MGMT_TABLE_GRANTS:  return "show grants";
    case TSDB_MGMT_TABLE_VNODES:  return "show vnodes";
    default:                      return "undefined";
  }
}

static int32_t mnodeProcessShowMsg(SMnodeMsg *pMsg) {
  SCMShowMsg *pShowMsg = pMsg->pCont;
  if (pShowMsg->type >= TSDB_MGMT_TABLE_MAX) {
    return TSDB_CODE_INVALID_MSG_TYPE;
  }

  if (!tsMnodeShowMetaFp[pShowMsg->type] || !tsMnodeShowRetrieveFp[pShowMsg->type]) {
    mError("show type:%s is not support", mnodeGetShowType(pShowMsg->type));
    return TSDB_CODE_OPS_NOT_SUPPORT;
  }

  int32_t size = sizeof(SCMShowRsp) + sizeof(SSchema) * TSDB_MAX_COLUMNS + TSDB_EXTRA_PAYLOAD_SIZE;
  SCMShowRsp *pShowRsp = rpcMallocCont(size);
  if (pShowRsp == NULL) {
    return TSDB_CODE_SERV_OUT_OF_MEMORY;
  }

  int32_t showObjSize = sizeof(SShowObj) + htons(pShowMsg->payloadLen);
  SShowObj *pShow = (SShowObj *) calloc(1, showObjSize);
  pShow->signature  = pShow;
  pShow->type       = pShowMsg->type;
  pShow->payloadLen = htons(pShowMsg->payloadLen);
  strcpy(pShow->db, pShowMsg->db);
  memcpy(pShow->payload, pShowMsg->payload, pShow->payloadLen);

  pShow = mnodeSaveQhandle(pShow, showObjSize);
  pShowRsp->qhandle = htobe64((uint64_t) pShow);

  mTrace("show:%p, type:%s, start to get meta", pShow, mnodeGetShowType(pShowMsg->type));
  int32_t code = (*tsMnodeShowMetaFp[pShowMsg->type])(&pShowRsp->tableMeta, pShow, pMsg->thandle);
  if (code == 0) {
    pMsg->rpcRsp.rsp = pShowRsp;
    pMsg->rpcRsp.len = sizeof(SCMShowRsp) + sizeof(SSchema) * pShow->numOfColumns;
    return TSDB_CODE_SUCCESS;
  } else {
    mError("show:%p, type:%s, failed to get meta, reason:%s", pShow, mnodeGetShowType(pShowMsg->type), tstrerror(code));
    mnodeFreeQhandle(pShow, true);
    return code;
  }
}

static int32_t mnodeProcessRetrieveMsg(SMnodeMsg *pMsg) {
  int32_t rowsToRead = 0;
  int32_t size = 0;
  int32_t rowsRead = 0;
  SRetrieveTableMsg *pRetrieve = pMsg->pCont;
  pRetrieve->qhandle = htobe64(pRetrieve->qhandle);

  /*
   * in case of server restart, apps may hold qhandle created by server before
   * restart, which is actually invalid, therefore, signature check is required.
   */
  if (!mnodeCheckQhandle(pRetrieve->qhandle)) {
    mError("retrieve:%p, qhandle:%p is invalid", pRetrieve, pRetrieve->qhandle);
    return TSDB_CODE_INVALID_QHANDLE;
  }

  SShowObj *pShow = (SShowObj *)pRetrieve->qhandle;
  mTrace("show:%p, type:%s, retrieve data", pShow, mnodeGetShowType(pShow->type));

  if ((pRetrieve->free & TSDB_QUERY_TYPE_FREE_RESOURCE) != TSDB_QUERY_TYPE_FREE_RESOURCE) {
    rowsToRead = pShow->numOfRows - pShow->numOfReads;
  }

  /* return no more than 100 meters in one round trip */
  if (rowsToRead > 100) rowsToRead = 100;

  /*
   * the actual number of table may be larger than the value of pShow->numOfRows, if a query is
   * issued during a continuous create table operation. Therefore, rowToRead may be less than 0.
   */
  if (rowsToRead < 0) rowsToRead = 0;
  size = pShow->rowSize * rowsToRead;

  size += 100;
  SRetrieveTableRsp *pRsp = rpcMallocCont(size);

  // if free flag is set, client wants to clean the resources
  if ((pRetrieve->free & TSDB_QUERY_TYPE_FREE_RESOURCE) != TSDB_QUERY_TYPE_FREE_RESOURCE)
    rowsRead = (*tsMnodeShowRetrieveFp[pShow->type])(pShow, pRsp->data, rowsToRead, pMsg->thandle);

  if (rowsRead < 0) {
    rpcFreeCont(pRsp);
    mnodeFreeQhandle(pShow, false);
    assert(false);
    return TSDB_CODE_ACTION_IN_PROGRESS;
  }

  pRsp->numOfRows = htonl(rowsRead);
  pRsp->precision = htonl(TSDB_TIME_PRECISION_MILLI);  // millisecond time precision

  pMsg->rpcRsp.rsp = pRsp;
  pMsg->rpcRsp.len = size;

  if (rowsToRead == 0) {
    mnodeFreeQhandle(pShow, true);
  } else {
    mnodeFreeQhandle(pShow, false);
  }

  return TSDB_CODE_SUCCESS;
}

static int32_t mnodeProcessHeartBeatMsg(SMnodeMsg *pMsg) {
  SCMHeartBeatRsp *pHBRsp = (SCMHeartBeatRsp *) rpcMallocCont(sizeof(SCMHeartBeatRsp));
  if (pHBRsp == NULL) {
    return TSDB_CODE_SERV_OUT_OF_MEMORY;
  }

  pHBRsp->onlineDnodes = htonl(mnodeGetOnlinDnodesNum());
  pHBRsp->totalDnodes = htonl(mnodeGetDnodesNum());
  mnodeGetMnodeIpSetForShell(&pHBRsp->ipList);
  
  /*
   * TODO
   * Dispose kill stream or kill query message
   */
  pHBRsp->queryId = 0;
  pHBRsp->streamId = 0;
  pHBRsp->killConnection = 0;

  pMsg->rpcRsp.rsp = pHBRsp;
  pMsg->rpcRsp.len = sizeof(SCMHeartBeatRsp);
  
  return TSDB_CODE_SUCCESS;
}

static int32_t mnodeProcessConnectMsg(SMnodeMsg *pMsg) {
  SCMConnectMsg *pConnectMsg = pMsg->pCont;
  int32_t code = TSDB_CODE_SUCCESS;

  SRpcConnInfo connInfo;
  if (rpcGetConnInfo(pMsg->thandle, &connInfo) != 0) {
    mError("thandle:%p is already released while process connect msg", pMsg->thandle);
    code = TSDB_CODE_INVALID_MSG_CONTENT;
    goto connect_over;
  }

  code = taosCheckVersion(pConnectMsg->clientVersion, version, 3);
  if (code != TSDB_CODE_SUCCESS) {
    goto connect_over;
  }

  SUserObj *pUser = pMsg->pUser;
  SAcctObj *pAcct = pUser->pAcct;

  if (pConnectMsg->db[0]) {
    char dbName[TSDB_TABLE_ID_LEN * 3] = {0};
    sprintf(dbName, "%x%s%s", pAcct->acctId, TS_PATH_DELIMITER, pConnectMsg->db);
    SDbObj *pDb = mnodeGetDb(dbName);
    if (pDb == NULL) {
      code = TSDB_CODE_INVALID_DB;
      goto connect_over;
    }
    mnodeDecDbRef(pDb);
  }

  SCMConnectRsp *pConnectRsp = rpcMallocCont(sizeof(SCMConnectRsp));
  if (pConnectRsp == NULL) {
    code = TSDB_CODE_SERV_OUT_OF_MEMORY;
    goto connect_over;
  }

  sprintf(pConnectRsp->acctId, "%x", pAcct->acctId);
  strcpy(pConnectRsp->serverVersion, version);
  pConnectRsp->writeAuth = pUser->writeAuth;
  pConnectRsp->superAuth = pUser->superAuth;

  mnodeGetMnodeIpSetForShell(&pConnectRsp->ipList);

connect_over:
  if (code != TSDB_CODE_SUCCESS) {
    mLError("user:%s login from %s, result:%s", connInfo.user, taosIpStr(connInfo.clientIp), tstrerror(code));
  } else {
    mLPrint("user:%s login from %s, result:%s", connInfo.user, taosIpStr(connInfo.clientIp), tstrerror(code));
    pMsg->rpcRsp.rsp = pConnectRsp;
    pMsg->rpcRsp.len = sizeof(SCMConnectRsp);
  }

  return code;
}

static int32_t mnodeProcessUseMsg(SMnodeMsg *pMsg) {
  SCMUseDbMsg *pUseDbMsg = pMsg->pCont;

  int32_t code = TSDB_CODE_SUCCESS;
  if (pMsg->pDb == NULL) pMsg->pDb = mnodeGetDb(pUseDbMsg->db);
  if (pMsg->pDb == NULL) {
    code = TSDB_CODE_INVALID_DB;
  }

  return code;
}

static bool mnodeCheckQhandle(uint64_t qhandle) {
  void *pSaved = taosCacheAcquireByData(tsQhandleCache, (void *)qhandle);
  if (pSaved == (void *)qhandle) {
    mTrace("show:%p, is retrieved", qhandle);
    return true;
  } else {
    mTrace("show:%p, is already released", qhandle);
    return false;
  }
}

static void *mnodeSaveQhandle(void *qhandle, int32_t size) {
  if (tsQhandleCache != NULL) {
    char key[24];
    sprintf(key, "show:%p", qhandle);
    void *newQhandle = taosCachePut(tsQhandleCache, key, qhandle, size, 60);
    free(qhandle);

    mTrace("show:%p, is saved", newQhandle);
    return newQhandle;
  }

  return NULL;
}

static void mnodeFreeShowObj(void *data) {
  SShowObj *pShow = data;
  sdbFreeIter(pShow->pIter);
  mTrace("show:%p, is destroyed", pShow);
}

static void mnodeFreeQhandle(void *qhandle, bool forceRemove) {
  mTrace("show:%p, is released, force:%s", qhandle, forceRemove ? "true" : "false");
  taosCacheRelease(tsQhandleCache, &qhandle, forceRemove);
}
