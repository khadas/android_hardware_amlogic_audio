#ifndef __PES_H__
#define __PES_H__
typedef struct PES_HEADER_TAG//size =9
{
        unsigned char packet_start_code_prefix[3];
        unsigned char stream_id;
        unsigned char PES_packet_length[2];

        unsigned char original_or_copy : 1;
        unsigned char copyright : 1;
        unsigned char data_alignment_indicator : 1;
        unsigned char PES_priority : 1;
        unsigned char PES_scrambling_control : 2;
        unsigned char fix_bit : 2;

        unsigned char PES_extension_flag : 1;
        unsigned char PES_CRC_flag : 1;
        unsigned char additional_copy_info_flag : 1;
        unsigned char DSM_trick_mode_flag : 1;
        unsigned char ES_rate_flag : 1;
        unsigned char ESCR_flag : 1;
        unsigned char PTS_DTS_flags : 2;

        unsigned char PES_header_data_length;
} __attribute__((packed))* pPES_HEADER_tag;

typedef struct  PES_EXTENSION_FLAG_TAG //size 1
{
    unsigned char PES_extension_flag_2 : 1;
    unsigned char reserved : 3;
    unsigned char PSTD_buffer_flag : 1;
    unsigned char program_packet_sequence_counter_flag : 1;
    unsigned char pack_header_field_flag : 1;
    unsigned char PES_private_data_flag : 1;

}__attribute__((packed)) PES_extension_header;

typedef struct AD_DESCRIPTOR_TAG //128 bit
{
    unsigned char AD_descriptor_length : 4;//1000
    unsigned char Reserve : 4;//1111
    unsigned char AD_text_tag[5];//0x4454474144
    unsigned char revision_text_tag;//0x31
    unsigned char fade ;              //0xXX
    unsigned char pan ;               //0xYY
    unsigned char Reserved[7];        //0xFFFFFFFFFFFFFF

}__attribute__((packed)) AD_descriptor;


void handlepesheader(unsigned char *buf,int64_t *outpts,unsigned char* pan,unsigned char* fade);
#endif
