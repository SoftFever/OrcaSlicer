#pragma once

#ifdef BAMBU_DYNAMIC
#  define BAMBU_EXPORT
#  define BAMBU_FUNC(x) (*x)
#else
#  ifdef __WIN32__
#    define BAMBU_EXPORT __declspec(dllexport)
#  else
#    define BAMBU_EXPORT
#  endif // __WIN32__
#  define BAMBU_FUNC(x) x
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct Bambu_Session;
#ifdef __WIN32__
typedef wchar_t Bambu_Message;
#else
typedef char Bambu_Message;
#endif
typedef void (*Logger)(Bambu_Session * session, int level, Bambu_Message const * msg);

enum Bambu_StreamType
{
    VIDE,
    AUDI
};

enum Bambu_VideoSubType
{
    AVC1,
};

enum Bambu_AudioSubType
{
    MP4A
};

enum Bambu_FormatType
{
    video_avc_packet,
    video_avc_byte_stream,
    audio_raw,
    audio_adts
};

struct Bambu_StreamInfo
{
    Bambu_StreamType type;
    int sub_type;
    union {
        struct
        {
            int width;
            int height;
            int frame_rate;
        } video;
        struct
        {
            int sample_rate;
            int channel_count;
            int sample_size;
        } audio;
    } format;
    int format_type;
    int format_size;
    unsigned char const * format_buffer;
};

enum Bambu_SampleFlag
{
    f_sync = 1
};

struct Bambu_Sample
{
    int itrack;
    int size;
    int flags;
    unsigned char const * buffer;
    unsigned long long decode_time;
};

enum Bambu_Error
{
    Bambu_success,
    Bambu_stream_end,
    Bambu_would_block
};

struct Bambu_Session
{
    int gSID = -1;
    int avIndex = -1;
    int block = 0;
    int block_next = 0;
    Logger logger = nullptr;
    void * buffer = nullptr;
    int buffer_size = 0;
    void * extra = 0;
    void * dump_file1 = nullptr;
    void * dump_file2 = nullptr;

    void Log(int unused, int level, wchar_t const * format, ...);
};

#ifdef BAMBU_DYNAMIC
struct BambuLib {
#endif

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Open)(Bambu_Session* session, char const* uid);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_StartStream)(Bambu_Session* session);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_GetStreamCount)(Bambu_Session* session);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_GetStreamInfo)(Bambu_Session* session, int index, Bambu_StreamInfo* info);

BAMBU_EXPORT unsigned long BAMBU_FUNC(Bambu_GetDuration)(Bambu_Session* session);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Seek)(Bambu_Session* session, unsigned long time);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_ReadSample)(Bambu_Session* session, Bambu_Sample* sample);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_SendMessage)(Bambu_Session* session, int ctrl, char const* data, int len);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_RecvMessage)(Bambu_Session* session, int* ctrl, char* data, int* len);

BAMBU_EXPORT void BAMBU_FUNC(Bambu_Close)(Bambu_Session* session);

BAMBU_EXPORT int BAMBU_FUNC(Bambu_Init)();

BAMBU_EXPORT void BAMBU_FUNC(Bambu_Deinit)();

BAMBU_EXPORT char const* BAMBU_FUNC(Bambu_GetLastErrorMsg)();

BAMBU_EXPORT void BAMBU_FUNC(Bambu_FreeLogMsg)(Bambu_Message const* msg);

#ifdef BAMBU_DYNAMIC
};
#endif

#ifdef __cplusplus
}
#endif // __cplusplus
