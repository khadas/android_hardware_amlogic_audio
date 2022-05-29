#ifndef _AUDIO_TSYNC_WRAP_H_
#define _AUDIO_TSYNC_WRAP_H_

#include <stdbool.h>

#define TSYNC_FIRSTAPTS "/sys/class/tsync/firstapts"
#define TSYNC_FIRSTVPTS "/sys/class/tsync/firstvpts"
#define TSYNC_PCRSCR    "/sys/class/tsync/pts_pcrscr"
#define TSYNC_EVENT     "/sys/class/tsync/event"
#define TSYNC_APTS      "/sys/class/tsync/pts_audio"
#define TSYNC_VPTS      "/sys/class/tsync/pts_video"
#define TSYNC_ENABLE    "/sys/class/tsync/enable"
#define TSYNC_MODE      "/sys/class/tsync/mode"



void aml_hwsync_wrap_single_set_tsync_init(void);
void aml_hwsync_wrap_single_set_tsync_pause(void);
void aml_hwsync_wrap_single_set_tsync_resume(void);
void aml_hwsync_wrap_single_set_tsync_stop(void);
int aml_hwsync_wrap_single_set_tsync_start_pts(uint32_t pts);
int aml_hwsync_wrap_single_set_tsync_start_pts64(uint64_t pts);
int aml_hwsync_wrap_single_get_tsync_pts(uint32_t *pts);
int aml_hwsync_wrap_single_get_tsync_vpts(uint32_t *pts);
int aml_hwsync_wrap_single_get_tsync_firstvpts(uint32_t *pts);
int aml_hwsync_wrap_single_reset_tsync_pcrscr(uint64_t pts);

int aml_hwsync_tsync_get_pcr(audio_hwsync_t *p_hwsync, uint64_t *value);
int aml_hwsync_get_tsync_pts_by_handle(int fd, uint64_t *pts);
int aml_hwsync_open_tsync(void);
void aml_hwsync_close_tsync(int fd);


#endif
