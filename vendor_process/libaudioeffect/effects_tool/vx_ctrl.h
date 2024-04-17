
#ifndef VIRTUAL_X_CTRL_H_
#define VIRTUAL_X_CTRL_H_

#include <media/AudioEffect.h>

using namespace android;

int set_param_from_cmd_line(int argc, char **argv);

int Virtualx_effect_func(sp<AudioEffect>& gAudioEffect, int paramIndex);

void printf_vx_help(char *name);

//int set_param_for_file_cmd_line(char *line_buf);
int set_param_from_scanf(sp<AudioEffect>& gAudioEffect, int effectIndex);
#endif
