
#ifndef AUDIO_DUMMY_STREAM_OUT_H_
#define AUDIO_DUMMY_STREAM_OUT_H_

int adev_open_dummy_output_stream(struct audio_hw_device *dev,
                                    audio_io_handle_t handle,
                                    audio_devices_t devices,
                                    audio_output_flags_t flags,
                                    struct audio_config *config,
                                    struct audio_stream_out **stream_out,
                                    const char *address __unused);


void adev_close_dummy_output_stream(struct audio_hw_device *dev,
                                    struct audio_stream_out *stream);

#endif
