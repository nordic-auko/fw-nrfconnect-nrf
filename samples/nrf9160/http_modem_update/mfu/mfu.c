#include "mfu.h"
#include "mfu_types.h"
#include "mfu_state.h"

#include <string.h>
#include <drivers/flash.h>
#include <sys/byteorder.h>
#include <logging/log.h>

#include "modem_dfu_rpc.h"

#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
#include "mbedtls/sha256.h"
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>
#endif

LOG_MODULE_REGISTER(mfu, CONFIG_MFU_LOG_LEVEL);

struct mfu_verify_stream_state {
	u32_t total_len;
	u32_t processed;
	u8_t header_sha256[32];
#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
	struct mbedtls_sha256_context sha_ctx;
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
	struct tc_sha256_state_struct sha_ctx;
#endif
};

#if CONFIG_MFU_STREAM_VERIFICATION_ENABLE
static struct mfu_verify_stream_state stream_state;
#endif

static bool update_found;
static bool update_verified;

static struct device *device;
static u32_t dev_offset;

static u8_t flash_read_buf[CONFIG_MFU_FLASH_BUF_SIZE];

/* TODO: get this buffer as an argument from outside */
static u8_t modem_rpc_buf[MODEM_RPC_BUFFER_MIN_SIZE];

BUILD_ASSERT(sizeof(flash_read_buf) >= MFU_HEADER_MAX_LEN);

static void mfu_sha_init(void *ctx)
{
#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
	mbedtls_sha256_init(ctx);
	int err = mbedtls_sha256_starts_ret(ctx, 0);
	__ASSERT_NO_MSG(err == 0);
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
	int err = tc_sha256_init(ctx);
	__ASSERT_NO_MSG(err == TC_CRYPTO_SUCCESS);
#endif
}

static int mfu_sha_process(void *ctx, u8_t *input, size_t input_len)
{
#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
	int err = mbedtls_sha256_update_ret(ctx, input, input_len);
	if (err) {
		return -EINVAL;
	} else {
		return 0;
	}
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
	int err = tc_sha256_update(ctx, input, input_len);
	if (err != TC_CRYPTO_SUCCESS) {
		return -EINVAL;
	} else {
		return 0;
	}
#endif

	return -EINVAL;
}

static int mfu_sha_finalize(void *ctx, u8_t digest[32])
{
#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
	int err = mbedtls_sha256_finish_ret(ctx, digest);
	if (err) {
		return -EINVAL;
	} else {
		return 0;
	}
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
	int err = tc_sha256_final(digest, ctx);
	if (err != TC_CRYPTO_SUCCESS) {
		return -EINVAL;
	} else {
		return 0;
	}
#endif

	return -EINVAL;
}

static int mfu_headers_iterate(
	enum mfu_header_type *header_type,
	void *header,
	size_t header_size,
	u32_t *offset)
{
	int err;
	u32_t data_length;
	u8_t read_buf[MFU_HEADER_MAX_LEN];
	u32_t read_offset;
	enum mfu_header_type next_type;

	if (*header_type == MFU_HEADER_TYPE_UNSIGNED_PKG) {
		/* Package headers have data length covering entire update */
		/* The purpose here is to iterate over headers within update */
		data_length = 0;
	} else {
		data_length = mfu_header_data_length_get(*header_type, header);
	}

	read_offset = *offset + mfu_header_length_get(*header_type) + data_length;

	err = flash_read(device, read_offset, read_buf, sizeof(read_buf));
	if (err) {
		LOG_ERR("flash_read: %d", err);
		return err;
	}

	err = mfu_header_type_get(read_buf, sizeof(read_buf), &next_type);
	if (err) {
		return err;
	}

	err = mfu_header_decode(
		next_type,
		read_buf,
		sizeof(read_buf),
		header,
		header_size);
	if (err) {
		return err;
	}

	*header_type = next_type;
	*offset = read_offset;

	return 0;
}

static int mfu_package_header_get(struct mfu_header_type_pkg *pkg)
{
	enum mfu_header_type type;
	u8_t header_read_buf[MFU_HEADER_MAX_LEN];
	int err;

	err = flash_read(device, dev_offset, header_read_buf, sizeof(header_read_buf));
	if (err) {
		return err;
	}

	err = mfu_header_type_get(header_read_buf, sizeof(header_read_buf), &type);
	if (err) {
		LOG_ERR("mfu_header_type_get: %d", err);
		return err;
	}

	if (type != MFU_HEADER_TYPE_UNSIGNED_PKG) {
		LOG_ERR("Type error: %d", type);
		return -ENODATA;
	}

	err = mfu_header_decode(
		type,
		header_read_buf,
		sizeof(header_read_buf),
		pkg,
		sizeof(*pkg));
	if (err) {
		LOG_ERR("mfu_header_decode: %d", err);
		return err;
	}

	return 0;
}

static int mfu_bl_segment_header_get(
	struct mfu_header_type_bl_segment *bl_segment_header)
{
	enum mfu_header_type type = MFU_HEADER_TYPE_UNSIGNED_PKG;
	u8_t header_read_buf[MFU_HEADER_MAX_LEN];
	u32_t offset = dev_offset;
	int err;

	/* Iterate over all headers and return first instance of BL segment */
	do {
		err = mfu_headers_iterate(&type, header_read_buf, sizeof(header_read_buf), &offset);
		if (!err) {
			switch (type) {
				case MFU_HEADER_TYPE_BL_SEGMENT:
					return mfu_header_decode(
						MFU_HEADER_TYPE_BL_SEGMENT,
						header_read_buf,
						sizeof(header_read_buf),
						bl_segment_header,
						sizeof(*bl_segment_header));
				default:
					break;
			}
		}
	} while (!err);

	return -ENODATA;
}

static int mfu_modem_dfu_program_bootloader(
	struct mfu_header_type_bl_segment *header,
	u32_t bl_data_offset)
{
	modem_err_t modem_err;
	int err;

	LOG_INF("mfu_modem_dfu_program_bootloader");

	modem_err = modem_start_transfer();
	if (modem_err != MODEM_SUCCESS) {
		LOG_ERR("modem_start_transfer: %d", modem_err);
		return -ENODEV;
	}

	u32_t bytes_read = 0;
	modem_memory_chunk_t bootloader_chunk = {
		.target_address = 0, /* Not used with bootloader firmware */
    	.data = flash_read_buf,
		.data_len = 0,
	};

	do
	{
		u32_t read_length;
		u32_t read_offset;

		read_offset = dev_offset + bl_data_offset + bytes_read;
		read_length = (header->data_length - bytes_read);
		if (read_length > sizeof(flash_read_buf)) {
			read_length = sizeof(flash_read_buf);
		}

		LOG_INF("Read %d bytes @ 0x%08X", read_length, read_offset);
		err = flash_read(device, read_offset, flash_read_buf, read_length);
		if (err) {
			LOG_ERR("flash_read: %d", err);
			return err;
		}

		bootloader_chunk.data_len = read_length;
		modem_err = modem_write_bootloader_chunk(&bootloader_chunk);
		if (modem_err != MODEM_SUCCESS) {
			LOG_ERR("modem_write_bootloader_chunk(%d / %d): %d",
				bytes_read, header->data_length, modem_err);
			return -EINVAL;
		}

		bytes_read += read_length;
	} while (bytes_read != header->data_length);

	modem_err = modem_end_transfer();
	if (modem_err != MODEM_SUCCESS) {
		LOG_ERR("modem_end_transfer: %d", modem_err);
		return -ENODEV;
	}

	return 0;
}

static int mfu_modem_dfu_program_fw(
	bool has_digest,
	void *header,
	u32_t fw_data_offset)
{
	modem_err_t modem_err;
	u32_t fw_offset;
	u32_t fw_length;
	u8_t *fw_hash;
	int err;

	LOG_INF("mfu_modem_dfu_program_fw(%d)", has_digest);

	if (has_digest) {
		struct mfu_header_type_fw_segment_w_digest *type_header = header;

		fw_offset = type_header->data_address;
		fw_length = type_header->data_length;
		fw_hash = type_header->plaintext_hash;
	} else {
		struct mfu_header_type_fw_segment *type_header = header;

		fw_offset = type_header->data_address;
		fw_length = type_header->data_length;
		fw_hash = NULL;
	}

	// if (has_digest) {
	// 	modem_digest_buffer_t digest_buffer;
	// 	modem_err = modem_get_memory_hash(fw_offset, fw_offset + fw_length - 1, &digest_buffer);
	// 	if (modem_err) {
	// 		LOG_ERR("modem_get_memory_hash: %d", modem_err);
	// 	} else {
	// 		int pos = 0;
	// 		u8_t *sha_digest = digest_buffer.data;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 		printk("%02X-%02X-%02X-%02X\n", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
	// 		pos += 4;
	// 	}
	// }

	modem_err = modem_start_transfer();
	if (modem_err != MODEM_SUCCESS) {
		LOG_ERR("modem_start_transfer: %d", modem_err);
		return -ENODEV;
	}

	u32_t bytes_read = 0;
	modem_memory_chunk_t firmware_chunk = {
		.target_address = fw_offset,
    	.data = flash_read_buf,
		.data_len = 0,
	};

// #if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
// 	struct mbedtls_sha256_context sha_ctx;
// #elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
// 	struct tc_sha256_state_struct sha_ctx;
// #endif

// 	mfu_sha_init(&sha_ctx);

	do
	{
		u32_t read_length;
		u32_t read_offset;

		read_offset = dev_offset + fw_data_offset + bytes_read;
		read_length = (fw_length - bytes_read);
		if (read_length > sizeof(flash_read_buf)) {
			read_length = sizeof(flash_read_buf);
		}

		LOG_INF("Read %d bytes @ 0x%08X", read_length, read_offset);
		err = flash_read(device, read_offset, flash_read_buf, read_length);
		if (err) {
			LOG_ERR("flash_read: %d", err);
			return err;
		}

	// err = mfu_sha_process(&sha_ctx, flash_read_buf, read_length);
	// if (err) {
	// 	LOG_ERR("mfu_sha_process: %d", err);
	// 	return err;
	// }

		firmware_chunk.data_len = read_length;
		modem_err = modem_write_firmware_chunk(&firmware_chunk);
		if (modem_err != MODEM_SUCCESS) {
			LOG_ERR("modem_write_firmware_chunk(%d / %d): %d",
				bytes_read, fw_length, modem_err);
			LOG_ERR("NRF_IPC_NS->GPMEM[1]: 0x%08X",
				NRF_IPC_NS->GPMEM[1]);
			// u8_t sha_digest[32];
			// err = mfu_sha_finalize(&sha_ctx, sha_digest);
			// if (err) {
			// 	LOG_ERR("mfu_sha_finalize: %d", err);
			// 	return err;
			// }
			// int pos = 0;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			// printk("%02X-%02X-%02X-%02X\n", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
			// pos += 4;
			return -EINVAL;
		}

		firmware_chunk.target_address += read_length;
		bytes_read += read_length;
	} while (bytes_read != fw_length);

	modem_err = modem_end_transfer();
	if (modem_err != MODEM_SUCCESS) {
		LOG_ERR("modem_start_transfer: %d", modem_err);
		return -ENODEV;
	}

	if (has_digest) {
		modem_digest_buffer_t digest_buffer;

		/* Address convention is "up to and including" for end address */
		modem_err = modem_get_memory_hash(
			fw_offset,
			fw_offset + fw_length - 1,
			&digest_buffer);
		if (modem_err != MODEM_SUCCESS) {
			LOG_ERR("modem_get_memory_hash: %d", modem_err);
			return -ENODEV;
		}
		printk("Modem SHA256 for 0x%08X - 0x%08X:\n", fw_offset, (fw_offset + fw_length - 1));
		int pos = 0;
		u8_t *sha_digest = digest_buffer.data;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X-", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
		printk("%02X-%02X-%02X-%02X\n", sha_digest[pos+0], sha_digest[pos+1], sha_digest[pos+2], sha_digest[pos+3]);
		pos += 4;
	}

	return 0;
}

int mfu_init(struct device *flash_dev, u32_t addr)
{
	bool resume_update;
	int err;

	update_found = false;
	update_verified = false;

	if (!flash_dev) {
		return -ENODEV;
	}

	device = flash_dev;
	dev_offset = addr;

	err = mfu_state_init(NULL, 0);
	if (err) {
		return err;
	}

	resume_update = false;

	switch (mfu_state_get()) {
		case MFU_STATE_NO_UPDATE_AVAILABLE:
		case MFU_STATE_UPDATE_AVAILABLE:
		case MFU_STATE_INSTALL_FINISHED:
			/* No action needed */
			break;
		case MFU_STATE_INSTALLING:
			/* Update was started but not marked as finished: apply again */
			resume_update = true;
			break;
		default:
			return -EINVAL;
	}

	if (resume_update) {
		err = mfu_update_apply();
		if (err) {
			return err;
		}
	}

	return 0;
}

int mfu_update_available_set(void)
{
	switch (mfu_state_get()) {
		case MFU_STATE_NO_UPDATE_AVAILABLE:
		case MFU_STATE_INSTALL_FINISHED:
			return mfu_state_set(MFU_STATE_UPDATE_AVAILABLE);
		case MFU_STATE_UPDATE_AVAILABLE:
		case MFU_STATE_INSTALLING:
			return 0;
		default:
			return -EINVAL;
	}
}

int mfu_update_available_clear(void)
{
	switch (mfu_state_get()) {
		case MFU_STATE_INSTALLING:
			return -EINVAL;
		default:
			return mfu_state_set(MFU_STATE_UPDATE_AVAILABLE);
	}
}

bool mfu_update_available_get(void)
{
	switch (mfu_state_get()) {
		case MFU_STATE_UPDATE_AVAILABLE:
			return true;
		default:
			return false;
	}
}

int mfu_update_verify_integrity(void)
{
	u8_t sha_digest[32];
	u8_t sha_buf[offsetof(struct mfu_header_type_pkg, package_hash)];
	struct mfu_header_type_pkg pkg_header;
	int err;
	size_t pos;
	
	err = mfu_package_header_get(&pkg_header);
	if (err) {
		return -ENODATA;
	}

	pos = 0;
	sys_put_le32(pkg_header.magic_value, &sha_buf[pos]);
	pos += sizeof(u32_t);
	sys_put_le32(pkg_header.type, &sha_buf[pos]);
	pos += sizeof(u32_t);
	sys_put_le32(pkg_header.data_length, &sha_buf[pos]);
	pos += sizeof(u32_t);

#if CONFIG_MFU_SHA256_BACKEND_MBEDTLS
	struct mbedtls_sha256_context sha_ctx;
#elif CONFIG_MFU_SHA256_BACKEND_TINYCRYPT
	struct tc_sha256_state_struct sha_ctx;
#endif

	mfu_sha_init(&sha_ctx);
	err = mfu_sha_process(&sha_ctx, sha_buf, sizeof(sha_buf));
	if (err) {
		LOG_ERR("mfu_sha_process: %d", err);
		return err;
	}

	size_t bytes_read = 0;
	u32_t read_offset = dev_offset + mfu_header_length_get(MFU_HEADER_TYPE_UNSIGNED_PKG);

	for (int i = 0; i < pkg_header.data_length; i += sizeof(flash_read_buf)) {
		u32_t read_length;

		if ((bytes_read + sizeof(flash_read_buf)) <= pkg_header.data_length) {
			read_length = sizeof(flash_read_buf);
		} else {
			read_length = pkg_header.data_length - bytes_read;
		}

		err = flash_read(device, read_offset, flash_read_buf, read_length);
		if (err) {
			return err;
		}

		err = mfu_sha_process(&sha_ctx, flash_read_buf, read_length);
		if (err) {
			LOG_ERR("mfu_sha_process: %d", err);
			return err;
		}

		bytes_read += read_length;
		read_offset += read_length;
	}

	err = mfu_sha_finalize(&sha_ctx, sha_digest);
	if (err) {
		LOG_ERR("mfu_sha_finalize: %d", err);
		return err;
	}

	if (memcmp(sha_digest, pkg_header.package_hash, sizeof(sha_digest)) == 0) {
		update_verified = true;
	} else {
		update_verified = false;
	}

	return update_verified ? 0 : -ENODATA;
}

#if CONFIG_MFU_STREAM_VERIFICATION_ENABLE
void mfu_update_verify_stream_init(void)
{
	memset(&stream_state, 0, sizeof(stream_state));
	mfu_sha_init(&stream_state.sha_ctx);
}

int mfu_update_verify_stream_process(u32_t offset)
{
	int err;

	// LOG_INF("mfu_update_verify_stream_process(%d)", offset);

	if (offset < mfu_header_length_get(MFU_HEADER_TYPE_UNSIGNED_PKG)) {
		return 0;
	}


	if (stream_state.processed == 0) {
		u8_t sha_buf[offsetof(struct mfu_header_type_pkg, package_hash)];
		struct mfu_header_type_pkg pkg_header;
		size_t pos;
		
		err = mfu_package_header_get(&pkg_header);
		if (err) {
			return -ENODATA;
		}

		stream_state.total_len =
			pkg_header.data_length +
			mfu_header_length_get(MFU_HEADER_TYPE_UNSIGNED_PKG);

		memcpy(
			stream_state.header_sha256,
			pkg_header.package_hash,
			sizeof(stream_state.header_sha256));

		pos = 0;
		sys_put_le32(pkg_header.magic_value, &sha_buf[pos]);
		pos += sizeof(u32_t);
		sys_put_le32(pkg_header.type, &sha_buf[pos]);
		pos += sizeof(u32_t);
		sys_put_le32(pkg_header.data_length, &sha_buf[pos]);
		pos += sizeof(u32_t);

		// LOG_INF("pkg_header.data_length: %d", pkg_header.data_length);

		stream_state.processed =
			mfu_header_length_get(MFU_HEADER_TYPE_UNSIGNED_PKG);

		err = mfu_sha_process(&stream_state.sha_ctx, sha_buf, sizeof(sha_buf));
		if (err) {
			LOG_ERR("mfu_sha_process: %d", err);
			return err;
		}
	}

	while (stream_state.processed < offset) {
		u32_t read_offset;
		u32_t read_length;

		read_offset = dev_offset + stream_state.processed;
		read_length = (offset - stream_state.processed);
		if (read_length > sizeof(flash_read_buf)) {
			read_length = sizeof(flash_read_buf);
		}

		// LOG_INF("Read %d @ 0x%08X", read_length, read_offset);

		if (stream_state.total_len < (stream_state.processed + read_length)) {
			LOG_ERR("Over read");
			return -EINVAL;
		}

		err = flash_read(device, read_offset, flash_read_buf, read_length);
		if (err) {
			LOG_ERR("flash_read: %d", err);
			return err;
		}

		err = mfu_sha_process(&stream_state.sha_ctx, flash_read_buf, read_length);
		if (err) {
			LOG_ERR("mfu_sha_process: %d", err);
			return err;
		}

		stream_state.processed += read_length;
	}

	return 0;
}

int mfu_update_verify_stream_finalize(void)
{
	u8_t sha_digest[32];
	int err;

	if (stream_state.processed != stream_state.total_len) {
		err = mfu_update_verify_stream_process(stream_state.total_len);
		if (err) {
			LOG_ERR("mfu_update_verify_stream_process: %d", err);
			return err;
		}
	}

	err = mfu_sha_finalize(&stream_state.sha_ctx, sha_digest);
	if (err) {
		LOG_ERR("mfu_sha_finalize: %d", err);
		return err;
	}

	if (memcmp(sha_digest, stream_state.header_sha256, sizeof(sha_digest)) == 0) {
		return 0;
	} else {
		return -EINVAL;
	}
}
#endif /* CONFIG_MFU_STREAM_VERIFICATION_ENABLE */

int mfu_update_apply(void)
{
	int err;
	bool update_abort_possible;
	modem_err_t modem_err;
	modem_state_t modem_state;
	modem_digest_buffer_t modem_digest;
	u32_t modem_digest_snippet;
	struct mfu_header_type_bl_segment bl_segment_header;

	err = mfu_update_verify_integrity();
	if (err) {
		return err;
	}

	err = mfu_bl_segment_header_get(&bl_segment_header);
	if (err) {
		LOG_ERR("mfu_bl_segment_header_get: %d", err);
		return err;
	}

	modem_err = modem_dfu_rpc_init(&modem_digest, modem_rpc_buf, sizeof(modem_rpc_buf));
	if (modem_err != MODEM_SUCCESS) {
		LOG_ERR("modem_dfu_rpc_init: %d", modem_err);
		return -ENODEV;
	}

	/* Only 28 bits from modem_digest is used for validation at this point */
	modem_digest_snippet =
		((modem_digest.data[0] << 20) & 0x0FF00000) |
		((modem_digest.data[1] << 12) & 0x000FF000) |
		((modem_digest.data[2] << 4)  & 0x00000FF0) |
		((modem_digest.data[3] >> 4)  & 0x0000000F);
	
	if (modem_digest_snippet != bl_segment_header.digest) {
		LOG_ERR("Bootloader mismatch: expected 0x%08X, got 0x%08X",
			modem_digest_snippet,
			bl_segment_header.digest);
		return -ENODATA;
	}

	err = mfu_state_set(MFU_STATE_INSTALLING);
	if (err) {
		return err;
	}

	/* Update can be aborted until firmware chunks are written */
	update_abort_possible = true;

	enum mfu_header_type type = MFU_HEADER_TYPE_UNSIGNED_PKG;
	u8_t header_buf[MFU_HEADER_MAX_LEN];
	u32_t offset = dev_offset;

	do {
		err = mfu_headers_iterate(&type, header_buf, sizeof(header_buf), &offset);
		if (!err) {
			switch (type) {
				case MFU_HEADER_TYPE_BL_SEGMENT:
					LOG_INF("MFU_HEADER_TYPE_BL_SEGMENT");
					err = mfu_modem_dfu_program_bootloader(
					(struct mfu_header_type_bl_segment *) header_buf,
						offset + mfu_header_length_get(MFU_HEADER_TYPE_BL_SEGMENT));
					if (err) {
						goto abort;
					}

					modem_state = modem_get_state();
					if (modem_state != MODEM_STATE_READY_FOR_IPC_COMMANDS) {
						LOG_ERR("Unexpected state: %d", modem_state);
						goto abort;
					}
					break;
				case MFU_HEADER_TYPE_FW_SEGMENT:
					LOG_INF("MFU_HEADER_TYPE_FW_SEGMENT");
					update_abort_possible = false;
					err = mfu_modem_dfu_program_fw(
						false,
						header_buf,
						offset + mfu_header_length_get(MFU_HEADER_TYPE_FW_SEGMENT));
					if (err) {
						goto abort;
					}
					break;
				case MFU_HEADER_TYPE_FW_SEGMENT_W_DIGEST:
					LOG_INF("MFU_HEADER_TYPE_FW_SEGMENT_W_DIGEST");
					update_abort_possible = false;
					err = mfu_modem_dfu_program_fw(
						true,
						header_buf,
						offset + mfu_header_length_get(MFU_HEADER_TYPE_FW_SEGMENT_W_DIGEST));
					if (err) {
						goto abort;
					}
					break;
				case MFU_HEADER_TYPE_UNSIGNED_PKG:
					break;
				default:
					LOG_ERR("ERROR INVALID TYPE");
					break;
			}
		}
	} while (!err);

	err = mfu_state_set(MFU_STATE_INSTALL_FINISHED);
	if (err) {
		goto abort;
	}

	return 0;

abort:
	if (update_abort_possible) {
		/* Clean up */
	}

	LOG_WRN("TODO: Remove state clearing in case of error");
	mfu_state_set(MFU_STATE_INSTALL_FINISHED);

	return -ENODEV;
}
