#include "mfu_types.h"

#include <errno.h>
#include <string.h>
#include <sys/byteorder.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(mfu_types, CONFIG_MFU_LOG_LEVEL);

size_t mfu_header_length_get(enum mfu_header_type type)
{
    switch (type) {
#define X(_name, _value, _struct) case _name: return sizeof(struct _struct);
	MFU_HEADER_TYPE_LIST
#undef X
        default:
            return 0;
    }
}

size_t mfu_header_data_length_get(enum mfu_header_type type, void *header)
{
    switch (type) {
#define X(_name, _value, _struct) case _name:\
    return ((struct _struct *) header)->data_length;
	MFU_HEADER_TYPE_LIST
#undef X
        default:
            return 0;
    }
}

int mfu_header_type_get(
	u8_t *data_buf,
	size_t data_buf_size,
	enum mfu_header_type *type)
{
    u32_t decoded;

    if (data_buf_size < MFU_HEADER_MAX_LEN) {
        return -ENODATA;
    }

    decoded = sys_get_le32(&data_buf[0]);
    if (decoded == MFU_HEADERHEADER_MAGIC_VALUE) {
        /* Package header has magic value prefix */
        decoded = sys_get_le32(&data_buf[sizeof(u32_t)]);
    }

    switch (decoded) {
#define X(_name, _value, _struct) case _value: *type = _name; break;
	MFU_HEADER_TYPE_LIST
#undef X
        default:
            return -ENODATA;
    }

    return 0;
}

int mfu_header_decode(
	enum mfu_header_type type,
	u8_t *data_buf,
	size_t data_buf_size,
	void *type_buf,
	size_t type_buf_size)
{
    if (data_buf_size < mfu_header_length_get(type) ||
        type_buf_size < mfu_header_length_get(type)) {
        return -ENOMEM;
    }

	size_t pos = 0;

    switch(type) {
        case MFU_HEADER_TYPE_UNSIGNED_PKG:
        {
            struct mfu_header_type_pkg *ptr = type_buf;

            ptr->magic_value = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->magic_value);
            
            ptr->type = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->type);

            ptr->data_length = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_length);

            memcpy(ptr->package_hash, &data_buf[pos], sizeof(ptr->package_hash));
            pos += sizeof(ptr->package_hash);
        } break;

        case MFU_HEADER_TYPE_BL_SEGMENT:
        {
            struct mfu_header_type_bl_segment *ptr = type_buf;
            
            ptr->type = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->type);

            ptr->data_length = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_length);

            ptr->data_address = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_address);

            ptr->digest = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->digest);
        } break;

        case MFU_HEADER_TYPE_FW_SEGMENT:
        {
            struct mfu_header_type_fw_segment *ptr = type_buf;
            
            ptr->type = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->type);

            ptr->data_length = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_length);

            ptr->data_address = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_address);
        } break;

        case MFU_HEADER_TYPE_FW_SEGMENT_W_DIGEST:
        {
            struct mfu_header_type_fw_segment_w_digest *ptr = type_buf;
            
            ptr->type = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->type);

            ptr->data_length = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_length);

            ptr->data_address = sys_get_le32(&data_buf[pos]);
            pos += sizeof(ptr->data_address);

            memcpy(ptr->plaintext_hash, &data_buf[pos], sizeof(ptr->plaintext_hash));
            pos += sizeof(ptr->plaintext_hash);
        } break;
        default:
            LOG_ERR("Unknown type");
            return -ENODATA;
    }

    return 0;
}