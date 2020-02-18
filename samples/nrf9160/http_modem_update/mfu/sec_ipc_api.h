//
// Copyright (c) 2017 Nordic Semiconductor. All Rights Reserved.
//
// The information contained herein is confidential property of Nordic Semiconductor. The use,
// copying, transfer or disclosure of such information is prohibited except by express written
// agreement with Nordic Semiconductor.
//

#ifndef _SEC_IPC_API_H_
#define _SEC_IPC_API_H_


#define IPC_DFU_CMD_PROGRAM_RANGE         0x00000003ul
#define IPC_DFU_CMD_READ_RANGE            0x00000004ul

#define IPC_DFU_CMD_DIGEST_RANGE          0x00000007ul
#define IPC_DFU_CMD_READ_UUID             0x00000008ul

#define IPC_DFU_RESP_UNKNOWN_CMD          0x5A000001ul
#define IPC_DFU_RESP_CMD_ERROR            0x5A000002ul

#define IPC_DFU_RESP_ROOT_KEY_DIGEST      0xA5000001ul
#define IPC_DFU_RESP_DFU_EXECUTABLE       0xA5000002ul
#define IPC_DFU_RESP_PROGRAM_RANGE        0xA5000005ul
#define IPC_DFU_RESP_DIGEST_RANGE         0xA5000007ul
#define IPC_DFU_RESP_READ_UUID            0xA5000008ul

#define IPC_DFU_RESP_STATUS_OK            0x00000001ul
#define IPC_DFU_RESP_STATUS_FAIL          0xFFFFFFFFul  ///< -1

#define IPC_DFU_MIN_DIGEST_RANGE_BYTES    16

typedef struct __attribute__ ((__packed__))
{
    uint32_t    version;        /**< RPC version. If DFU_RPC_VERSION_NUMBER, use this structure */
    uint32_t    shmem_start;    /**< Start address of shared memory region dedicated for the DFU usage */
    uint32_t    shmem_size;     /**< Size of shared memory region reserved for DFU usage */
} ipc_dfu_descriptor_t;

typedef struct
{
  uint32_t  id;
  uint32_t  param[];
} ipc_dfu_cmd_t;

typedef struct
{
  uint32_t  id;
  uint32_t  start;
  uint32_t  bytes;
  uint32_t  data[];
} ipc_dfu_program_cmd_t;

typedef struct
{
  uint32_t start;
  uint32_t bytes;
} digest_range_t;

typedef struct
{
  uint32_t  id;
  uint32_t  no_ranges;
  digest_range_t ranges[];
} ipc_dfu_digest_cmd_t;

typedef struct
{
  uint32_t  id;
  uint32_t  payload[];
} ipc_dfu_rsp_t;

#endif /* _SEC_IPC_API_H_ */
