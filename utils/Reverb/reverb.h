#ifndef REVERB_H
#define REVERB_H

int AML_Reverb_Init(void **reverb_handle);
int AML_Reverb_Process(void *reverb_handle, int16_t *inBuffer, int16_t *outBuffer, int frameCount);
int AML_Reverb_Release(void *reverb_handle);
void AML_Reverb_Set_Mode(void *reverb_handle, unsigned int mode);

#endif

