
/* 
* SPDX-License-Identifier: Apache-2.0
* Copyright 2019-2021 Western Digital Corporation or its affiliates.
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* 
* http:*www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
/**
* @file   comrv.c
* @author Ronen Haen
* @date   21.06.2019
* @brief  The file implements the COM-RV engine
* 
*/
/* disable warning for inline-asm - we are using comrv
   reserved register and don't want to see these warnings.
   Note - if one of the inline assembly in this file is changes
   this line need to be commented so that we can verify the change
   didn't cause new warnings */
_Pragma("clang diagnostic ignored \"-Winline-asm\"")

/**
* include files
*/
#include "comrv_api.h"
#include "comrv_config.h"
#include "comrv.h"
#include "comrv_info.h"
#ifdef D_CTI
   #include "cti_api.h"
#endif /* D_CTI */

/**
* definitions
*/
#define D_COMRV_END_OF_STACK                          0xDEAD
#define D_COMRV_MRU_ITEM                              0xFF
#define D_COMRV_LRU_ITEM                              0xFF
#define D_COMRV_MAX_GROUP_NUM                         0xFFFF
#define D_COMRV_DWORD_IN_BITS                         32
#define D_COMRV_ENTRY_LOCKED                          1
#define D_COMRV_ENTRY_UNLOCKED                        0
#define D_COMRV_CANDIDATE_LIST_SIZE                   (1+(D_COMRV_OVL_GROUP_SIZE_MAX/D_COMRV_OVL_GROUP_SIZE_MIN))
#define D_COMRV_ENTRY_TOKEN_INIT_VALUE                0x0001FFFE
#define D_COMRV_ENTRY_PROPERTIES_INIT_VALUE           0x04
#define D_COMRV_ENTRY_PROPERTIES_RESET_MASK           0xC3
#define D_COMRV_OFFSET_SCALE_VALUE                    4
#define D_COMRV_INVOKE_CALLEE_BIT_0                   1
#define D_COMRV_RET_CALLER_BIT_0                      0
#define D_COMRV_NUM_BITS_DWORD                        32
#if (D_COMRV_NUM_OF_CACHE_ENTRIES % D_COMRV_NUM_BITS_DWORD)
  #define D_COMRV_EVICT_CANDIDATE_MAP_SIZE            ((D_COMRV_NUM_OF_CACHE_ENTRIES/sizeof(u32_t)) + 1)
#else
  #define D_COMRV_EVICT_CANDIDATE_MAP_SIZE            (D_COMRV_NUM_OF_CACHE_ENTRIES/sizeof(u32_t))
#endif
#define D_COMRV_PROPERTIES_SIZE_FLD_SHIFT_AMNT        2
#define D_COMRV_GRP_SIZE_IN_BYTES_SHIFT_AMNT          9
#define D_COMRV_GROUP_NOT_FOUND                       0xFFFF
#define D_COMRV_EMPTY_CALLEE_MULTIGROUP               0xFFFF
#define D_COMRV_LAST_MULTI_GROUP_ENTRY                0
#define D_COMRV_TABLES_OFFSET                         0
#define D_COMRV_TABBLES_NOT_LOADED                    0
#define D_COMRV_TABBLES_LOADED                        1
#define D_COMRV_LUI_TOKEN_20_BITS_MASK                0xFFFFF000
#define D_COMRV_ADDI_TOKEN_12_BITS_MASK               0xFFF00000
#define D_COMRV_ADDI_TOKEN_SHMT                       20
#define D_COMRV_TABLES_TOKEN                          0x00000001
#define D_COMRV_STATE_CLEAR                           0
#define D_COMRV_STATE_ENTRY                           1
#define D_COMRV_STATE_EXIT                            2
#define D_COMRV_POST_SEARCH_LOAD                      4
#define D_COMRV_POST_SEARCH_LOAD_INTERRUPTED          8
#define D_COMRV_S0_REG_STACK_OFFSET_TMP               0
#define D_COMRV_S1_REG_STACK_OFFSET_TMP               1
#define D_COMRV_A0_REG_STACK_OFFSET_TMP               2
#define D_COMRV_A1_REG_STACK_OFFSET_TMP               3
#define D_COMRV_A2_REG_STACK_OFFSET_TMP               4
#define D_COMRV_A3_REG_STACK_OFFSET_TMP               5
#define D_COMRV_A4_REG_STACK_OFFSET_TMP               6
#define D_COMRV_A5_REG_STACK_OFFSET_TMP               7
#define D_COMRV_A6_REG_STACK_OFFSET_TMP               8
#define D_COMRV_A7_REG_STACK_OFFSET_TMP               9
#define D_COMRV_S2_REG_STACK_OFFSET_TMP               10
#define D_COMRV_S3_REG_STACK_OFFSET_TMP               11
#define D_COMRV_S4_REG_STACK_OFFSET_TMP               12
#define D_COMRV_S5_REG_STACK_OFFSET_TMP               13
#define D_COMRV_S6_REG_STACK_OFFSET_TMP               14
#define D_COMRV_S7_REG_STACK_OFFSET_TMP               15
#define D_COMRV_S8_REG_STACK_OFFSET_TMP               16
#define D_COMRV_S9_REG_STACK_OFFSET_TMP               17
#define D_COMRV_S10_REG_STACK_OFFSET_TMP              18
#define D_COMRV_S11_REG_STACK_OFFSET_TMP              19
#define D_COMRV_RA_REG_STACK_OFFSET_TMP               20
#define D_COMRV_T5_REG_STACK_OFFSET_TMP               21
#define D_COMRV_SP_REG_STACK_OFFSET_TMP               22
#define D_COMRV_STATE_STACK_OFFSET_TMP                23
/* mask value for cache entry properties -
   bit 0 - usEvictLock and bit 6 usEntryLock */
#define D_COMRV_ANY_LOCK_MASK                         0x41
#define D_COMRV_DEBRUIJN32                            0x077CB531
#define D_COMRV_DEBRUIJN32_SHFT_AMNT                  27

#ifndef D_CTI
   #define M_CTI_SYNC_POINT(x)
   #define M_CTI_MARK_DEFRAG()
#endif /* D_CTI */

#define D_COMRV_LOAD_DISABLED                         0
#define D_COMRV_LOAD_ENABLED                          1
#define D_COMRV_DATA_OVERLAY_IND                      1
#define D_COMRV_MAX_REF_COUNT                         255
#define D_COMRV_ADDRESS_TOKEN                         1
#define D_COMRV_TRUE                                  1
#define D_COMRV_FALSE                                 0

/**
* macros
*/
#ifdef D_COMRV_OVL_DATA_SUPPORT
/* write to token register (t5) */
#define M_COMRV_WRITE_TOKEN_REG(x)       asm volatile ("mv t5, %0" : : "r" (x) );
#endif /* D_COMRV_OVL_DATA_SUPPORT */
/* read comrv task stack register (tp) */
#define M_COMRV_TASK_STACK_REG(x)        asm volatile ("mv %0, tp" : "=r" (x)  : );
/* read token register (t5) */
#define M_COMRV_READ_TOKEN_REG(x)        asm volatile ("mv %0, t5" : "=r" (x)  : );
/* write stack pool register (t4) */
#define M_COMRV_WRITE_POOL_REG(x)        asm volatile ("mv t4, %0" : : "r" (x) );
/* read stack pool register (t4) */
#define M_COMRV_READ_POOL_REG(x)         asm volatile ("mv %0, t4" : "=r" (x)  : );
/* read comrv stack register (t3) */
#define M_COMRV_READ_STACK_REG(x)        asm volatile ("mv %0, t3" : "=r" (x)  : );
/* write comrv stack register (t3) */
#define M_COMRV_WRITE_STACK_REG(x)       asm volatile ("mv t3, %0" : : "r" (x) );
/* set comrv entry engine address */
#define M_COMRV_SET_ENTRY_ADDR(address)  asm volatile ("la x31, %0" : : "i"(address) : );
/* get comrv entry engine address */
#define M_COMRV_READ_ENTRY_ADDR(x)       asm volatile ("mv %0, t6" : "=r" (x)  : );
/* set the comrv stack pool and comrv stack registers */
#if __riscv_xlen == 64
 #define M_COMRV_SET_STACK_ADDR(address) asm volatile ("la t3, %0" : : "i"(address) : ); \
                                         asm volatile ("ld t3, 0x0(t3)"  : : : );
#elif __riscv_xlen == 32
 #define M_COMRV_SET_STACK_ADDR(address) asm volatile ("la t3, %0" : : "i"(address) : ); \
                                         asm volatile ("lw t3, 0x0(t3)"  : : : );
#endif
/* overlay group size in D_COMRV_OVL_GROUP_SIZE_MIN granularity */
#define M_COMRV_GET_OVL_GROUP_SIZE(unToken)          (pOverlayOffsetTable[unToken.stFields.uiOverlayGroupID+1] - \
                                                      pOverlayOffsetTable[unToken.stFields.uiOverlayGroupID])
/* overlay group size in bytes */
#define M_COMRV_GET_OVL_GROUP_SIZE_IN_BYTES(unToken) (M_COMRV_GET_OVL_GROUP_SIZE(unToken) << 9)
/* token offset in bytes */
#define M_COMRV_GET_TOKEN_OFFSET_IN_BYTES(unToken)   ((unToken.stFields.uiOffset) * D_COMRV_OFFSET_SCALE_VALUE)
/* overlay group offset in bytes */
#define M_COMRV_GET_GROUP_OFFSET_IN_BYTES(unToken)   ((pOverlayOffsetTable[unToken.stFields.uiOverlayGroupID]) << 9)
/* convert a given entry size in to an entry properties value */
#define M_COMRV_CONVERT_TO_ENTRY_SIZE_FROM_VAL(val)  ((val) << D_COMRV_PROPERTIES_SIZE_FLD_SHIFT_AMNT)
/* */
#define M_COMRV_GROUP_SIZE_TO_BYTES(groupSize)       ((groupSize) << D_COMRV_GRP_SIZE_IN_BYTES_SHIFT_AMNT)
/* macro for verifying overlay group CRC */
#ifdef D_COMRV_CRC
#define M_COMRV_VERIFY_CRC(pAddressToCalc, usMemSizeInBytes, uiExpectedResult)   \
      if (M_COMRV_BUILTIN_EXPECT(comrvCrcCalcHook(pAddressToCalc, usMemSizeInBytes, uiExpectedResult),0))  \
      {                                                           \
         M_COMRV_ERROR(stErrArgs, D_COMRV_CRC_CHECK_ERR, unToken.uiValue);   \
      }

#define M_COMRV_VERIFY_TABLES_CRC(pAddressToCalc, usMemSizeInBytes)  \
      unToken.uiValue = D_COMRV_TABLES_TOKEN; \
      M_COMRV_VERIFY_CRC(pAddressToCalc, (usMemSizeInBytes)-sizeof(u32_t), \
                         *((u32_t*)(pAddressToCalc + ((usMemSizeInBytes)-sizeof(u32_t)))));

#else
#define M_COMRV_VERIFY_CRC(pAddressToCalc, usMemSizeInBytes, uiExpectedResult)
#define M_COMRV_VERIFY_TABLES_CRC(pAddressToCalc, usMemSizeInBytes)
#endif /* D_COMRV_CRC */

/* calculate the cache address for a given cache entry */
#define M_COMRV_CALC_CACHE_ADDR_IN_BYTES_FROM_ENTRY(ucEntryIndex) ((u08_t*)pComrvCacheBaseAddress + (ucEntryIndex<<9))

#define M_COMRV_CACHE_SIZE_IN_BYTES()   ((u08_t*)&__OVERLAY_CACHE_END__ - (u08_t*)pComrvCacheBaseAddress)
/* this macro is only for code readability (the symbol '__OVERLAY_CACHE_START__'
   is defined in the linker script and defines the start address of comrv cache) */
#define pComrvCacheBaseAddress          (&__OVERLAY_CACHE_START__)
/* address of offset table (last comrv cache entry) */
#define pOverlayOffsetTable             ((u16_t*)((u08_t*)pComrvCacheBaseAddress + (g_stComrvCB.ucLastCacheEntry*(D_COMRV_OVL_GROUP_SIZE_MIN))))
/* address of multi group table */
#define pOverlayMultiGroupTokensTable   ((comrvOverlayToken_t*)(pOverlayOffsetTable + g_stComrvCB.ucMultiGroupOffset))
/* Place a label, the debugger will stop here to query the overlay manager current status.  */
#define M_COMRV_DEBUGGER_HOOK_SYMBOL()            asm volatile (".globl _ovly_debug_event\n" \
                                                      "_ovly_debug_event:");
/* offset and multigroup tables total size in bytes */
#define D_COMRV_TABLES_TOTAL_SIZE_IN_BYTES ((u32_t)&__OVERLAY_MULTIGROUP_TABLE_END - (u32_t)&__OVERLAY_GROUP_TABLE_START)
/* extract the return offset for a given return address */
#define M_COMRV_EXTRACT_RETURN_OFFSET(pReturnAddress, uiFuncOffset) \
      ((((u32_t)(pReturnAddress)) - uiFuncOffset - ( pComrvStackFrame->ucAlignmentToMaxGroupSize << D_COMRV_GRP_SIZE_IN_BYTES_SHIFT_AMNT)) & (D_COMRV_OVL_GROUP_SIZE_MAX-1))

/* Get the index of the rightmost set bit */
#ifndef D_BIT_MANIPULATION
   /* with the absence of RISC-V bitmanip extension we use a faster method to find the
      location of the first set bit - http://supertech.csail.mit.edu/papers/debruijn.pdf */
   #define M_COMRV_GET_SET_BIT_INDEX(uiFindFirstSet)   ucArrDeBruijnBitPos[((u32_t)(uiFindFirstSet * D_COMRV_DEBRUIJN32)) >> D_COMRV_DEBRUIJN32_SHFT_AMNT]
#else
   // TODO: update this macro to use bitmnip instructions */
   // TODO: Nati - also add macros for clz, ctz.
   #define M_COMRV_GET_SET_BIT_INDEX(uiFindFirstSet)
   #error "M_COMRV_GET_SET_BIT_INDEX missing implementation"
#endif /* D_BIT_MANIPULATION */

/**
* types
*/

/**
* local prototypes
*/
void*        comrvMemset                     (void* pMemory, s32_t siVal, u32_t uiSizeInDwords);
static void  comrvClearComrvState            (void);
static u08_t comrvGetEvictionCandidates      (u08_t ucRequestedEvictionSize, u08_t* pEvictCandidatesList);
static void  comrvUpdateCacheEvectionParams  (u08_t ucEntryIndex);
static u16_t comrvSearchForLoadedOverlayGroup(comrvOverlayToken_t unToken);
/**
* external prototypes
*/
/* main comrv entry function - all overlay functions are invoked
   through this function (the address of comrvEntry() is set in reg t6) */
extern void  comrvEntry               (void);
extern void  comrvEntryDisable        (void);
extern void  comrvEntryNotInitialized (void);
extern void  comrv_pre_ret_to_asm_engine(u32_t uiPrevIntStatus, void *pComrvAsmEntryAddress);

/* user hook functions - user application must implement the following functions */
extern void  comrvErrorHook           (const comrvErrorArgs_t* pErrorArgs);
extern void  comrvMemcpyHook          (void* pDest, void* pSrc, u32_t uiSizeInBytes);
extern void* comrvLoadOvlayGroupHook  (comrvLoadArgs_t* pLoadArgs);
#ifdef D_COMRV_RTOS_SUPPORT
extern u32_t comrvEnterCriticalSectionHook(void);
extern u32_t comrvExitCriticalSectionHook(void);
#endif /* D_COMRV_RTOS_SUPPORT */
#ifdef D_COMRV_CRC
extern u32_t comrvCrcCalcHook         (const void* pAddress, u16_t usMemSizeInBytes, u32_t uiExpectedResult);
#endif /* D_COMRV_CRC */
#ifdef D_COMRV_FW_INSTRUMENTATION
extern void  comrvInstrumentationHook (const comrvInstrumentationArgs_t* pInstArgs);
#endif /* D_COMRV_FW_INSTRUMENTATION */
extern void comrvInvalidateDataCacheHook(const void* pAddress, u32_t uiNumSizeInBytes);
#ifdef D_COMRV_RTOS_SUPPORT
extern void comrv_ret_from_callee(void);
extern void comrvEntry_context_switch(void);
extern void comrv_ret_from_callee_context_switch(void);
#endif /* D_COMRV_RTOS_SUPPORT */

/**
* global variables
*/
/* comrv information:
 * bits 0- 3: multi group field offset
 * bits 4- 5: eviction policy
 * bits 6-31: reserved
 */
D_COMRV_USED D_COMRV_RODATA_SECTION
static const u32_t g_uiComrvInfo    = (D_COMRV_MULTIGROUP_OFFSET | (D_COMRV_EVICTION_POLICY << 4));
/* comrv information:
 * bits 0  - 15: minor version
 * bits 16 - 31: major version
 */
D_COMRV_USED D_COMRV_RODATA_SECTION
static const u32_t g_uiComrvVersion = ((D_COMRV_VERSION_MAJOR << 16) | (D_COMRV_VERSION_MINOR));
/* global comrv control block */
D_COMRV_DATA_SECTION static comrvCB_t         g_stComrvCB;
/* global comrv stack pool */
D_COMRV_DATA_SECTION static comrvStackFrame_t g_stComrvStackPool[D_COMRV_CALL_STACK_DEPTH];

/* symbols defining the start and end of the overlay cache */
extern void *__OVERLAY_CACHE_START__, *__OVERLAY_CACHE_END__;
/* symbol defining the start of the offset management table */
extern void *__OVERLAY_GROUP_TABLE_START;
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
/* symbol defining the start of the multigroup management table */
extern void *__OVERLAY_MULTIGROUP_TABLE_START;
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
/* symbol defining the end of the overlay table */
extern void *__OVERLAY_MULTIGROUP_TABLE_END;
/* symbol defining comrv text section start address */
extern void *COMRV_TEXT_SEC;

/**
* @brief COM-RV initialization function
*
* @param  pInitParams - initialization parameters (currently no args)
*
* @return none
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvInit(comrvInitArgs_t* pInitArgs)
{
   u08_t              ucIndex;
   comrvStackFrame_t* pStackPool   = g_stComrvStackPool;
#ifdef D_COMRV_VERIFY_INIT_ARGS
   comrvErrorArgs_t   stErrArgs;
#endif /* D_COMRV_VERIFY_INIT_ARGS */

#ifdef D_COMRV_VERIFY_INIT_ARGS
   /* verify cache configuration - size and alignment to D_COMRV_OVL_GROUP_SIZE_MIN */
   if (M_COMRV_BUILTIN_EXPECT(((u32_t)&__OVERLAY_CACHE_END__ - (u32_t)&__OVERLAY_CACHE_START__) != D_COMRV_OVL_CACHE_SIZE_IN_BYTES ||
       (M_COMRV_CACHE_SIZE_IN_BYTES()) % D_COMRV_OVL_GROUP_SIZE_MIN, 0) )
   {
      M_COMRV_ERROR(stErrArgs, D_COMRV_INVALID_INIT_PARAMS_ERR, D_COMRV_INVALID_TOKEN);
   }
#endif /* D_COMRV_VERIFY_INIT_ARGS */

   /* set the address of COMRV entry point (disabled) in register t6 - end user won't be able
      to call overlay functions until comrv tables are loaded */
   M_COMRV_SET_ENTRY_ADDR(comrvEntryDisable);

   /* initialize comrv cache control block - including offset/multi-group entry */
   comrvReset(E_RESET_TYPE_ALL);

   /* initialize all stack entries */
   for (ucIndex = 0 ; ucIndex < D_COMRV_CALL_STACK_DEPTH ; ucIndex++, pStackPool++)
   {
      pStackPool->ssOffsetPrevFrame            = (s16_t)-sizeof(comrvStackFrame_t);
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      pStackPool->tCalleeMultiGroupTableEntry = (multigroupEntryIndex_t)D_COMRV_EMPTY_CALLEE_MULTIGROUP;
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
   }

   /* point to the last frame */
   pStackPool--;

   /* set the address of COMRV stack pool register t4 */
   M_COMRV_WRITE_POOL_REG(pStackPool);

#ifndef D_COMRV_RTOS_SUPPORT
   /* in baremetal applications, stack register is initialized here;
      this must be done after the stack pool register was
      initialized (M_COMRV_WRITE_POOL_REG) */
   comrvInitApplicationStack();
#endif /* D_COMRV_RTOS_SUPPORT */

   /* check if end user enables loading offset and multi group tables */
   if (pInitArgs->ucCanLoadComrvTables != 0)
   {
      comrvLoadTables();
   }

#ifdef D_COMRV_LOAD_CONFIG_SUPPORT
   /* load operation is enabled by default */
   g_stComrvCB.ucLoadEnabled = D_COMRV_LOAD_ENABLED;
#endif /* D_COMRV_LOAD_CONFIG_SUPPORT */
}

/**
* @brief This function is performing the following:
* 1.  load requested token (from t5 register)
* 2.  search for the requested token in the loaded cache
* 2.a for multi group search for each multi group token
* 3.  if group not loaded, load it
* 4.  return the address of the function (call or return address) or data
*
* @param none
*
* @return void* - address of the overlay function/data
*/
D_COMRV_TEXT_SECTION void* comrvGetAddressFromToken(void* pReturnAddress)
{
   comrvCacheEntry_t   *pEntry;
   comrvOverlayToken_t  unToken;
#ifdef D_COMRV_ERROR_NOTIFICATIONS
   comrvErrorArgs_t     stErrArgs;
#endif /* D_COMRV_ERROR_NOTIFICATIONS */
   comrvLoadArgs_t      stLoadArgs;
   u08_t                ucIsInvoke;
   comrvStackFrame_t   *pComrvStackFrame;
   void                *pAddress, *pNextEvictCandidateCacheAddress, *pDestinationAddress;
   u16_t                usOverlayGroupSize, usOffset, usSearchResultIndex;
   u08_t                ucNumOfEvictionCandidates, ucIndex, ucSizeOfEvictionCandidates, ucNeighbourIndex;
   u08_t                ucEntryIndex, ucEvictCandidateList[D_COMRV_CANDIDATE_LIST_SIZE];
#ifdef D_COMRV_FW_INSTRUMENTATION
   comrvInstrumentationArgs_t stInstArgs;
   u32_t                uiProfilingIndication;
#endif /* D_COMRV_FW_INSTRUMENTATION */
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
   u32_t                  uiTemp;
   multigroupEntryIndex_t tSelectedMultiGroupEntry;
   comrvOverlayToken_t*   pMultigroup = (comrvOverlayToken_t*)pOverlayMultiGroupTokensTable;
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
#ifdef D_COMRV_RTOS_SUPPORT
   #ifndef D_COMRV_MULTI_GROUP_SUPPORT
      u32_t                  uiTemp;
   #endif /* D_COMRV_MULTI_GROUP_SUPPORT */
   u32_t                  ret;
   u32_t                  uiPrevIntState;
#elif defined(D_COMRV_FW_INSTRUMENTATION)
   u32_t                  uiPrevIntState;
#endif /* D_COMRV_RTOS_SUPPORT */
   comrvCacheEntry_t stCacheEntry;

   /* read the requested token value (t5) */
   M_COMRV_READ_TOKEN_REG(unToken.uiValue);

   /* read comrv stack register (t3) */
   M_COMRV_READ_STACK_REG(pComrvStackFrame);

   /* get the invoke callee indication */
   ucIsInvoke = (u32_t)pComrvStackFrame & D_COMRV_INVOKE_CALLEE_BIT_0;
   /* are we calling a new overlay function */
   if (ucIsInvoke == D_COMRV_INVOKE_CALLEE_BIT_0)
   {
      /* clear the invoke indication from pComrvStackFrame address */
      pComrvStackFrame = (comrvStackFrame_t*)((u32_t)pComrvStackFrame & (~D_COMRV_INVOKE_CALLEE_BIT_0));
      /* write back the stack register after bit 0 was cleared*/
      M_COMRV_WRITE_STACK_REG(pComrvStackFrame);

#ifdef D_COMRV_FW_INSTRUMENTATION
      uiProfilingIndication = D_COMRV_NO_LOAD_AND_INVOKE_IND;
#endif /* D_COMRV_FW_INSTRUMENTATION */
   }
   /* we are returning to an overlay function or accessing overlay data */
   else
   {
#ifdef D_COMRV_FW_INSTRUMENTATION
      uiProfilingIndication = D_COMRV_NO_LOAD_AND_RETURN_IND;
#endif /* D_COMRV_FW_INSTRUMENTATION */
   }

   /* comrv test sync point - trigger sw int and force context switch */
   M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_BEFORE_ENTER_CRITICAL_SEC);

   /* we need to make sure that from this point
      we won't have new overlay requests - we alow context swiches */
   M_COMRV_ENTER_CRITICAL_SECTION();

   /* comrv test sync point - trigger sw int and force context switch */
   M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_AFTER_ENTER_CRITICAL_SEC);

#ifdef D_COMRV_MULTI_GROUP_SUPPORT
   /* if the requested token isn't a multi-group token */
   if (unToken.stFields.uiMultiGroup == 0)
   {
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
      /* search for token */
      usSearchResultIndex = comrvSearchForLoadedOverlayGroup(unToken);
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      /* must be set to empty in case no multi-group */
      tSelectedMultiGroupEntry = (multigroupEntryIndex_t)D_COMRV_EMPTY_CALLEE_MULTIGROUP;
   }
   /* search for a multi-group overlay token */
   else
   {
      /* first ucEntryIndex to search from in the multi group table is determined by the overlayGroupID
         field of the requested token */
      ucEntryIndex = unToken.stFields.uiOverlayGroupID;
      do
      {
         /* search for the token */
         usSearchResultIndex = comrvSearchForLoadedOverlayGroup(pMultigroup[ucEntryIndex++]);
      /* continue the search as long as the group wasn't found and we have additional tokens */
      } while ((usSearchResultIndex == D_COMRV_GROUP_NOT_FOUND) && (pMultigroup[ucEntryIndex].uiValue != D_COMRV_LAST_MULTI_GROUP_ENTRY));

      /* save the selected multi group entry */
      tSelectedMultiGroupEntry = ucEntryIndex-1;
   }
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */

#ifndef D_COMRV_MULTI_GROUP_SUPPORT
   /* get the group size */
   usOverlayGroupSize = M_COMRV_GET_OVL_GROUP_SIZE(unToken);
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */

   /* if the data/function is not loaded we need to evict and load it */
   if (usSearchResultIndex == D_COMRV_GROUP_NOT_FOUND)
   {
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      /* if the requested token is a multi-group token */
      if (unToken.stFields.uiMultiGroup)
      {
         // TODO: need to have a more sophisticated way to select the multi-group */
         /* save the selected multi group entry; tSelectedMultiGroupEntry is used to
            update comrv stack frame with the loaded multi group table entry.
            It is used to calculate the actual return offset in case we
            are returning to a multi group token */
         tSelectedMultiGroupEntry = unToken.stFields.uiOverlayGroupID;
         /* for now we take the first token in the list of tokens */
         unToken = pMultigroup[tSelectedMultiGroupEntry];
      }
      /* get the group size */
      usOverlayGroupSize = M_COMRV_GET_OVL_GROUP_SIZE(unToken);
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */

      /* get eviction candidates according to the requested pOverlayGroupSize */
      ucNumOfEvictionCandidates = comrvGetEvictionCandidates(usOverlayGroupSize, ucEvictCandidateList);

      ucEntryIndex = 0;

      /* if you have no/some candidates */
      if (M_COMRV_BUILTIN_EXPECT(ucNumOfEvictionCandidates == 0, 0))
      {
         M_COMRV_EXIT_CRITICAL_SECTION();
         M_COMRV_ERROR(stErrArgs, D_COMRV_NOT_ENOUGH_ENTRIES, unToken.uiValue);
      }
      /* we need to handle cache fragmentation since we got more than 1 eviction candidate */
      else
      {
         /* no handling to the last eviction candidate */
         ucNumOfEvictionCandidates--;
         /* loop all eviction candidates entries */
         while (ucEntryIndex < ucNumOfEvictionCandidates)
         {
            /* get the candidate entry index */
            ucIndex = ucEvictCandidateList[ucEntryIndex++];
            /* get neighbour index */
            ucNeighbourIndex = ucIndex + g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usSizeInMinGroupSizeUnits;
            /* verify we are not coping from neighbour candidate entries */
            if (ucNeighbourIndex != ucEvictCandidateList[ucEntryIndex])
            {
               M_CTI_MARK_DEFRAG();
               /* get the candidate cache entry address */
               pDestinationAddress = M_COMRV_CALC_CACHE_ADDR_IN_BYTES_FROM_ENTRY(ucIndex);
               /* calc the source address - we point here to the cache area
                  from which we want to copy the overlay group:
                  cache-entry-address + current-entry-group-size = address of the neighbour cache entry  */
               pAddress = pDestinationAddress + M_COMRV_GET_OVL_GROUP_SIZE_IN_BYTES(g_stComrvCB.stOverlayCache[ucIndex].unToken);
               /* get the cache address of the next evict candidate - it is used to calculate
                  the amount of memory to copy */
               pNextEvictCandidateCacheAddress = M_COMRV_CALC_CACHE_ADDR_IN_BYTES_FROM_ENTRY(ucEvictCandidateList[ucEntryIndex]);
               /* perform code copy - from neighbour cache entry (pAddress) to current evict cache entry */
               comrvMemcpyHook(pDestinationAddress, pAddress, pNextEvictCandidateCacheAddress - pAddress);
               /* after code copy we need to align the entries structures */
               do
               {
                  /* remember entire entry */
                  stCacheEntry = g_stComrvCB.stOverlayCache[ucIndex];
#ifdef D_COMRV_OVL_DATA_SUPPORT
                  /* the entry is locked (data overlay) - can't handle de-frag */
                  if (M_COMRV_BUILTIN_EXPECT(stCacheEntry.unProperties.stFields.usEntryLock, 0))
                  {
                     M_COMRV_EXIT_CRITICAL_SECTION();
                     M_COMRV_ERROR(stErrArgs, D_COMRV_OVL_DATA_DEFRAG_ERR, unToken.uiValue);
                  }
#endif /* D_COMRV_OVL_DATA_SUPPORT */
                  /* replace ucIndex entry with ucNeighbourIndex entry */
                  g_stComrvCB.stOverlayCache[ucIndex] = g_stComrvCB.stOverlayCache[ucNeighbourIndex];
                  /* get new neighbour's index - may have changed */
                  ucNeighbourIndex = ucIndex + g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usSizeInMinGroupSizeUnits;
                  /* replace ucNeighbourIndex entry with ucIndex entry */
                  g_stComrvCB.stOverlayCache[ucNeighbourIndex] = stCacheEntry;
#ifdef D_COMRV_EVICTION_LRU
                  /* update the 'prev' field of ucNeighbourIndex's 'next' */
                  g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typNextLruIndex].unLru.stFields.typPrevLruIndex = ucNeighbourIndex;
                  /* updating the lru */
                  if (g_stComrvCB.ucLruIndex == ucIndex)
                  {
                     /* update the lru index */
                     g_stComrvCB.ucLruIndex = ucNeighbourIndex;
                     /* update the 'next' field of ucIndex's 'prev' */
                     g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typPrevLruIndex].unLru.stFields.typNextLruIndex = ucIndex;
                  }
                  else
                  {
                     /* update the 'next' field of ucNeighbourIndex's 'prev' */
                     g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typPrevLruIndex].unLru.stFields.typNextLruIndex = ucNeighbourIndex;
                     /* ucNeighbourIndex is the lru (can be if the lru index is locked) */
                     if (g_stComrvCB.ucLruIndex == ucNeighbourIndex)
                     {
                        /* update the lru index */
                        g_stComrvCB.ucLruIndex = ucIndex;
                     }
                     else
                     {
                        /* update the 'next' field of ucIndex's 'prev' */
                        g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typPrevLruIndex].unLru.stFields.typNextLruIndex = ucIndex;
                     }
                  }
                  /* updating the mru */
                  if (g_stComrvCB.ucMruIndex == ucNeighbourIndex)
                  {
                     /* update the mru index */
                     g_stComrvCB.ucMruIndex = ucIndex;
                  }
                  else
                  {
                     /* update the 'prev' field of ucIndex's 'next' */
                     g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typNextLruIndex].unLru.stFields.typPrevLruIndex = ucIndex;
                  }
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
                  /* move to the next index */
                  ucIndex = ucNeighbourIndex;
                  /* get new neighbour's index */
                  ucNeighbourIndex = ucIndex + g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usSizeInMinGroupSizeUnits;
               } while (ucNeighbourIndex != ucEvictCandidateList[ucEntryIndex]);
               /* end of do while loop */
#ifdef D_COMRV_EVICTION_LRU
               /* if ucNeighbourIndex is the ucIndex's 'next' than update ucIndex's 'next' to be ucNeighbourIndex's 'next' */
               if (g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typNextLruIndex == ucNeighbourIndex)
               {
                  /* now 2 entries are merged - update cachec entry 'next' value */
                  g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typNextLruIndex = g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typNextLruIndex;
               }
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
            }
            else
            {
#ifdef D_COMRV_EVICTION_LRU
               /* if the neighbour has a next */
               if (g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typNextLruIndex != D_COMRV_MRU_ITEM)
               {
                  /* update the prev of my neighbour's next */
                  g_stComrvCB.stOverlayCache[g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typNextLruIndex].unLru.stFields.typPrevLruIndex = ucIndex;
                  /* my next is my neighbour's next */
                  g_stComrvCB.stOverlayCache[ucIndex].unLru.stFields.typNextLruIndex = g_stComrvCB.stOverlayCache[ucNeighbourIndex].unLru.stFields.typNextLruIndex;
               }
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
            } /* if (ucNeighbourIndex != ucEvictCandidateList[ucEntryIndex]) */

            /* update the next eviction candidate with the updated entry */
            ucEvictCandidateList[ucEntryIndex] = ucIndex;
            /* set the new size */
            g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usSizeInMinGroupSizeUnits += g_stComrvCB.stOverlayCache[ucNeighbourIndex].unProperties.stFields.usSizeInMinGroupSizeUnits;
         } /* end of loop -> while (ucEntryIndex < ... */
         /* increment the number of candidates so ucNumOfEvictionCandidates
            points now to the eviction entry that contains the total eviction size */
         ucNumOfEvictionCandidates++;
      } /* if (ucNumOfEvictionCandidates == 0) */
      /* at this point we will have only one entry left (w/ or w/o fragmentation) */
      ucIndex = ucEvictCandidateList[ucEntryIndex];
      /* update the cache entry with the new token */
      g_stComrvCB.stOverlayCache[ucIndex].unToken.uiValue = unToken.uiValue;
      /* update the cache entry properties with the group size */
      g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usSizeInMinGroupSizeUnits = usOverlayGroupSize;
      /* if evicted size is larger than requested size we need to update the CB remaining space */
      ucSizeOfEvictionCandidates = ucEvictCandidateList[ucNumOfEvictionCandidates] - usOverlayGroupSize;
      /* check if the evicted size was bigger than the requested size */
      if (ucSizeOfEvictionCandidates != 0)
      {
#ifdef D_COMRV_EVICTION_LRU
         /* point to the CB cache entry to be updated */
         pEntry = &g_stComrvCB.stOverlayCache[ucIndex + usOverlayGroupSize];
         /* mark the group ID so that it won't pop in the next search */
         pEntry->unToken.uiValue      = D_COMRV_ENTRY_TOKEN_INIT_VALUE;
         /* update the cache entry new size - this will also clear remaining properties */
         pEntry->unProperties.usValue = M_COMRV_CONVERT_TO_ENTRY_SIZE_FROM_VAL(ucSizeOfEvictionCandidates);
         /* update the cache entry 'prev lru' field of the previous lru */
         g_stComrvCB.stOverlayCache[g_stComrvCB.ucLruIndex].unLru.stFields.typPrevLruIndex =
               ucIndex + usOverlayGroupSize;
         /* update the cache entry 'next lru' field */
         pEntry->unLru.stFields.typNextLruIndex = g_stComrvCB.ucLruIndex;
         /* update the cache entry 'previous lru' field - now it is the first lru as
            it is now considered 'evicted/empty' */
         pEntry->unLru.stFields.typPrevLruIndex = D_COMRV_LRU_ITEM;
         /* update the global lru index */
         g_stComrvCB.ucLruIndex = ucIndex + usOverlayGroupSize;
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
      } /* if (M_COMRV_BUILTIN_EXPECT(ucNumOfEvictionCandidates == 0, 0)) */
      /* update the entry access */
      comrvUpdateCacheEvectionParams(ucIndex);
#ifdef D_COMRV_RTOS_SUPPORT
      /* mark the entry as locked - protect the soon loaded ram */
      g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usEntryLock = D_COMRV_ENTRY_LOCKED;
#endif /* D_COMRV_RTOS_SUPPORT */
#ifdef D_COMRV_OVL_DATA_SUPPORT
      /* are we loading a data overlay */
      if ((u32_t*)pReturnAddress == (u32_t*)D_COMRV_DATA_OVERLAY_IND)
      {
         /* clear pReturnAddress as as it is used later on */
         pReturnAddress = NULL;
#ifndef D_COMRV_RTOS_SUPPORT /* in rtos support, the entry is already locked*/
         /* lock the entry - we don't want it to be evicted until end user releases it */
         g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usEntryLock = D_COMRV_ENTRY_LOCKED;
#endif /* D_COMRV_RTOS_SUPPORT */
         /* mark this is a data overlay */
         g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usData = D_COMRV_TRUE;
         /* set data overlay ref count */
         g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usRefCount = 1;
      }
#endif /* D_COMRV_OVL_DATA_SUPPORT */
#ifdef D_COMRV_FW_INSTRUMENTATION
      /* update for FW profiling loaded the function */
      stInstArgs.uiInstNum = uiProfilingIndication | D_COMRV_INSTRUMENTATION_LOAD_BIT;
      stInstArgs.uiToken   = unToken.uiValue;
      /* disable the interrupts */
      M_COMRV_DISABLE_INTS(uiPrevIntState);
      /* instrumentation hook function */
      comrvInstrumentationHook(&stInstArgs);
      /* enable the interrupts */
      M_COMRV_ENABLE_INTS(uiPrevIntState);
#endif /* D_COMRV_FW_INSTRUMENTATION */
      /* the group size in bytes */
      usOverlayGroupSize = M_COMRV_GROUP_SIZE_TO_BYTES(usOverlayGroupSize);

#ifdef D_COMRV_LOAD_CONFIG_SUPPORT
      /* check if load is disabled */
      if (M_COMRV_BUILTIN_EXPECT(g_stComrvCB.ucLoadEnabled == D_COMRV_LOAD_DISABLED,0))
      {
         M_COMRV_ERROR(stErrArgs, D_COMRV_LOAD_DISABLED_ERR, unToken.uiValue);
      }
#endif /* D_COMRV_LOAD_CONFIG_SUPPORT */

      /* comrv test sync point - trigger sw int and force context switch */
      M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_SAME_OVERLAY);

      /* now we can load the overlay group */
      stLoadArgs.uiSizeInBytes = usOverlayGroupSize;
      stLoadArgs.pDest         = M_COMRV_CALC_CACHE_ADDR_IN_BYTES_FROM_ENTRY(ucIndex);
      stLoadArgs.uiGroupOffset = M_COMRV_GET_GROUP_OFFSET_IN_BYTES(g_stComrvCB.stOverlayCache[ucIndex].unToken);
      pAddress = comrvLoadOvlayGroupHook(&stLoadArgs);

      /* if group wasn't loaded */
      if (M_COMRV_BUILTIN_EXPECT(pAddress == 0,0))
      {
         M_COMRV_ERROR(stErrArgs, D_COMRV_LOAD_ERR, unToken.uiValue);
      }

      M_COMRV_VERIFY_CRC(pAddress, usOverlayGroupSize-sizeof(u32_t),
                        *((u32_t*)(pAddress + (usOverlayGroupSize-sizeof(u32_t)))));

      /*
       The following 3 lines were moved from (before):
       "usOverlayGroupSize = M_COMRV_GROUP_SIZE_TO_BYTES(usOverlayGroupSize);"
       This was done to avoid the case where a low priority task is in the load process
       and a high priority task is calling a function from the same group loaded by the
       low priority task.
       This change may delay the execution of the high priority task until the low priority
       task completes the load; this delay can be resolved by moving back the 3 lones and adding a 32bit
       field to each cache entry to hold a mutex address. when the low priority task gets to the load it
       shall allocate a mutex from a pre allocated mutex poll and set its address in the new 32bit cache
       entry field; once the high priority task gets to the search and finds the group it shall try to
       get the mutex - this will block the high priority task until the low priority task completes the load
       */
      /* comrv test sync point - trigger sw int and force context switch */
      M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_BEFORE_EXIT_CRITICAL_SEC);
      /* it is safe now to get new overlay requests */
      M_COMRV_EXIT_CRITICAL_SECTION();
      /* comrv test sync point - trigger sw int and force context switch */
      M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_AFTER_EXIT_CRITICAL_SEC);
      /* at this point we are sure comrv data is valid - debugger can now collect it */
      M_COMRV_DEBUGGER_HOOK_SYMBOL();

#ifdef D_COMRV_RTOS_SUPPORT
#ifdef D_COMRV_OVL_DATA_SUPPORT
      /* if overlay data - no need to unlock */
      if (g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usData == D_COMRV_FALSE)
      {
         /* TODO: do we need the if over all disable/enable */
#endif /* D_COMRV_OVL_DATA_SUPPORT */
         /* read comrv status from task stack register */
         M_COMRV_TASK_STACK_REG(uiTemp);
         /* we want to safely clear the lock indication so disable interrupts */
         M_COMRV_DISABLE_INTS(uiPrevIntState);
         /* mark the entry as unlocked - can now be evicted/moved */
         g_stComrvCB.stOverlayCache[ucIndex].unProperties.stFields.usEntryLock = D_COMRV_ENTRY_UNLOCKED;
         /* mark comrv state - 'post search and load' */
         ((u32_t*)uiTemp)[D_COMRV_STATE_STACK_OFFSET_TMP] |= D_COMRV_POST_SEARCH_LOAD;
         /* re enable interrupts */
         M_COMRV_ENABLE_INTS(uiPrevIntState);
#ifdef D_COMRV_OVL_DATA_SUPPORT
      }
#endif /* D_COMRV_OVL_DATA_SUPPORT */
#endif /* D_COMRV_RTOS_SUPPORT */
   } /* overlay group is already loaded */
   else
   {
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      /* get the already loaded overlay token */
      if (unToken.stFields.uiMultiGroup)
      {
         unToken = pMultigroup[tSelectedMultiGroupEntry];;
      }
      /* get the group size */
      usOverlayGroupSize = M_COMRV_GET_OVL_GROUP_SIZE(unToken);
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
      /* update the entry access */
      comrvUpdateCacheEvectionParams(usSearchResultIndex);
#ifdef D_COMRV_RTOS_SUPPORT
#ifdef D_COMRV_OVL_DATA_SUPPORT
      /* if non overlay data - mark post search load */
      if (g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usData == D_COMRV_FALSE)
      {
#endif /* D_COMRV_OVL_DATA_SUPPORT */
         /* read comrv status */
         M_COMRV_TASK_STACK_REG(uiTemp);
        /* mark comrv state - 'post search and load' */
        ((u32_t*)uiTemp)[D_COMRV_STATE_STACK_OFFSET_TMP] |= D_COMRV_POST_SEARCH_LOAD;
#ifdef D_COMRV_OVL_DATA_SUPPORT
      }
#endif /* D_COMRV_OVL_DATA_SUPPORT */
#endif /* D_COMRV_RTOS_SUPPORT */
#ifdef D_COMRV_FW_INSTRUMENTATION
      stInstArgs.uiInstNum  = uiProfilingIndication;
      stInstArgs.uiToken    = unToken.uiValue;
      /* disable the interrupts */
      M_COMRV_DISABLE_INTS(uiPrevIntState);
      /* instrumentation hook function */
      comrvInstrumentationHook(&stInstArgs);
      /* enable the interrupts */
      M_COMRV_ENABLE_INTS(uiPrevIntState);
#endif /* D_COMRV_FW_INSTRUMENTATION */
      /* it is safe now to get new overlay requests */
      M_COMRV_EXIT_CRITICAL_SECTION();
      /* get the loaded address */
      pAddress           = M_COMRV_CALC_CACHE_ADDR_IN_BYTES_FROM_ENTRY(usSearchResultIndex);
      /* the group size in bytes */
      usOverlayGroupSize = M_COMRV_GROUP_SIZE_TO_BYTES(usOverlayGroupSize);
#ifdef D_COMRV_OVL_DATA_SUPPORT
      /* data overlay */
      if ((u32_t*)pReturnAddress == (u32_t*)D_COMRV_DATA_OVERLAY_IND)
      {
         /* clear pReturnAddress as it is used later on */
         pReturnAddress = NULL;
         /* if data overlay ref count still valid */
         if (g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usRefCount != D_COMRV_MAX_REF_COUNT)
         {
            /* increment data overlay reference counter */
            g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usRefCount++;
         }
         else
         {
            /* notify error we reached the max value */
            M_COMRV_ERROR(stErrArgs, D_COMRV_DATA_OVL_MAX_COUNT, unToken.uiValue);
         }
      }
#endif /* D_COMRV_OVL_DATA_SUPPORT */
   } /* if (usSearchResultIndex == D_COMRV_GROUP_NOT_FOUND) */

   /* comrv test sync point - trigger sw int and force context switch */
   M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_AFTER_SEARCH_LOAD);

   /* comrv test sync point - trigger sw int and force context switch */
   M_CTI_SYNC_POINT(D_CTI_TASK_SYNC_DATA_OVERLAY);

   /* invalidate data cache */
   M_COMRV_DCACHE_FLUSH(&g_stComrvCB, sizeof(g_stComrvCB));

   /* get actual function/data offset */
   usOffset = M_COMRV_GET_TOKEN_OFFSET_IN_BYTES(unToken);

   /* are we returning to an overlay function */
   if (ucIsInvoke == D_COMRV_RET_CALLER_BIT_0)
   {
      /* calculate the function return offset; at this point
         pComrvStackFrame->uiCalleeToken holds the return address */
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      /* In multi group token we need to get the offset from previously loaded token */
      if (pComrvStackFrame->tCalleeMultiGroupTableEntry != (multigroupEntryIndex_t)D_COMRV_EMPTY_CALLEE_MULTIGROUP)
      {
         /* we now are at the point of loading a multi-group token so we need to take the
            previous token for which the return address refers to */
         unToken = pMultigroup[pComrvStackFrame->tCalleeMultiGroupTableEntry];
         /* get the offset */
         uiTemp = M_COMRV_GET_TOKEN_OFFSET_IN_BYTES(unToken);
         /* calculate the actual return offset */
         usOffset += M_COMRV_EXTRACT_RETURN_OFFSET(pReturnAddress, uiTemp);
         /* clear the save multi token so it won't be reused */
         pComrvStackFrame->tCalleeMultiGroupTableEntry = (multigroupEntryIndex_t)D_COMRV_EMPTY_CALLEE_MULTIGROUP;
      }
      else
      {
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
         /* calculate the actual return offset; we pass 0 since the ra already contains
            the function offset */
         usOffset = M_COMRV_EXTRACT_RETURN_OFFSET(pReturnAddress, 0);
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
      }
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
   }
   /* we are calling a new overlay function or accessing overlay data */
   else
   {
#ifdef D_COMRV_MULTI_GROUP_SUPPORT
     /* Update comrv stack frame with the loaded multi group table entry.
        It is used to calculate the actual return offset in case we
        are returning to a multi group token */
      pComrvStackFrame->tCalleeMultiGroupTableEntry = tSelectedMultiGroupEntry;
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */
   }

   /* save the alignment value - we need to update it also when returning to the caller as it may
      have been evicted and the load address has changed */
   pComrvStackFrame->ucAlignmentToMaxGroupSize = ((u32_t)pAddress & (D_COMRV_OVL_GROUP_SIZE_MAX-1)) >> D_COMRV_GRP_SIZE_IN_BYTES_SHIFT_AMNT;

/* if we allow calls to non comrv engin functions (e.g. instrumentation) in rtos support,
   we need to be ready for cases we context switch during such calls */
#if defined(D_COMRV_RTOS_SUPPORT) && defined(D_COMRV_ALLOW_CALLS_AFTER_SEARCH_LOAD)
   /*disable the interrupts while we read comrv status */
   M_COMRV_DISABLE_INTS(uiPrevIntState);
   /* read comrv task stack register */
   M_COMRV_TASK_STACK_REG(uiTemp);
   /* check if state is - 'interrupted' */
   if (((u32_t*)uiTemp)[D_COMRV_STATE_STACK_OFFSET_TMP] & D_COMRV_POST_SEARCH_LOAD_INTERRUPTED)
   {
      /* clear the D_COMRV_POST_SEARCH_LOAD, D_COMRV_POST_SEARCH_LOAD_INTERRUPTED indications */
      ((u32_t*)uiTemp)[D_COMRV_STATE_STACK_OFFSET_TMP] ^= (D_COMRV_POST_SEARCH_LOAD | D_COMRV_POST_SEARCH_LOAD_INTERRUPTED);
      /* determine the engines return point */
      if (((u32_t*)uiTemp)[D_COMRV_STATE_STACK_OFFSET_TMP] == D_COMRV_STATE_ENTRY)
      {
         pAddress = &comrvEntry_context_switch;
      }
      /* interrupted after exit */
      else
      {
         pAddress = &comrv_ret_from_callee_context_switch;
      }
      /* prepare returning to the comrv asm engine */
      comrv_pre_ret_to_asm_engine(uiPrevIntState, pAddress);
      /* comrv_pre_ret_to_asm_engine will never return to this point
         and will enable the interrupts */
   }
   /* re enable interrupts */
   M_COMRV_ENABLE_INTS(uiPrevIntState);
#endif /* D_COMRV_RTOS_SUPPORT && D_COMRV_ALLOW_CALLS_AFTER_SEARCH_LOAD*/

   /* group is now loaded to memory so we can return the address of the data/function */
   return ((u08_t*)pAddress + usOffset);
}

/**
* @brief Get comrv cache eviction candidates according to a given size
*
* @param requestedEvictionSize - size requested for eviction; expressed in
*                                units of D_COMRV_OVL_GROUP_SIZE_MIN
*        pEvictCandidatesList  - output eviction candidate list of comrv cache indexes
* @return number of eviction candidates in the output list
*/
D_COMRV_TEXT_SECTION
u08_t comrvGetEvictionCandidates(u08_t ucRequestedEvictionSize, u08_t* pEvictCandidatesList)
{
   comrvCacheEntry_t  *pCacheEntry;
   u32_t               uiCandidates;
   u08_t               ucAccumulatedSize = 0, ucIndex = 0;
   u08_t               ucEntryIndex, ucNumberOfCandidates = 0;
   u32_t               uiEvictCandidateMap[D_COMRV_EVICT_CANDIDATE_MAP_SIZE], uiFindFirstSet;
#if defined(D_COMRV_ASSERT_ENABLED) && defined(D_COMRV_ERROR_NOTIFICATIONS)
   comrvErrorArgs_t   stErrArgs;
#endif /* D_COMRV_ASSERT_ENABLED && D_COMRV_ERROR_NOTIFICATIONS */
#ifndef D_BITMANIP_EXT
   /* used for calculating the first set bit index */
   unsigned char ucArrDeBruijnBitPos[32] =
   {
     0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
     31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
   };
#endif /* D_BITMANIP_EXT */

   /* first lets clear the uiCandidates list */
   comrvMemset(uiEvictCandidateMap, 0, D_COMRV_EVICT_CANDIDATE_MAP_SIZE);

#ifdef D_COMRV_EVICTION_LRU
   /* get the first lru entry */
   ucEntryIndex = g_stComrvCB.ucLruIndex;
   /* loop LRU list until the eviction uiCandidates accumulated size
      reaches the requested eviction size */
   do
   {
      /* point to the cache entry CB */
      pCacheEntry = &g_stComrvCB.stOverlayCache[ucEntryIndex];
      /* verify the entry isn't locked */
      if ((pCacheEntry->unProperties.usValue & D_COMRV_ANY_LOCK_MASK) == 0)
      {
         /* count the number of uiCandidates */
         ucNumberOfCandidates++;
         /* accumulate size */
         ucAccumulatedSize += pCacheEntry->unProperties.stFields.usSizeInMinGroupSizeUnits;
         /* set the eviction candidate in the eviction map */
         uiEvictCandidateMap[ucEntryIndex/D_COMRV_DWORD_IN_BITS] |= (1 << ucEntryIndex);
      }
      /* move to the next LRU candidate */
      ucEntryIndex = pCacheEntry->unLru.stFields.typNextLruIndex;
   /* loop as long as we didn't get to the requested eviction size or we reached end of the list
      (means that all entries are locked) */
   } while (ucAccumulatedSize < ucRequestedEvictionSize && ucEntryIndex != D_COMRV_MRU_ITEM);

#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */

   /* make sure we don't have more than the maximum available entries
      to accommodate one group */
   M_COMRV_ASSERT((ucNumberOfCandidates > (D_COMRV_OVL_GROUP_SIZE_MAX/D_COMRV_OVL_GROUP_SIZE_MIN)),
                  0, D_COMRV_INTERNAL_ERR, D_COMRV_INVALID_TOKEN);

   /* now we have eviction uiCandidates bitmap of cache entries - lets create
      an output sorted list of these entries */
   for (ucEntryIndex = 0 ; ucIndex != ucNumberOfCandidates && ucEntryIndex < D_COMRV_EVICT_CANDIDATE_MAP_SIZE ; ucEntryIndex++)
   {
      /* get the candidates */
      uiCandidates = uiEvictCandidateMap[ucEntryIndex];
      /* convert each candidate to an actual value in pEvictCandidatesList */
      while (uiCandidates)
      {
         /* get the lsb that is set */
         uiFindFirstSet = uiCandidates & (-uiCandidates);
         /* subtract the lsb that is set */
         uiCandidates -= uiFindFirstSet;
         /* get the bit position */
         pEvictCandidatesList[ucIndex] = M_COMRV_GET_SET_BIT_INDEX(uiFindFirstSet);
         /* add the location of the bit - pEvictCandidatesList[ucIndex] will hold the group number */
         pEvictCandidatesList[ucIndex] += ucEntryIndex*D_COMRV_DWORD_IN_BITS;
         /* move to the next entry in pEvictCandidatesList */
         ucIndex++;
      }
   }

   /* at this point we may have 0, some or all needed memory
      if we don't have all memory we mark as if we have no
      memory but will return the available size if applicable */
   if (ucAccumulatedSize < ucRequestedEvictionSize)
   {
      ucNumberOfCandidates = 0;
   }

   /* set the total size of eviction candidates in the last entry */
   pEvictCandidatesList[ucNumberOfCandidates] = ucAccumulatedSize;

   return ucNumberOfCandidates;
}

/**
* @brief search if a specific token is already loaded to the cache
*
* @param token - the token to search for
* @return if the token is loaded the return value is set to the loaded address
*         otherwise NULL
*/
D_COMRV_TEXT_SECTION static u16_t comrvSearchForLoadedOverlayGroup(comrvOverlayToken_t unToken)
{
   u08_t              ucEntryIndex;
   comrvCacheEntry_t *pCacheEntry;

   /* loop all entries excluding the last entry which holds the comrv tables */
   for (ucEntryIndex = 0 ; ucEntryIndex < g_stComrvCB.ucLastCacheEntry ; ucEntryIndex+=pCacheEntry->unProperties.stFields.usSizeInMinGroupSizeUnits)
   {
      pCacheEntry = &g_stComrvCB.stOverlayCache[ucEntryIndex];
      /* if token already loaded */
      if (pCacheEntry->unToken.stFields.uiOverlayGroupID == unToken.stFields.uiOverlayGroupID)
      {
         /* return the actual cache entry of the loaded overlay group */
         return ucEntryIndex;
      }
   }

   /* overlay group not loaded */
   return D_COMRV_GROUP_NOT_FOUND;
}

/**
* @brief Update a given comrv cache entry was accessed
*
* @param entryIndex - the comrv cache entry being accessed
*
* @return none
*/

D_COMRV_TEXT_SECTION static void comrvUpdateCacheEvectionParams(u08_t ucEntryIndex)
{
   comrvCacheEntry_t *pCacheEntry;

#ifdef D_COMRV_EVICTION_LRU
   /* the '+ D_COMRV_OVL_GROUP_SIZE_MIN' is for the tables */
#if (D_COMRV_OVL_CACHE_SIZE_IN_BYTES <= D_COMRV_MAX_GROUP_SIZE_IN_BYTES+D_COMRV_OVL_GROUP_SIZE_MIN)
   /* this code is for a corner case where the cache size is smaller then
      the maximum group size; this mean we may reach to a point that the
      entire cache is occupied with a single overlay group */
      /* is one group loaded and occupies the entire cache */
   if (g_stComrvCB.stOverlayCache[ucEntryIndex].unProperties.stFields.usSizeInMinGroupSizeUnits == g_stComrvCB.ucLastCacheEntry)
   {
      /* both lru and mru globals point to the same index */
      g_stComrvCB.ucMruIndex = ucEntryIndex;
      g_stComrvCB.ucLruIndex = ucEntryIndex;
      /* mark both LRU/MRU of the entry as the last */
      g_stComrvCB.stOverlayCache[ucEntryIndex].unLru.typValue = (u16_t)((D_COMRV_MRU_ITEM << 8) | D_COMRV_MRU_ITEM);
   }
   else
#endif /* #if (D_COMRV_OVL_CACHE_SIZE_IN_BYTES <= D_COMRV_MAX_GROUP_SIZE_IN_BYTES) */
   /* there is no need to update if ucEntryIndex is already MRU */
   if (ucEntryIndex !=  g_stComrvCB.ucMruIndex)
   {
      pCacheEntry = &g_stComrvCB.stOverlayCache[ucEntryIndex];
      /* ucEntryIndex is not the ucLruIndex */
      if (ucEntryIndex !=  g_stComrvCB.ucLruIndex)
      {
         /* update previous item's 'next index' */
         g_stComrvCB.stOverlayCache[pCacheEntry->unLru.stFields.typPrevLruIndex].unLru.stFields.typNextLruIndex =
               pCacheEntry->unLru.stFields.typNextLruIndex;
      }
      else
      {
         /* update the global lru index */
         g_stComrvCB.ucLruIndex = pCacheEntry->unLru.stFields.typNextLruIndex;
         /* update the lru item with the previous item index */
         g_stComrvCB.stOverlayCache[g_stComrvCB.ucLruIndex].unLru.stFields.typPrevLruIndex = D_COMRV_LRU_ITEM;
      }
      /* update the prev index of next item index */
      g_stComrvCB.stOverlayCache[pCacheEntry->unLru.stFields.typNextLruIndex].unLru.stFields.typPrevLruIndex = pCacheEntry->unLru.stFields.typPrevLruIndex;
      /* update ucEntryIndex previous index */
      pCacheEntry->unLru.stFields.typPrevLruIndex = g_stComrvCB.ucMruIndex;
      /* update ucEntryIndex next index - last item (MRU)*/
      pCacheEntry->unLru.stFields.typNextLruIndex = D_COMRV_MRU_ITEM;
      /* update the old mru's next index */
      g_stComrvCB.stOverlayCache[g_stComrvCB.ucMruIndex].unLru.stFields.typNextLruIndex = ucEntryIndex;
      /* update the new MRU */
      g_stComrvCB.ucMruIndex = ucEntryIndex;
   }
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
}

/**
* @brief get comrv status
*
* @param pComrvStatus - function output
*
* @return none
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvGetStatus(comrvStatus_t* pComrvStatus)
{
   pComrvStatus->pComrvCB    = &g_stComrvCB;
   pComrvStatus->pComrvStack = g_stComrvStackPool;
}

/**
* @brief initialize comrv stack - needs to be invoke by each task (if rtos exist)
* when before initializing task stack.
* in bare-metal apps, this function is called by comrv initialization function
* and the user application doesn't need to do that.
*f
* @param none
*
* @return D_COMRV_NOT_INITIALIZED_ERR comrv engine not initialized (in rtos support)
*         D_COMRV_SUCCESS - comrv stack initialized
*/
D_COMRV_NO_INLINE D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
u32_t comrvInitApplicationStack(void)
{
#ifdef D_COMRV_RTOS_SUPPORT
   u32_t uiT6reg;
#endif /* D_COMRV_RTOS_SUPPORT */
   u32_t uiOutPrevIntState;
   comrvStackFrame_t *pStackPool, *pStackFrame;

   /* clear comrv state */
   comrvClearComrvState();

#ifdef D_COMRV_RTOS_SUPPORT
   /* read comrv entry address */
   M_COMRV_READ_ENTRY_ADDR(uiT6reg);
   /* in comrvInit() t6 can be set to point to comrvEntryDisable() or comrvEntry() depending if the
      end user requested to load comrv tables when comrvInit() was invoked:
      if end user requested to load the tables, t6 will point to comrvEntryDisable()
      if end user didn't request to load the tables, t6 will point to comrvEntry()
      So this is why I need to check ```if```` the address in t6 isn't set to one of these functions */
   if (uiT6reg != (u32_t)comrvEntry && uiT6reg != (u32_t)comrvEntryDisable)
   {
      /* t6 to point to comrvEntryNotInitialized */
      M_COMRV_SET_ENTRY_ADDR(comrvEntryNotInitialized);
      return D_COMRV_NOT_INITIALIZED_ERR;
   }
#endif /* D_COMRV_RTOS_SUPPORT */

   /* disable ints */
   M_COMRV_DISABLE_INTS(uiOutPrevIntState);
   /* read stack pool register (t4) */
   M_COMRV_READ_POOL_REG(pStackPool);
   /* save the address of the next available stack frame */
   pStackFrame = pStackPool;
   /* update the next stack pool address */
   pStackPool = (comrvStackFrame_t*)((u08_t*)pStackPool + pStackPool->ssOffsetPrevFrame);
   /* write the new stack pool address */
   M_COMRV_WRITE_POOL_REG(pStackPool);
   /* enable ints */
   M_COMRV_ENABLE_INTS(uiOutPrevIntState);
   /* set the address of COMRV stack in t3 */
   M_COMRV_WRITE_STACK_REG(pStackFrame);
   /* mark the last stack frame */
   pStackFrame->ssOffsetPrevFrame = D_COMRV_END_OF_STACK;
   /* clear token field - bit 0 must be 0 to indicate we came
      from non overlay function */
   pStackFrame->uiCalleeToken = 0;

   return D_COMRV_SUCCESS;
}

/**
* @brief comrv version of memset - set dwords only
*
* @param pMemory - address of the memory to be initialized
*        siVal   - pattern to initialize
*        uiSizeInDwords - number of dword to initialize
*
* @return address of the initialized memory
*/
D_COMRV_TEXT_SECTION
void* comrvMemset(void* pMemory, s32_t siVal, u32_t uiSizeInDwords)
{
   u32_t uiIndex;

   /* memory initialization loop */
   for (uiIndex = 0 ; uiIndex < uiSizeInDwords ; uiIndex++)
   {
      *((u32_t*)pMemory + uiIndex) = siVal;
   }

   return pMemory;
}

/**
* @brief load offset and multigroup tables
*
* @param None
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvLoadTables(void)
{
   void                *pAddress;
   comrvLoadArgs_t      stLoadArgs;
   u08_t                ucNumOfCacheEntriesToAllocateForTables;
#ifdef D_COMRV_ERROR_NOTIFICATIONS
   comrvErrorArgs_t     stErrArgs;
#endif /* D_COMRV_ERROR_NOTIFICATIONS */
#ifdef D_COMRV_CRC
   comrvOverlayToken_t  unToken;
#endif /* D_COMRV_CRC */

   /* clear comrv state */
   comrvClearComrvState();

   /* at this point comrv cache is empty so we take the
      first entry(s) and use it to store the multigroup and
      offset tables */
   ucNumOfCacheEntriesToAllocateForTables = (u08_t)(((D_COMRV_TABLES_TOTAL_SIZE_IN_BYTES + (D_COMRV_OVL_GROUP_SIZE_MIN - 1)) & -D_COMRV_OVL_GROUP_SIZE_MIN)/D_COMRV_OVL_GROUP_SIZE_MIN);
   /* calculate the last cache entry index */
   g_stComrvCB.ucLastCacheEntry = D_COMRV_NUM_OF_CACHE_ENTRIES - ucNumOfCacheEntriesToAllocateForTables;
   /* tables are located at offset 0 (first group) */
   stLoadArgs.uiGroupOffset = D_COMRV_TABLES_OFFSET;
   /* set the load group size */
   stLoadArgs.uiSizeInBytes = ucNumOfCacheEntriesToAllocateForTables * (D_COMRV_OVL_GROUP_SIZE_MIN);
   /* load address is the last comrv cache entry */
   stLoadArgs.pDest         = pOverlayOffsetTable;
   /* load the tables */
   pAddress = comrvLoadOvlayGroupHook(&stLoadArgs);
   /* if group wasn't loaded */
   if (M_COMRV_BUILTIN_EXPECT(pAddress == 0,0))
   {
      M_COMRV_ERROR(stErrArgs, D_COMRV_TBL_LOAD_ERR, D_COMRV_TABLES_TOKEN);
   }

   /* check tables CRC */
   M_COMRV_VERIFY_TABLES_CRC(pAddress, stLoadArgs.uiSizeInBytes);

#ifdef D_COMRV_MULTI_GROUP_SUPPORT
   /* calculate the offset to the multi group table */
   g_stComrvCB.ucMultiGroupOffset = ((u16_t*)&__OVERLAY_MULTIGROUP_TABLE_START - (u16_t*)&__OVERLAY_GROUP_TABLE_START);
#ifdef D_COMRV_CRC
   /* we need to clear the CRC in case it follows the last multigroup entry -
      when scanning for multigroup tokens, the loop termination is with 0 signaling
      last token in the multigroup */
   *((u32_t*)(pAddress + (stLoadArgs.uiSizeInBytes-sizeof(u32_t)))) = 0;
#endif /* D_COMRV_CRC */
#endif /* D_COMRV_MULTI_GROUP_SUPPORT */

   /* set cache entry token and properties */
   g_stComrvCB.stOverlayCache[g_stComrvCB.ucLastCacheEntry].unToken.uiValue = D_COMRV_TABLES_TOKEN;
   g_stComrvCB.stOverlayCache[g_stComrvCB.ucLastCacheEntry].unProperties.stFields.usEntryLock = D_COMRV_ENTRY_LOCKED;
   /* we set the size 0 so that debugger will not continue scanning the cache entries */
   g_stComrvCB.stOverlayCache[g_stComrvCB.ucLastCacheEntry].unProperties.stFields.usSizeInMinGroupSizeUnits = 0;
#ifdef D_COMRV_EVICTION_LRU
   /* mark the last entry in the LRU list */
   g_stComrvCB.stOverlayCache[g_stComrvCB.ucLastCacheEntry-1].unLru.stFields.typNextLruIndex = D_COMRV_MRU_ITEM;
   /* set the index of the list MRU */
   g_stComrvCB.ucMruIndex = g_stComrvCB.ucLastCacheEntry-1;
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
   /* set the address of COMRV entry point in register t6 -
      from this point end user can call overlay functions/load overlay data */
   M_COMRV_SET_ENTRY_ADDR(comrvEntry);
   /* invalidate data cache */
   M_COMRV_DCACHE_FLUSH(&g_stComrvCB, sizeof(g_stComrvCB));
}

/**
* @brief lock/unlock a specific overlay group
*
* @param pFuncAddress - overlay function its group shall be locked/unlocked
*
* @return D_COMRV_SUCCESS - lock/unlock operation succeeded;
*         D_COMRV_LOCK_UNLOCK_ERR - lock/unlock operation failed - group not loaded
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
u32_t comrvLockUnlockOverlayGroupByFunction(const void* pOvlFuncAddress, comrvLockState_t eLockState)
{
   u16_t usSearchResultIndex;
   comrvOverlayToken_t unToken;
#ifdef D_COMRV_RTOS_SUPPORT
   u32_t              ret;
#if defined(D_COMRV_ERROR_NOTIFICATIONS) && defined(D_COMRV_ASSERT_ENABLED)
   comrvErrorArgs_t   stErrArgs;
#endif /* defined(D_COMRV_ERROR_NOTIFICATIONS) && defined(D_COMRV_ASSERT_ENABLED) */
#endif /* D_COMRV_RTOS_SUPPORT */
#ifdef D_COMRV_OVL_DATA_SUPPORT
   comrvCacheEntry_t* pComrvCacheEntry;
#endif /* D_COMRV_OVL_DATA_SUPPORT */

   /* clear comrv state */
   comrvClearComrvState();

#ifdef D_COMRV_OVL_DATA_SUPPORT
   /* data overlay: pOvlFuncAddress is an actual token
      func overlay: pOvlFuncAddress is an actual thunk address */
   if (((u32_t)pOvlFuncAddress & D_COMRV_ADDRESS_TOKEN) != D_COMRV_ADDRESS_TOKEN)
   {
#endif /* D_COMRV_OVL_DATA_SUPPORT */
      /* Lets read the token from the given address (address is a thunk).
         The first instruction is lui so we need to decode the upper 20
         bits of the instruction */
      unToken.uiValue = *((u32_t*)pOvlFuncAddress) & D_COMRV_LUI_TOKEN_20_BITS_MASK;
      /* next instruction we decode is the addi - take the upper 12 bits */
      unToken.uiValue |= ((*(((u32_t*)pOvlFuncAddress+1)) & D_COMRV_ADDI_TOKEN_12_BITS_MASK) >> D_COMRV_ADDI_TOKEN_SHMT);
#ifdef D_COMRV_OVL_DATA_SUPPORT
   }
   else
   {
      unToken.uiValue = (u32_t)pOvlFuncAddress;
   }
#endif /* D_COMRV_OVL_DATA_SUPPORT */

   /* enter critical section */
   M_COMRV_ENTER_CRITICAL_SECTION();

   /* now search for the group */
   usSearchResultIndex = comrvSearchForLoadedOverlayGroup(unToken);
   /* check if the group isn't loaded */
   if (M_COMRV_BUILTIN_EXPECT(usSearchResultIndex == D_COMRV_GROUP_NOT_FOUND,0))
   {
      /* exit critical section */
      M_COMRV_EXIT_CRITICAL_SECTION();
      /* we can't lock an unloaded group */
      return D_COMRV_LOCK_UNLOCK_ERR;
   }

#ifdef D_COMRV_OVL_DATA_SUPPORT
   /* unlock data overlay? */
   if (g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usData == D_COMRV_TRUE)
   {
      if (eLockState == D_COMRV_GROUP_STATE_UNLOCK)
      {
         /* decrement the 'data overlay' lock counter */
         g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usRefCount--;
         /* is the lock counter 0 - we can release the lock */
         if (g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usRefCount == 0)
         {
            pComrvCacheEntry = &g_stComrvCB.stOverlayCache[usSearchResultIndex];
            /* unlock the entry and clear data property */
            pComrvCacheEntry->unProperties.stFields.usEntryLock = D_COMRV_ENTRY_UNLOCKED;
            pComrvCacheEntry->unProperties.stFields.usData = D_COMRV_FALSE;
            /* this group can now be marked as less hot so it will be evicted first */
#ifdef D_COMRV_EVICTION_LRU
            /* if not already the LRU */
            if (pComrvCacheEntry->unLru.stFields.typPrevLruIndex != D_COMRV_LRU_ITEM)
            {
               /* update the 'next' of my 'prev' - will get my 'next' */
               g_stComrvCB.stOverlayCache[pComrvCacheEntry->unLru.stFields.typPrevLruIndex].unLru.stFields.typNextLruIndex = pComrvCacheEntry->unLru.stFields.typNextLruIndex;
               /* if I have a next */
               if (pComrvCacheEntry->unLru.stFields.typNextLruIndex != D_COMRV_MRU_ITEM)
               {
                  /* update the 'prev' of my 'next' - will be my 'prev'*/
                  g_stComrvCB.stOverlayCache[pComrvCacheEntry->unLru.stFields.typNextLruIndex].unLru.stFields.typPrevLruIndex = pComrvCacheEntry->unLru.stFields.typPrevLruIndex;
               }
               /* I don't have a 'next' so I'm the MRU */
               else
               {
                  /* update the MRU to be my 'prev' */
                  g_stComrvCB.ucMruIndex = pComrvCacheEntry->unLru.stFields.typPrevLruIndex;
               }
               /* update my 'next' - will be the current LRU */
               pComrvCacheEntry->unLru.stFields.typNextLruIndex = g_stComrvCB.ucLruIndex;
               /* update my 'prev' - will be the LRU */
               pComrvCacheEntry->unLru.stFields.typPrevLruIndex = D_COMRV_LRU_ITEM;
               /* update current LRU 'prev' - will me */
               g_stComrvCB.stOverlayCache[g_stComrvCB.ucLruIndex].unLru.stFields.typPrevLruIndex = usSearchResultIndex;
               /* update the new LRU */
               g_stComrvCB.ucLruIndex = usSearchResultIndex;
            }
#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */
         }
      }
   }
   else
   {
#endif /* D_COMRV_OVL_DATA_SUPPORT */
      /* group is loaded so lets lock/unlock it */
      g_stComrvCB.stOverlayCache[usSearchResultIndex].unProperties.stFields.usEvictLock = eLockState;
#ifdef D_COMRV_OVL_DATA_SUPPORT
   }
#endif /* D_COMRV_OVL_DATA_SUPPORT */

   /* exit critical section */
   M_COMRV_EXIT_CRITICAL_SECTION();

   /* lock/unlock was successful */
   return D_COMRV_SUCCESS;
}

#ifdef D_COMRV_CONTROL_SUPPORT
/**
* @brief Enable the ability to call/load overlay functions/data
* This function has no influence if invoked before comrvInit()
*
* @param none
*
* @return none
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvEnable(void)
{
   /* clear comrv state */
   comrvClearComrvState();
   M_COMRV_SET_ENTRY_ADDR(comrvEntry);
}

/**
* @brief Disable the ability to call/load overlay functions/data
* This function can be invoked before comrv has been
* initialized with comrvInit() - in such a case, overlay functions/data
* shall not be called/loaded after comrvInit() until comrvEnable() is called
*
* @param none
*
* @return none
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvDisable(void)
{
   /* clear comrv state */
   comrvClearComrvState();
   M_COMRV_SET_ENTRY_ADDR(comrvEntryDisable);
}

#endif /* D_COMRV_CONTROL_SUPPORT */

/**
* @brief This function is invoked by the comev engine in case an overlay function
* was invoked and comrv is disabled
*
* @param None
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvNotifyDisabledError(void)
{
#ifdef D_COMRV_ERROR_NOTIFICATIONS
   comrvErrorArgs_t stErrArgs;
#endif /* D_COMRV_ERROR_NOTIFICATIONS */
   M_COMRV_ERROR(stErrArgs, D_COMRV_INVOKED_WHILE_DISABLED, D_COMRV_INVALID_TOKEN);
}

/**
* @brief retrieve comrv database address
*
* @param None
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
const comrvCB_t* comrvGetDatabase(void)
{
   return &g_stComrvCB;
}

#ifdef D_COMRV_RTOS_SUPPORT

/**
* @brief save comrv stack in case of context switch
*
* @param None
*
* @return None
*/
/* TODO: Is it a good idea to have comrvSaveContextSwitch in D_COMRV_TEXT_SECTION */
D_COMRV_TEXT_SECTION u32_t* comrvSaveContextSwitch(volatile u32_t* pMepc, volatile u32_t* pRegisterT3,
                                                   comrvTaskStackRegsVal_t** pComrvTaskStackRegsVal)
{
   u32_t             *pSpAddr = NULL;
   u32_t             *pComrvTaskStack, *pComrvStatus;
   comrvStackFrame_t *pStackPool, *pStackFrame, *pAppStack;

   /* check if mepc is in range of the overlay cache - means that interruption
      occurred during execution of an overlay function */
   if ((*pMepc - (u32_t)&__OVERLAY_CACHE_START__) < D_COMRV_OVL_CACHE_SIZE_IN_BYTES )
   {
      /* read comrv stack pool register (t4) */
      M_COMRV_READ_POOL_REG(pStackPool);
      /* get the address of the next available stack frame */
      pStackFrame = pStackPool;
      /* update the next stack pool address */
      pStackPool = (comrvStackFrame_t*)((u08_t*)pStackPool + pStackPool->ssOffsetPrevFrame);
      /* write the new comrv stack pool address */
      M_COMRV_WRITE_POOL_REG(pStackPool);
      /* read comrv application stack */
      pAppStack = (comrvStackFrame_t*)*pRegisterT3;
      /* update the offset to new stack frame */
      pStackFrame->ssOffsetPrevFrame = (s32_t)pAppStack - (s32_t)pStackFrame;
      /* save the return address */
      pStackFrame->uiCallerReturnAddress = *pMepc;
      /* set the address of COMRV stack in t3 */
      *pRegisterT3 = (u32_t)pStackFrame;
      /* save new mepc to point to comrv entry label 'comrv_ret_from_callee'
         so when 'context switch' back to this task, we will first make sure
         the overlay is loaded */
      *pMepc = (u32_t)&comrv_ret_from_callee;
      // TODO: not sure the following 2 lines are needed
      /* read comrv task stack register */
      M_COMRV_TASK_STACK_REG(pComrvTaskStack);
      /* clear the D_COMRV_POST_SEARCH_LOAD indication */
      pComrvTaskStack[D_COMRV_STATE_STACK_OFFSET_TMP] = 0;
   }
   /* check if mepc is in range of the overlay engine code - means that interruption
      occurred during execution of overlay engine */
   else if ((*pMepc - (u32_t)&COMRV_TEXT_SEC) < ((u32_t)comrvEntryDisable - (u32_t)&COMRV_TEXT_SEC))
   {
      /* verify we are not exactly in the engine entry/exit */
      // TODO: how can this be optimized
      if ((*pMepc != (u32_t)comrvEntry) && (*pMepc != (u32_t)comrv_ret_from_callee))
      {
         /* read comrv task stack register */
         M_COMRV_TASK_STACK_REG(pComrvTaskStack);
         /* point to comrv status */
         pComrvStatus = &pComrvTaskStack[D_COMRV_STATE_STACK_OFFSET_TMP];
         /* check if state is - 'post search and load'  */
         if (*pComrvStatus & D_COMRV_POST_SEARCH_LOAD)
         {
            // TODO: instead of the if we can have an array of the 2 entries
            /* save mepc according to interruption location - interrupted after entry */
            if (*pComrvStatus & D_COMRV_STATE_ENTRY)
            {
               *pMepc = (u32_t)&comrvEntry_context_switch;
            }
            /* interrupted after exit */
            else
            {
               *pMepc = (u32_t)&comrv_ret_from_callee_context_switch;
            }
            /* clear the D_COMRV_POST_SEARCH_LOAD indication */
            *pComrvStatus ^= D_COMRV_POST_SEARCH_LOAD;
            /* new SP address to return to caller */
            pSpAddr = (u32_t*)pComrvTaskStack[D_COMRV_SP_REG_STACK_OFFSET_TMP];
            /* set the address of registers value to be updated by caller */
            *pComrvTaskStackRegsVal = (comrvTaskStackRegsVal_t*)pComrvTaskStack;
         } /* if (*pComrvStatus & D_COMRV_POST_SEARCH_LOAD) */
      } /* if ((*pMepc != (u32_t)comrvEntry) && (*pMepc != (u32_t)comrv_ret_from_callee)) */
   } /* else if ((*pMepc - (u32_t)&COMRV_TEXT_SEC) < ((u32_t)comrvEntryDisable - (u32_t)&COMRV_TEXT_SEC)) */
#if defined(D_COMRV_RTOS_SUPPORT) && defined(D_COMRV_ALLOW_CALLS_AFTER_SEARCH_LOAD)
   else
   /* at this point we didn't interrupt the engine but maybe we interrupted a function 
      called by the engine (after the point of D_COMRV_POST_SEARCH_LOAD) */
   {
      /* read comrv task stack register */
      M_COMRV_TASK_STACK_REG(pComrvTaskStack);
      /* check if state is - 'post search and load'  */
      if (pComrvTaskStack[D_COMRV_STATE_STACK_OFFSET_TMP] & D_COMRV_POST_SEARCH_LOAD)
      {
         /* add the D_COMRV_POST_SEARCH_LOAD_INTERRUPTED indication */
         pComrvTaskStack[D_COMRV_STATE_STACK_OFFSET_TMP] |= D_COMRV_POST_SEARCH_LOAD_INTERRUPTED;
      }
   }
#endif /* D_COMRV_RTOS_SUPPORT && D_COMRV_ALLOW_CALLS_AFTER_SEARCH_LOAD*/

   return pSpAddr;
}

/**
* @brief This function is invoked by the comev engine in case an overlay function
* was invoked and comrv is not initialized
*
* @param None
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvNotifyNotInitialized(void)
{
   /* clear comrv state */
   comrvClearComrvState();

   #ifdef D_COMRV_ERROR_NOTIFICATIONS
   comrvErrorArgs_t stErrArgs;
#endif /* D_COMRV_ERROR_NOTIFICATIONS */
   M_COMRV_ERROR(stErrArgs, D_COMRV_NOT_INITIALIZED_ERR, D_COMRV_INVALID_TOKEN);
}
#endif /* D_COMRV_RTOS_SUPPORT */

/**
* @brief This function resets the comrv cache control block
*
* @param ucResetTables - non-zero value indicates that offset
*                        and multi-group tables are also reset
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvReset(comrvResetType_t eResetType)
{
   comrvCacheEntry_t *pCacheEntry;
   u08_t              ucPrevIndex;
   u08_t              ucIndex, ucResetCacheLoopCount, ucResetEvictionCountersLoopCount;
#ifdef D_COMRV_RTOS_SUPPORT
   u32_t              ret;
#ifdef D_COMRV_ASSERT_ENABLED
#ifdef D_COMRV_ERROR_NOTIFICATIONS
   comrvErrorArgs_t   stErrArgs;
#endif /* D_COMRV_ERROR_NOTIFICATIONS */
   comrvOverlayToken_t  unToken = { D_COMRV_INVALID_TOKEN };
#endif /* D_COMRV_ASSERT_ENABLED*/
#endif /* D_COMRV_RTOS_SUPPORT */

   /* clear comrv state */
   comrvClearComrvState();

   switch (eResetType)
   {
      /* caller wishes to reset cache table and eviction counters */
      case E_RESET_TYPE_LOADED_GROUPS:
         /* loop table entries excluding the offset/multi-group entry */
         ucResetCacheLoopCount = g_stComrvCB.ucLastCacheEntry;
         ucResetEvictionCountersLoopCount = 0;
         break;
       /* caller wishes to reset all cache tables (including offset and multigroup) and eviction counters */
      case E_RESET_TYPE_ALL:
         /* loop all table entries */
         ucResetCacheLoopCount = D_COMRV_NUM_OF_CACHE_ENTRIES;
         ucResetEvictionCountersLoopCount = 0;
         break;
      /* only reset eviction counters */
      case E_RESET_TYPE_LRU_HISTORY:
         ucResetEvictionCountersLoopCount = g_stComrvCB.ucLastCacheEntry;
         ucResetCacheLoopCount = 0;
         break;
      default:
         break;
   }

   /* enter critical section */
   M_COMRV_ENTER_CRITICAL_SECTION();

#ifdef D_COMRV_EVICTION_LRU

   /* cache reset */
   if (ucResetCacheLoopCount)
   {
      /* initialize all cache entries (exclude last cache entry which is
         reserved for comrv tables) */
      for (ucIndex = 0 ; ucIndex < ucResetCacheLoopCount ; ucIndex++)
      {
         pCacheEntry = &g_stComrvCB.stOverlayCache[ucIndex];
         /* initially each entry points to the previous and next neighbor cells */
         pCacheEntry->unLru.stFields.typPrevLruIndex = ucIndex-1;
         pCacheEntry->unLru.stFields.typNextLruIndex = ucIndex+1;
         pCacheEntry->unToken.uiValue                = D_COMRV_ENTRY_TOKEN_INIT_VALUE;
         pCacheEntry->unProperties.usValue           = D_COMRV_ENTRY_PROPERTIES_INIT_VALUE;
      }
      /* set the index of the LRU and MRU */
      g_stComrvCB.ucLruIndex = 0;
      g_stComrvCB.ucMruIndex = ucResetCacheLoopCount - 1;
      pCacheEntry->unLru.stFields.typNextLruIndex = D_COMRV_MRU_ITEM;
   }
   /* reset eviction counters */
   else if (ucResetEvictionCountersLoopCount)
   {
      ucPrevIndex = 0;
      /* loop stOverlayCache and reset lru list order - loaded groups won't be affected */
      for (ucIndex = 0 ; ucIndex < ucResetEvictionCountersLoopCount ; ucIndex = pCacheEntry->unLru.stFields.typNextLruIndex)
      {
         pCacheEntry = &g_stComrvCB.stOverlayCache[ucIndex];
         /* reset each entry to point to the previous and next neighbor cells (native in-order) */
         pCacheEntry->unLru.stFields.typPrevLruIndex = ucIndex;
         pCacheEntry->unLru.stFields.typNextLruIndex = ucIndex + pCacheEntry->unProperties.stFields.usSizeInMinGroupSizeUnits;
         ucPrevIndex = ucIndex;
      }
      /* set the index of the LRU and MRU */
      g_stComrvCB.ucLruIndex = 0;
      g_stComrvCB.ucMruIndex = ucPrevIndex;
      g_stComrvCB.stOverlayCache[0].unLru.stFields.typNextLruIndex = D_COMRV_LRU_ITEM;
      pCacheEntry->unLru.stFields.typNextLruIndex = D_COMRV_MRU_ITEM;
   }

#elif defined(D_COMRV_EVICTION_LFU)
#elif defined(D_COMRV_EVICTION_MIX_LRU_LFU)
#endif /* D_COMRV_EVICTION_LRU */

   /* exit critical section */
   M_COMRV_EXIT_CRITICAL_SECTION();
}

#ifdef D_COMRV_LOAD_CONFIG_SUPPORT
/**
* @brief This function enables/disables the 'load' operation. When 'load'
*        operation is disabled and an overlay function is invoked and not ,
*        already loaded, the engine will call the error hook.
*        By default, load is enabled.
*
* @param ucEnable - zero - disable the load operation
*                   non-zero - enable the load operation
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvConfigureLoadOperation(u08_t ucEnable)
{
   /* clear comrv state */
   comrvClearComrvState();

   /* the end user requested to enable the 'load' */
   if (ucEnable)
   {
      g_stComrvCB.ucLoadEnabled = D_COMRV_LOAD_ENABLED;
   }
   else
   {
      g_stComrvCB.ucLoadEnabled = D_COMRV_LOAD_DISABLED;
   }
}
#endif /* D_COMRV_LOAD_CONFIG_SUPPORT */

#ifdef D_COMRV_OVL_DATA_SUPPORT
/**
* @brief This function releases the cache area held by the given token
*
* @param pToken - a token, representing the required data overlay
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
void comrvDataOverlayRelease(const void* pToken)
{
   /* clear comrv state */
   comrvClearComrvState();

   comrvLockUnlockOverlayGroupByFunction(pToken, D_COMRV_GROUP_STATE_UNLOCK);
}

/**
* @brief This function return the address of the given data overlay token
*
* @param pToken - a token, representing the required data overlay
*
* @return physical address of loaded data overlay token
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
const void* comrvDataOverlayAllocation(const void* pToken)
{
   comrvStackFrame_t   *pComrvStackFrame;
   /* get the required token and set the 'data overlay' field */
   u32_t uiTemp = (u32_t)pToken;
   /* indication to comrvGetAddressFromToken so it knows we need to load 'data overlay'
      this indication forces a 'lock' on the loaded overlay */
   u32_t* pFakeAddress = (u32_t*)D_COMRV_DATA_OVERLAY_IND;

   /* clear comrv state */
   comrvClearComrvState();

   /* we need to set in the 'token register' the value of the data overlay token */
   M_COMRV_WRITE_TOKEN_REG(uiTemp);

   /* read comrv stack register (t3) */
   M_COMRV_READ_STACK_REG(pComrvStackFrame);

   /* set the invoke indication from pComrvStackFrame address -
      needed for extracting the offset from the token and not from the return address */
   pComrvStackFrame = (comrvStackFrame_t*)((u32_t)pComrvStackFrame | D_COMRV_INVOKE_CALLEE_BIT_0);

   /* write back the stack register after bit 0 was cleared*/
   M_COMRV_WRITE_STACK_REG(pComrvStackFrame);

   /* get and return the overlay loaded address */
   return comrvGetAddressFromToken(pFakeAddress);
}
#endif /* D_COMRV_OVL_DATA_SUPPORT */

/**
* @brief This function clears comrv state (applies rtos support)
*
* @param None
*
* @return None
*/
D_COMRV_NO_CHANGE_IN_FLOW_TEXT_SECTION
static void comrvClearComrvState(void)
{
#ifdef D_COMRV_RTOS_SUPPORT
   u32_t  *pTaskStackReg, uiPrevIntState;

   /* we want to safely clear comrv state */
   M_COMRV_DISABLE_INTS(uiPrevIntState);

   /* read comrv task stack register */
   M_COMRV_TASK_STACK_REG(pTaskStackReg);

   /* clear comrv state - in case of context switch we don't want to rollback
      to comrvEntry and therefore we are clearing comrv state when handling
      data overlays */
   pTaskStackReg[D_COMRV_STATE_STACK_OFFSET_TMP] = D_COMRV_STATE_CLEAR;

   /* re-enable interrupts */
   M_COMRV_ENABLE_INTS(uiPrevIntState);
#endif /* D_COMRV_RTOS_SUPPORT */

}
