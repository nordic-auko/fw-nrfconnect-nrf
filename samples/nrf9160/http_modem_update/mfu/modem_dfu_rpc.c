#include "modem_dfu_rpc.h"
#include <zephyr.h>
#include "nrf.h"
#include "modem_rpc_defines.h"

/* Initialize the logger for this file */
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_MODULE_NAME modem_dfu_rpc
#include <logging/log.h>
LOG_MODULE_REGISTER();

/* IPC related defines */
#define VMC_RAM_BLOCKS                    8
#define VMC_RAM_SECTIONS_PER_BLOCK        4
#define IPCEVENT_FAULT_RECEIVE_CHANNEL    0
#define IPCEVENT_COMMAND_RECEIVE_CHANNEL  2
#define IPCEVENT_DATA_RECEIVE_CHANNEL     4
#define IPCTASK_DATA_SEND_CHANNEL         1
#define IPC_WAIT_FOR_EVENT_TIME_MS        3000

/* Forward declaration of local (static) functions */
static void         modem_ipc_init(void);
static void         modem_ipc_clear_events(void);
static modem_err_t  modem_ipc_wait_for_event(void);
static modem_err_t  modem_ipc_trigger_and_check_response(void);
static modem_err_t  modem_prepare_for_dfu(modem_digest_buffer_t * digest_buffer);


/* There are multiple definitions of the header bytes used in the RPC protocol, depending on the type of command.
 * This is why different structs are used to keep track of the byte field names */
union dfu_modem_com_t{
    volatile modem_rpc_cmd_t            cmd;
    volatile modem_rpc_program_cmd_t    program;
    volatile modem_rpc_digest_cmd_t     digest;
    volatile modem_rpc_response_t       response;
    volatile uint8_t                    buffer[1];
};

static volatile union dfu_modem_com_t * rpc_buffer    = NULL;
static volatile uint32_t rpc_buffer_total_size        = 0;
static volatile uint32_t rpc_buffer_payload_size      = 0;

/* The "descriptor" struct defines the API version and buffer location (of rpc_buffer)
 * for the Remote Procedure Call communication. This struct is handed to the modem when initializing */
static volatile modem_rpc_descriptor_t descriptor = {
  .version      = 0x80010000,
  .shmem_start  = (uint32_t)NULL,
  .shmem_size   = 0
};

/* Struct for tracking the state of the modem and firmware upload progress */
typedef struct {
    modem_state_t       modem_state;
    bool                rpc_fault_indicated;
    bool                rpc_command_error_indicated;
    unsigned long long  rpc_data_target_address;
    unsigned long long  rpc_offset_in_buffer;
} status_t;

static volatile status_t status = {
    .modem_state                  = MODEM_STATE_UNINITIALIZED,
    .rpc_fault_indicated          = false,
    .rpc_command_error_indicated  = false,
    .rpc_data_target_address      = 0,
    .rpc_offset_in_buffer         = 0
};

/* Get functions for modem state */
bool modem_get_fault_indicated(void){
    return status.rpc_fault_indicated;
}
bool modem_get_command_error_indicated(void){
    return status.rpc_command_error_indicated;
}
modem_state_t modem_get_state(void){
    return status.modem_state;
}

/* Helping function for entering modem bad state */
static modem_state_t set_modem_state_bad(void){
    status.modem_state = MODEM_STATE_BAD;
    return MODEM_ERR_BADSTATE;
}


/* Main initialization function for this module
 * Init configures IPC, shared RAM, and attempts to put modem in DFU mode */
modem_err_t modem_dfu_rpc_init(modem_digest_buffer_t * digest_buffer, uint8_t* modem_rpc_buffer, uint32_t modem_rpc_buffer_length){
    /* Check if buffer is too small */
    if (modem_rpc_buffer_length < MODEM_RPC_BUFFER_MIN_SIZE){
        return MODEM_ERR_INVALID_ARGUMENT;
    }

    /* Find total buffer size to use. It must follow the form (0x10 + n*PAGE_SIZE) */
    rpc_buffer_total_size = MODEM_RPC_BUFFER_MIN_SIZE;
    while( (rpc_buffer_total_size + MODEM_RPC_PAGE_SIZE) <= modem_rpc_buffer_length){
        rpc_buffer_total_size += MODEM_RPC_PAGE_SIZE;
    }

    /* Enforce max limit */
    if (modem_rpc_buffer_length > MODEM_RPC_BUFFER_MAX_SIZE){
        rpc_buffer_total_size = MODEM_RPC_BUFFER_MAX_SIZE;
    }

    /* Update payload size (total - overhead) */
    rpc_buffer_payload_size = rpc_buffer_total_size - MODEM_RPC_OVERHEAD_SIZE;

    /* Set rpc_buffer to point to given buffer */
    rpc_buffer = (union dfu_modem_com_t *)(modem_rpc_buffer);

    if (rpc_buffer_total_size != modem_rpc_buffer_length){
        LOG_DBG("The RPC buffer has %uB unused space (not page aligned)\r\n", modem_rpc_buffer_length - rpc_buffer_total_size);
    }

    // Reset status
    status.modem_state                  = MODEM_STATE_UNINITIALIZED;
    status.rpc_fault_indicated          = false;
    status.rpc_command_error_indicated  = false;
    status.rpc_data_target_address      = 0;
    status.rpc_offset_in_buffer         = 0;

    modem_ipc_init();
    return modem_prepare_for_dfu(digest_buffer);
}

static void modem_ipc_init(void){
    #ifndef NRF_TRUSTZONE_NONSECURE
        /* Configure APP IPC as non-secure */
        NRF_SPU_S->PERIPHID[IPC_IRQn].PERM = 0;
    
        /* Configure APP RAM to non-secure */
        const uint32_t ram_pages = VMC_RAM_BLOCKS * VMC_RAM_SECTIONS_PER_BLOCK;
        const uint32_t perm_read_write_excecute = 0x7;

        for (uint32_t ramregion = 0; ramregion < ram_pages; ramregion++)
            NRF_SPU_S->RAMREGION[ramregion].PERM = perm_read_write_excecute;
    #endif
    
    /* Enable brodcasting on IPC channel 1 and 3 */
    NRF_IPC_NS->SEND_CNF[1] = (1 << 1);
    NRF_IPC_NS->SEND_CNF[3] = (1 << 3);
    
    /* Send rpc descriptor memory address to slave MCU via IPC */
    NRF_IPC_NS->GPMEM[0] = (uint32_t)&descriptor | 0x01000000ul;

    /* Reset fault indication */
    NRF_IPC_NS->GPMEM[1] = 0;

    /* Enable subscription to relevant channels */
    NRF_IPC_NS->RECEIVE_CNF[IPCEVENT_FAULT_RECEIVE_CHANNEL]     = (1 << IPCEVENT_FAULT_RECEIVE_CHANNEL);
    NRF_IPC_NS->RECEIVE_CNF[IPCEVENT_COMMAND_RECEIVE_CHANNEL]   = (1 << IPCEVENT_COMMAND_RECEIVE_CHANNEL);
    NRF_IPC_NS->RECEIVE_CNF[IPCEVENT_DATA_RECEIVE_CHANNEL]      = (1 << IPCEVENT_DATA_RECEIVE_CHANNEL);

    modem_ipc_clear_events();
}

static modem_err_t modem_prepare_for_dfu(modem_digest_buffer_t * digest_buffer){

    modem_ipc_clear_events();

    /* Store DFU indication into shared memory */
    descriptor.version        = 0x80010000;
    descriptor.shmem_start    = (uint32_t)(rpc_buffer) | 0x01000000ul;
    descriptor.shmem_size     = rpc_buffer_total_size;

    #ifndef NRF_TRUSTZONE_NONSECURE
    k_sleep(10);
    *((volatile uint32_t *)0x50005610) = 0;
    k_sleep(10);
    *((volatile uint32_t *)0x50005610) = 1;
    k_sleep(10);
    *((volatile uint32_t *)0x50005610) = 0;
    k_sleep(10);
    #else
    k_sleep(10);
    *((volatile uint32_t *)0x40005610) = 0;
    k_sleep(10);
    *((volatile uint32_t *)0x40005610) = 1;
    k_sleep(10);
    *((volatile uint32_t *)0x40005610) = 0;
    k_sleep(10);
    #endif

    /* Start polling IPC.MODEM_CTRL_EVENT to receive root key digest */
    if (modem_ipc_wait_for_event() != MODEM_SUCCESS){
        return set_modem_state_bad();
    }

    /* Check response field */
    if (rpc_buffer->response.id != MODEM_RPC_RESP_ROOT_KEY_DIGEST){
        return set_modem_state_bad();
    }

    /* Copy rootkey/bootloader digest */
    memcpy(digest_buffer->data, (uint8_t*)(rpc_buffer->response.payload), MODEM_DIGEST_BUFFER_LEN);
  
    /* Update the dfu state. After the modem reset, a bootloader must be programmed */
    status.modem_state = MODEM_STATE_WAITING_FOR_BOOTLOADER;
    return MODEM_SUCCESS;
}

static void modem_ipc_clear_events(void){
    NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_COMMAND_RECEIVE_CHANNEL]  = 0;
    NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_DATA_RECEIVE_CHANNEL]     = 0;
    NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_FAULT_RECEIVE_CHANNEL]    = 0;
}

static modem_err_t modem_ipc_wait_for_event(void){
    s64_t start_time = k_uptime_get();
    s64_t compare_time = start_time;
    bool got_event = false;

    while(k_uptime_delta(&compare_time) < IPC_WAIT_FOR_EVENT_TIME_MS){
        /* k_uptime_delta will update the time value held by the input argument, so we need to reset it to the start time */
        compare_time = start_time;

        if (NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_COMMAND_RECEIVE_CHANNEL] || NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_DATA_RECEIVE_CHANNEL]){
            got_event = true;
            break;
        }
        if (NRF_IPC_NS->EVENTS_RECEIVE[IPCEVENT_FAULT_RECEIVE_CHANNEL]){
            status.rpc_fault_indicated = true;
            return MODEM_STATE_BAD;
        }
    }

    modem_ipc_clear_events();
    
    /* Got timeout */
    if (!got_event){
        return MODEM_ERR_BADSTATE;
    }

    return MODEM_SUCCESS;
}

static modem_err_t modem_ipc_trigger_and_check_response(void){    
    LOG_DBG("Trigger IPC event\n\r");

    /* Trigger ipc and check for timeout. */
    modem_ipc_clear_events();
    NRF_IPC_NS->TASKS_SEND[IPCTASK_DATA_SEND_CHANNEL] = 1;

    modem_err_t err = modem_ipc_wait_for_event();
    if (err != MODEM_SUCCESS){
        return err;
    }

    /* Check the rpc command return value (placed by the modem in shared memory) */
    switch (rpc_buffer->response.id)
    {
        case MODEM_RPC_RESP_UNKNOWN_CMD:
            LOG_DBG("\tReceived MODEM_RPC_RESP_UNKNOWN_CMD.\n\r");
            status.rpc_fault_indicated = true;
            return MODEM_ERR_BADSTATE;
        case MODEM_RPC_RESP_CMD_ERROR:
            LOG_DBG("\tReceived MODEM_RPC_RESP_CMD_ERROR.\n\r");
            status.rpc_command_error_indicated = true;
            return MODEM_ERR_BADSTATE;
        default:
            break;
    }
    LOG_DBG("\tReceived ACK_OK.\n\r");
    return MODEM_SUCCESS;
}


static void rpc_program_write_metadata(uint32_t address, uint32_t size){
    LOG_DBG("Write RPC data output:\n\r");
    LOG_DBG("\tid:    IPC_DFU_CMD_PROGRAM_RANGE \n\r");
    LOG_DBG("\tstart: 0x%08X\n\r", address);
    LOG_DBG("\tbytes: 0x%08X\n\r", size);
    rpc_buffer->program.id    = MODEM_RPC_CMD_PROGRAM_RANGE;
    rpc_buffer->program.start = address;
    rpc_buffer->program.bytes = size;
}

modem_err_t modem_write_bootloader_chunk(modem_memory_chunk_t * bootloader_chunk){
    /* Check if the current state allows programming bootloader.
     * Bootloader can be written both in WAITING_FOR_BOOTLOADER and READY_FOR_IPC_COMMANDS state */
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;
        
        /* Fallthrough */
        case MODEM_STATE_WAITING_FOR_BOOTLOADER:
        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }
    
    /* Check current offset position. We must be able to fit the entire bootloader into the RPC buffer */
    if ((status.rpc_offset_in_buffer + bootloader_chunk->data_len) > rpc_buffer_payload_size){
        LOG_ERR("Modem bootloader too large for RPC buffer.");
        return MODEM_ERR_INVALID_ARGUMENT;
    }

    /* Write data chunk to RPC buffer with given offset 
     * No spesific RPC instruction is used, e.g. the bootloader is written directly to the buffer */
    memcpy((uint8_t *)(&(rpc_buffer->buffer)) + status.rpc_offset_in_buffer, bootloader_chunk->data, bootloader_chunk->data_len);
    status.rpc_offset_in_buffer += bootloader_chunk->data_len;

    return MODEM_SUCCESS;
}


/* Function to write incoming image/firmware data to memory
 * Assumes all data in the same firmware chunk are contigous */
modem_err_t modem_write_firmware_chunk(modem_memory_chunk_t * firmware_chunk){
    modem_err_t err;

    /* Check if the current state programming firmware */
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;

        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }

    /* Dissallow 0 as address */
    if (firmware_chunk->target_address == 0){
        LOG_DBG("Invalid argument in write_firmware_chunk: target address is zero.\r\n");
        return MODEM_ERR_INVALID_ARGUMENT;
    }

    /* Set the new target address if the RPC buffer is empty */
    if (status.rpc_offset_in_buffer == 0){
        status.rpc_data_target_address = firmware_chunk->target_address;
    }
    
    /* If the target address is not aligned with prev chunk. Submit what is already written in the RPC buffer */
    bool address_aligned_with_prev_chunk = ((status.rpc_data_target_address + status.rpc_offset_in_buffer) == firmware_chunk->target_address);
    if (!address_aligned_with_prev_chunk && status.rpc_offset_in_buffer != 0){
        LOG_DBG("Firmware data address is unaligned. Commit data to start new data block\n\r");
        rpc_program_write_metadata(status.rpc_data_target_address, status.rpc_offset_in_buffer);
        status.rpc_offset_in_buffer = 0;
        err = modem_ipc_trigger_and_check_response();
        status.rpc_data_target_address = firmware_chunk->target_address;
        if (err != MODEM_SUCCESS){
            return set_modem_state_bad();
        }
    }
    
    /* Start loop for writing the firmware chunk data */
    uint32_t bytes_written = 0;
    while  (bytes_written < firmware_chunk->data_len){
        /* Write as much as possible to the buffer */
        uint32_t bytes_to_write = MIN(firmware_chunk->data_len - bytes_written, rpc_buffer_payload_size - status.rpc_offset_in_buffer);
        memcpy((uint8_t *)(&(rpc_buffer->program.data)) + status.rpc_offset_in_buffer, firmware_chunk->data + bytes_written, bytes_to_write);

        /* Update offset variables */
        bytes_written += bytes_to_write;
        status.rpc_offset_in_buffer += bytes_to_write;

        /* Submit buffer if full */
        if (status.rpc_offset_in_buffer >= rpc_buffer_payload_size){
            rpc_program_write_metadata(status.rpc_data_target_address, status.rpc_offset_in_buffer);
            err = modem_ipc_trigger_and_check_response();

            /* Reset offset and assign new target address for RPC buffer data */
            status.rpc_data_target_address += status.rpc_offset_in_buffer;
            status.rpc_offset_in_buffer = 0;
            if (err != MODEM_SUCCESS){
                return set_modem_state_bad();
            }
        }
    }

    return MODEM_SUCCESS;
}

modem_err_t modem_start_transfer()
{
    /* Check current state */
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;

        /* Fallthrough */
        case MODEM_STATE_WAITING_FOR_BOOTLOADER:
        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }

    status.rpc_offset_in_buffer = 0;
    return MODEM_SUCCESS;
}

modem_err_t modem_end_transfer()
{
    /* Check current state */
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;

        /* Fallthrough */
        case MODEM_STATE_WAITING_FOR_BOOTLOADER:
        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }
    

    /* Skip if no data in buffer */
    if (status.rpc_offset_in_buffer == 0)
    {
        status.modem_state = MODEM_STATE_READY_FOR_IPC_COMMANDS;
        return MODEM_SUCCESS;
    }

    /* Firmware upload requires meta information to be written to RPC fields */
    if (status.modem_state == MODEM_STATE_READY_FOR_IPC_COMMANDS){
        rpc_program_write_metadata(status.rpc_data_target_address, status.rpc_offset_in_buffer);
    }

    status.rpc_offset_in_buffer = 0;


    modem_err_t err = modem_ipc_trigger_and_check_response();
    if (err != MODEM_SUCCESS){
        return set_modem_state_bad();
    }
    
    /* The resulting state is the same for successful bootloader and firmware upload */
    status.modem_state = MODEM_STATE_READY_FOR_IPC_COMMANDS;

    return MODEM_SUCCESS;
}

/* Function for requesting a digest/hash of a memory range from the modem */
modem_err_t modem_get_memory_hash(uint32_t start_address, uint32_t end_address, modem_digest_buffer_t * digest_buffer){
    /* Check the current state*/
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;

        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }

    modem_ipc_clear_events();

    /* Write command indication */ 
    rpc_buffer->digest.id = MODEM_RPC_CMD_DIGEST_RANGE;

    /* Write a list of memory segments that we want to verify into the ipc_rpc data buffer. 
     * The max span/length of each segment to verify is limited by rpc_buffer_payload_size */
    uint32_t segment_counter = 0;
    uint32_t address = start_address;
    uint32_t length = MIN((uint32_t)(end_address - start_address + 1), MODEM_RPC_MAX_DIGEST_RANGE_BYTES);
    while(address < end_address){
        rpc_buffer->digest.ranges[segment_counter].start = address;
        rpc_buffer->digest.ranges[segment_counter].bytes = length;

        /* Update iteration variables */
        address += length;
        length = MIN((uint32_t)(end_address - address + 1), MODEM_RPC_MAX_DIGEST_RANGE_BYTES);
        ++segment_counter;
    }

    /* Check if there is anything to verify */ 
    if (segment_counter == 0){
        return MODEM_ERR_INVALID_ARGUMENT;
    }

    /* Write the number of segments to verify to rpc buffer */    
    rpc_buffer->digest.no_ranges = segment_counter;

    /* Intiate verification process */
    modem_err_t err = modem_ipc_trigger_and_check_response();
    if (err != MODEM_SUCCESS){
        return set_modem_state_bad();
    }
  
    /* Put the digest response in the response */
    memcpy(digest_buffer->data, (uint8_t *)(rpc_buffer->response.payload), MODEM_DIGEST_BUFFER_LEN);
    
    return MODEM_SUCCESS;
}

modem_err_t modem_get_uuid(modem_uuid_t * modem_uuid){
    /* Check the current state*/
    switch(status.modem_state){
        case MODEM_STATE_BAD:
            return MODEM_ERR_BADSTATE;

        case MODEM_STATE_READY_FOR_IPC_COMMANDS:
            break;

        default:
            return MODEM_ERR_INVALID_OPERATION;
    }
    
    rpc_buffer->cmd.id = MODEM_RPC_CMD_READ_UUID;
    modem_err_t err = modem_ipc_trigger_and_check_response();
    if (err != MODEM_SUCCESS){
        return set_modem_state_bad();
    }

    memcpy( modem_uuid->data, (uint8_t*)(rpc_buffer->response.payload), MODEM_UUID_BUFFER_LEN);
    return MODEM_SUCCESS;
}

