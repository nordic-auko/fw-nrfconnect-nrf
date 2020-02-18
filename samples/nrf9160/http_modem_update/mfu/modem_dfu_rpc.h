#ifndef __MODEM_DFU_RPC_H__
#define __MODEM_DFU_RPC_H__

/* This library API can be used to do a full upgrade of the nrf91 modem firmware.
 * As of now, Feb 2020, there are four files of interest included in the modem firmware zip folder:
 *     #1 72B3D7C.ipc_dfu.signed_<version_number>.ihex
 *        This is the modem bootloader file, which is the first hex file you must give to the modem. The preceeding numbers 
 *        "72B3D7C" in the filename equivalents to the hex value of the root digest hash value the modem responds with after
 *        being put in DFU mode. The bootloader hex file is assumed contigous and less than one page in size (0x2000 byte).
 *        Note that programming the bootloader is slightly different from the other hex files, as the modem doesn't need an
 *        address describing where to put the data.
 *
 *     #2 firmware.update.image.segments.0.hex
 *        This file contains meta-info about the main firmware file, and is significantly smaller than the second segment.
 *        It acts as a key the modem needs to verify before beginning the main transaction, and is to be programmed after
 *        the bootloader has been accepted.
 *
 *     #3 firmware.update.image.segments.1.hex
 *        The main firmware file. This hex file has multiple memory regions (contigous regions).
 *
 *     #4 firmware.update.image.digest.txt
 *        The digest file is a plain text file that describes a set of memory ranges in the modem, and the expected hash value
 *        of the content of these memory ranges. This is used post-upgrade to verify the modem memory contents, and is mostly
 *        useful in the case where a re-upload of the modem is possible without the use of the modem.
 *        
 *
 * About data representation:
 *        This library requires that any firmware/hex files are converted to a binary representation. When writing data to the
 *        modem one must supply:
 *            - a byte array of contigous data. 
 *            - a target address
 *            - the length of the array
 *        These values are passed using the struct 'modem_memory_chunk_t'.
 *
 *        The address describes where to put the first byte of the byte array (in the modem memory), and is found when parsing
 *        the hex file. Contigous data means that the bytes in the array belongs as a single uninterrupted sequence, i.e. a
 *        block of data in memory. This implies that if the hex files describes multiple regions of contigous data it must be
 *        fragmented to contigous blocks before programming. This is the case for firmware segment.1 (file #3). Moreover, some
 *        of the contigous data blocks are too large to be held in memory and needs to be broken down into a set of shorter byte
 *        arrays.
 *
 *        When programming firmware/bootloader, use repeated calls to the write function 'modem_write_firmware_chunk' or
 *        'modem_write_bootloader_chunk'.
 *
 *        Write process pseudocode for a hex file split into binary contigous blocks:
 *
 *            modem_start_transfer()
 *            modem_memory_chunk_t memory_chunk
 *            uint8_t buffer[200]
 *            memory_chunk.data = buffer
 *            for block in contigous_blocks
 *                bytes_read = 0
 *                while bytes_read < block.length 
 *                    n_bytes = read_block(from:  block, to: buffer, max_bytes: 200)
 *                    memory_chunk.target_address   = block.start_address + bytes_read
 *                    memory_chunk.data_len         = n_bytes
 *                    modem_write_firmware_chunk( &memory_chunk)
 *                    bytes_read  += n_bytes
 *            modem_end_transfer()            
 *        
 * 
 * How to perform a full modem firmware upgrade using this library in steps:
 *     1) Run the initialize function
 *        This function will configure the RAM and IPC to non-secure and set up the required IPC channels. Then the address of
 *        an RPC descriptor is written to GPMEM[0]. This descriptor gives the modem information about the API version to use,
 *        and the length and address of the communication buffer. The communication buffer is set to be a minimum of 1 page (2kB)
 *        plus the maximum overhead of the RPC protocol (16B / 4 x uint32).
 * 
 *        The init function takes a pointer to a digest_buffer_t struct. If successful, the root digest hash modem response is
 *        put into the buffer.
 *
 *     2) (Optional): Use the root digest hash that you obtained from the init function to verify you have the correct bootloader
 *        by comparing it to the hex filename (file #1).
 *
 *     3) Program the files in order:
 *            Bootloader  (file #1)
 *            segmen0     (file #2)
 *            segment1    (file #3)
 *        
 *        (See note on data representation)
 *        For each file do:
 *            - Invoke the 'modem_start_transfer' function before writing data to the modem. In practice, this function does not
 *              communicate with the modem, it only resets variables used to load data into the RPC buffer.
 *            - Write the data by using either 'modem_write_bootloader_chunk' or 'modem_write_firmware_chunk'. These functions
 *              takes a pointer to a 'modem_memory_chunk_t' struct that holds a piece of contigous data and a target address.
 *              Note that the 'target_address' field of the struct is ignored when writing the bootloader.
 *            - Invoke modem_end_transfer to finalize the transfer. If this function returns MODEM_SUCCESS, the programming was
 *              successful.
 *        
 *     4) (Optional): Use the digest text file (file #4) to verify that the modem firmware is correct
 *        Use the 'modem_get_memory_hash' to get a 256-bit hash of a memory region. Compare the result with the number in the
 *        text file. Remember to convert the text file number to binary before comparing.
 *        
 *  About error handling:
 *        The library uses an internal state machine to check whether an action is permitted
 *
 *        +-------+
 *        | Start |
 *        +--+----+
 *           | Init IPC
 *           | and RAM
 *           v                       Error
 *        +--+------------+  Error   +---+
 *        | Modem         +------+   |   |
 *        | Uninitialized |      v   |   v
 *        +--+------------+    +-+---+---+-+
 *           |                 | Modem bad |
 *           +<----------------+ state     |
 *           |  Initialize     +--------+--+
 *           v                          ^
 *        +--+----------+               |
 *        | Waiting for |               |
 *        | bootloader  +-------------->+
 *        +--+----------+  Error        |
 *           |  Program                 |
 *           |  bootloader              |
 *           v                          |
 *        +--+------------+             |
 *        | Ready for RPC +-------------+
 *        | commands      |  Error
 *        +-+---+---+---+-+
 *          |   ^   |   ^
 *          |   |   |   |
 *          +---+   +---+
 *         Program  Verify
 *        
 *
 *        MODEM_STATE_UNINITIALIZED
 *          The program is in an uninitialized state before calling the init function 'modem_dfu_rpc_init'. If the init function is
 *          successful, the state continues to MODEM_STATE_WAITING_FOR_BOOTLOADER.
 *
 *          If the the init function returns an error, we enter MODEM_STATE_BAD. This occurs if the modem does not signal the IPC
 *          within a timelimit after being put in DFU mode. Usually, this has only been the case if the shared RAM region or IPC has
 *          been configured with the wrong settings/permissions and should not happen.
 *
 *        MODEM_STATE_WAITING_FOR_BOOTLOADER
 *          After putting the modem in DFU mode, it expects to recieve a bootloader. Other actions such as program and verify will fail
 *          before the bootloader is programmed. If the programming is successful the modem is ready for RPC communication, i.e. we enter
 *          the MODEM_STATE_READY_FOR_IPC_COMMANDS state.
 *
 *          If corrupted data is input instead of a valid bootloader we go to MODEM_STATE_BAD. This can be either because the modem
 *          signaled a fault, or as a result of a response timeout.
 *          
 *        MODEM_STATE_READY_FOR_IPC_COMMANDS
 *          In this state, we can send requests to program a region of the modem flash, ask for a hash of a memory range, or re-program
 *          the bootloader. However, this state has a hidden state, as you first need to program segment.0 (file #2) before you can program
 *          the rest of the firmware.
 *
 *          If the modem signals a fault or error, or fails to respons, we enter MODEM_STATE_BAD.
 *
 *        MODEM_STATE_BAD
 *          Some operation that was attempted with the modem failed, and usually the reason is unknown. The modem performs validation of the
 *          files we want to program, and can f.ex. signal an error because of firmware downgrade protection (not all versions have this).
 *          To get out of this state, re-initialize the modem using the init function. This will reset all your progress and you will have to
 *          go through all the steps again. Note that in some cases a when there is an unidentified problem, it can't be fixed by running 
 *          the init function such that a board reset might be necessary.
 *        
 *        You can also use the following functions to get information about the modem state:
 *            - modem_state_t modem_get_state(void);
 *            - bool modem_get_command_error_indicated(void);
 *            - bool modem_get_fault_indicated(void);
 *        
 *        Also, check the return value of the function calls to see if it was successful.
 *
 *        If the modem responds with IPC_DFU_RESP_UNKNOWN_CMD (0x5A000001ul), the modem could not interpret the RPC data and a fault
 *        indication flag is set. The modem can indicate a fault both through the RPC response field and by writing a value to GPMEM[1].
 *        If the modem responds with IPC_DFU_RESP_CMD_ERROR (0x5A000002ul), the modem have rejected the command. A few possible reasons for
 *        this are:
 *            - data block target address range is invalid (valid ranges are 0x6000...0xFFFF and 0x50000...0x27FFFF)
 *            - data block size is bigger than the shared memory area size
 *            - data block size is bigger than FLASH size 0x280000
 *            - data block target address is not aligned to FLASH page (0x2000 bytes)
 *            - metadata is invalid
 *            - data block integrity check failed
 *            - flash target block erase failed
 *            - flash target block write failed
 * 
 *        By design, the response of the modem is limited to make it harder for an attacker to get information from the device. Unfortunately,
 *        this means extra harm for the user. The only option for recovering the modem state provided in this API is to restart the DFU
 *        from scratch. 
 * 
 *   
 */

#include <zephyr.h>

/* Function return codes */
typedef enum {
    MODEM_SUCCESS               =  0,
    MODEM_ERR_BADSTATE          = -1, 
    MODEM_ERR_INVALID_ARGUMENT  = -2,
    MODEM_ERR_INVALID_OPERATION = -3
} modem_err_t; 

/* Modem states */
typedef enum {
    MODEM_STATE_UNINITIALIZED           = 1,
    MODEM_STATE_WAITING_FOR_BOOTLOADER  = 2,
    MODEM_STATE_READY_FOR_IPC_COMMANDS  = 3,
    MODEM_STATE_BAD                     = 4
} modem_state_t;


/* Struct type for passing firmware data.
 * One piece of contiguous firmware is split into smaller modem_memory_chunk_t chunks. */
typedef struct {
    /* Destination address for the data (read by the modem)
     * Not used with bootloader firmware */
    unsigned long long target_address;

    /* Chunk data and length (num bytes) */
    size_t data_len;
    uint8_t * data;
} modem_memory_chunk_t;


#define MODEM_DIGEST_BUFFER_LEN 32
#define MODEM_UUID_BUFFER_LEN 36

/* Struct type for storing 256-bit digest/hash replies */
typedef struct {
    uint8_t data[MODEM_DIGEST_BUFFER_LEN];
} modem_digest_buffer_t;

/* Struct for storing modem UUID response (36B) */
typedef struct {
    uint8_t data[MODEM_UUID_BUFFER_LEN];
} modem_uuid_t;


/* The init function takes an array and the length of the array. The array is used as shared RAM to communicate
 * with the modem. The RPC payload must be modem page aligned, which means the smallest possible buffer is 1 page + RPC overhead data.
 * In general, use array lenghts of the form:     MODEM_RPC_OVERHEAD_SIZE + n * MODEM_RPC_MODEM_PAGE_SIZE, for n[1 .. 8] 
 * A larger buffer does not necessarily mean better performance */
#define MODEM_RPC_PAGE_SIZE                     0x2000
#define MODEM_RPC_OVERHEAD_SIZE                 0x10
#define MODEM_RPC_BUFFER_MIN_SIZE               (MODEM_RPC_OVERHEAD_SIZE + MODEM_RPC_PAGE_SIZE * 1)
#define MODEM_RPC_BUFFER_MAX_SIZE               (MODEM_RPC_OVERHEAD_SIZE + MODEM_RPC_PAGE_SIZE * 8)

/* Init modem in DFU/RPC mode.
 * Call once before DFU operation. If the modem goes to a bad state, this can be called again to re-initialize.
 * The root key digest response of the modem is put in the digest_buffer argument
 * If success, modem will be in MODEM_STATE_WAITING_FOR_BOOTLOADER */
modem_err_t modem_dfu_rpc_init(modem_digest_buffer_t * digest_buffer, uint8_t* modem_rpc_buffer, uint32_t modem_rpc_buffer_length);


/* Functions for writing a chunk of data to the modem.
 * Use modem_start_transfer and modem_end_transfer at the start and end of a firmware upload. 
 * See notes 'About data representation' above for a pseudocode example */
modem_err_t modem_write_bootloader_chunk(modem_memory_chunk_t * bootloader_chunk);
modem_err_t modem_write_firmware_chunk(modem_memory_chunk_t * firmware_chunk);
modem_err_t modem_start_transfer(void);
modem_err_t modem_end_transfer(void);

/* Requires modem state MODEM_STATE_READY_FOR_IPC_COMMANDS */
modem_err_t modem_get_memory_hash(uint32_t start_address, uint32_t end_address, modem_digest_buffer_t * digest_buffer);
modem_err_t modem_get_uuid(modem_uuid_t * modem_uuid);

/* Functions for reading modem state.
 * If modem is in MODEM_STATE_BAD, one can check if an fault or error
 * was indicated from the modem */
modem_state_t modem_get_state(void);
bool modem_get_command_error_indicated(void);
bool modem_get_fault_indicated(void);

#endif /*__MODEM_DFU_RPC_H__*/
