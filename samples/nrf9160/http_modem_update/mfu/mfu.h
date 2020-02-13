#ifndef __MFU_H__
#define __MFU_H__

#include <zephyr/types.h>
#include <device.h>

/**
 * @brief Initialize Modem Firmware Update state
 * 
 * If the internal state indicates that an update was started but not finished,
 * the update will be automatically resumed/restarted.
 * The update process can take several minutes depending on the underlying
 * flash device used to store the update file.
 *
 * @param flash_dev Flash device where update file is stored
 * @param addr Flash device address where update file is expected
 * @param read_buf Buffer used for flash read.
 * @param buf_size Read buffer size. Must be >= MFU_HEADER_MAX_LEN
 *
 * @retval 0 if successful
 */
int mfu_init(struct device *flash_dev, u32_t addr);

/**
 * @brief Inform MFU that an update is now available
 * 
 * This function only updates the persistent state.
 * Use @ref mfu_update_verify_integrity to validate update integrity.
 * 
 * @retval 0 if successful
 */
int mfu_update_available_set(void);

/**
 * @brief Inform MFU that an update is no longer available
 * 
 * This function only updates the persistent state.
 * 
 * @retval 0 if successful
 * @retval -EINVAL if state is currently MFU_STATE_INSTALLING
 */
int mfu_update_available_clear(void);

/**
 * @brief Check if MFU is marked as update available.
 * 
 * This only checks internal state of MFU. @ref mfu_update_verify_integrity
 * is used to checks the integrity of the full update file.
 * 
 * @retval true if update has been marked as available
 */
bool mfu_update_available_get(void);

/**
 * @brief Verify integrity of entire modem update file.
 * 
 * This can take a while as the update file can be several megabytes in size.
 * The duration depends on the underlying flash device.
 * @ref mfu_update_apply will automatically parform this integrity check.
 *
 * @retval 0 if successful
 * @retval -ENODATA if verification fails
 */
int mfu_update_verify_integrity(void);

/**
 * @brief Initialize stream verification state.
 * 
 * @note When stream verification is done, take care not to use the same flash
 * buffer for both this module and the downloader module.
 */
void mfu_update_verify_stream_init(void);

/**
 * @brief Process a chunk of received modem update file
 * 
 * @param offset Size/offset of data received so far
 * 
 * @retval 0 if successful
 * @retval -EINVAL if the modem update header is invalid
 * @retval -EINVAL if the offset exceeds the size given in header
 */
int mfu_update_verify_stream_process(u32_t offset);

/**
 * @brief Complete the verification of modem update file
 * 
 * This function runs @ref mfu_update_verify_stream_process for the 
 * remaining part of the modem update file (if any) and compares the 
 * generated hash digest with the modem update header.
 * 
 * @retval 0 if successful
 * @retval -EINVAL if the verification fails
 */
int mfu_update_verify_stream_finalize(void);

/**
 * @brief Install modem update file
 * 
 * This process can take several minutes, depending on the underlying flash
 * device. One should make sure the application state will allow being suspended
 * for this period. E.g. ensure sufficient battery and that any running watchdog
 * is fed.
 * 
 * This function must be called before BSD lib is initialized.
 * The modem should also be reset after the update has completed.
 *
 * @retval 0 if successful
 */
int mfu_update_apply(void);

#endif /* __MFU_H__*/
