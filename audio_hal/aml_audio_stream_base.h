#ifndef AML_STREAM_COMMON_STRUCT_H_
#define AML_STREAM_COMMON_STRUCT_H_

#include <sys/types.h>

enum streamout_usecase_t {
    STREAM_OUT_EFFECT = (0x1 << 9),
    STREAM_OUT_OTHERS = (0x1 <<  10),
};

/*
Function: get address of member:base pointer from address of xx_out or &xx_out.stream
examples:
    struct xx_out {
        struct audio_stream_out stream; // front member
        aml_streamout_base base; //base member
    };
*/
#define TO_BASE_PTR(front_addr, front_type, base_type) (base_type *)((char*)(front_addr) + sizeof(front_type))

typedef struct aml_streamout_base {
    int32_t common_usecase;
} aml_streamout_base;

#endif
