#ifndef __MFU_STATE_H__
#define __MFU_STATE_H__

enum mfu_state {
    MFU_STATE_NO_UPDATE_AVAILABLE,
    MFU_STATE_UPDATE_AVAILABLE,
    MFU_STATE_INSTALLING,
    MFU_STATE_INSTALL_FINISHED,
};

// struct mfu_state_info {

// }

int mfu_state_init(void *device, int offset);
enum mfu_state mfu_state_get(void);
int mfu_state_set(enum mfu_state new_state);

#endif /* __MFU_STATE_H__*/
