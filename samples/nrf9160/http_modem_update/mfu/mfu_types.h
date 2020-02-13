#ifndef __MFU_TYPES_H__
#define __MFU_TYPES_H__

#include <stddef.h>
#include <zephyr/types.h>

#define MFU_HEADERHEADER_MAGIC_VALUE 0x7FDEB13C

struct mfu_header_type_pkg {
	u32_t magic_value;
	u32_t type;
	u32_t data_length;
	u8_t package_hash[32];
};

struct mfu_header_type_bl_segment {
	u32_t type;
	u32_t data_length;
	u32_t data_address;
	u32_t digest;
};

struct mfu_header_type_fw_segment {
	u32_t type;
	u32_t data_length;
	u32_t data_address;
};

struct mfu_header_type_fw_segment_w_digest {
	u32_t type;
	u32_t data_length;
	u32_t data_address;
	u8_t plaintext_hash[32];
};

/* Type name, type value, type struct */
#define MFU_HEADER_TYPE_LIST \
	X(MFU_HEADER_TYPE_UNSIGNED_PKG, 0x01, mfu_header_type_pkg) \
	X(MFU_HEADER_TYPE_BL_SEGMENT, 0x03, mfu_header_type_bl_segment) \
	X(MFU_HEADER_TYPE_FW_SEGMENT, 0x04, mfu_header_type_fw_segment) \
	X(MFU_HEADER_TYPE_FW_SEGMENT_W_DIGEST, 0x05, mfu_header_type_fw_segment_w_digest)

enum mfu_header_type {
#define X(_name, _value, _structname) _name = _value,
	MFU_HEADER_TYPE_LIST
#undef X
};

union mfu_header_type_sizes {
#define X(_name, _value, _structname) struct _structname _name##_struct;
	MFU_HEADER_TYPE_LIST
#undef X
};

#define MFU_HEADER_MAX_LEN sizeof(union mfu_header_type_sizes)

size_t mfu_header_length_get(enum mfu_header_type type);
size_t mfu_header_data_length_get(enum mfu_header_type type, void *header);

/**
 * @brief Decode header
 * 
 * Buffer size must be minimum @ref 4 bytes,
 * as type is not yet known
 * 
 * @param data_buf Data buffer
 * @param data_buf_size Size of data buffer in bytes
 * @param type Buffer to hold type
 *
 * @retval 0 if successful
 * @retval -ENOMEM if a buffer is too small
 * @retval -ENODATA if there are formatting errors
 */
int mfu_header_type_get(
	u8_t *data_buf,
	size_t data_buf_size,
	enum mfu_header_type *type);

/**
 * @brief Decode header
 * 
 * Data and type buffers can be set to @ref MFU_HEADER_MAX_LEN for convenience.
 * 
 * @param type Header type
 * @param data_buf Data buffer
 * @param data_buf_size Size of data buffer in bytes
 * @param type_buf Type buffer (content format given by type output argument)
 * @param type_buf_size Size of type buffer in bytes
 *
 * @retval 0 if successful
 * @retval -ENOMEM if a buffer is too small
 * @retval -ENODATA if there are formatting errors
 */
int mfu_header_decode(
	enum mfu_header_type type,
	u8_t *data_buf,
	size_t data_buf_size,
	void *type_buf,
	size_t type_buf_size);


#endif /* __MFU_TYPES_H__*/
