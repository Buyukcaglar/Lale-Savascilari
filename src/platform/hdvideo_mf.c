/*
 * hdvideo_mf.c - Windows: video files decoded by Media Foundation
 * (hdvideo.h).  A source reader with video processing delivers every frame
 * as RGB32, whatever the file's codec and colour format.  Decoding runs on
 * the processor: measured here on 3840x2160 H.264 it takes 7-11 ms a frame,
 * while the Direct3D 11 route (decoding on the graphics card, the frame
 * copied back to memory) took 15-30 ms, the copy being the slow part - so
 * that route is not used.  Media Foundation is started once, at the first
 * video, and kept.
 * All calls come from the decoder thread of hdvideo.c; there is never more
 * than one such thread (a video is closed before the next is opened), so
 * the start-up below needs no lock.
 *
 * What plays is what Windows can decode: H.264 always; HEVC and AV1 only
 * with the codec extensions from the Microsoft Store.
 */
#ifdef _WIN32

#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdvideo.h"

#define VIDEO_STREAM ((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM)

struct HdReader {
    IMFSourceReader *reader;
    int  w, h;
    LONG stride;            /* of a contiguous buffer; negative = bottom row first */
    int  com;
};

static int     mf_state;        /* 0 not started, 1 running, -1 not available */
static HRESULT mf_error;

static void start_mf(void)
{
    mf_error = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    mf_state = SUCCEEDED(mf_error) ? 1 : -1;
}

/* Media Foundation is missing on the "N" editions of Windows without the
   Media Feature Pack.  Its two libraries are therefore linked to be loaded at
   their first use (CMakeLists.txt: --delayload), and nothing of them is used
   unless they can be loaded: the program then runs without the 4K videos. */
int hdreader_available(void)
{
    static int have = -1;
    if (have < 0)
        have = LoadLibraryW(L"mfplat.dll") != NULL && LoadLibraryW(L"mfreadwrite.dll") != NULL;
    return have;
}

static int read_format(HdReader *r)
{
    IMFMediaType *t = NULL;
    UINT64 size = 0;
    UINT32 stride = 0;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(r->reader, VIDEO_STREAM, &t)))
        return -1;
    HRESULT hr = IMFMediaType_GetUINT64(t, &MF_MT_FRAME_SIZE, &size);
    r->w = (int)(size >> 32);
    r->h = (int)(UINT32)size;
    if (SUCCEEDED(IMFMediaType_GetUINT32(t, &MF_MT_DEFAULT_STRIDE, &stride)))
        r->stride = (LONG)stride;
    else
        r->stride = r->w * 4;
    IMFMediaType_Release(t);
    return SUCCEEDED(hr) && r->w > 0 && r->h > 0 ? 0 : -1;
}

HdReader *hdreader_open(const char *path, int *w, int *h, char *err, size_t errlen)
{
    wchar_t wpath[1400];
    IMFAttributes *attr = NULL;
    IMFMediaType *type = NULL;
    HRESULT hr;
    HdReader *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (!mf_state)
        start_mf();
    if (mf_state < 0) {
        snprintf(err, errlen, "Media Foundation is not available on this system (%08lx)", (unsigned long)mf_error);
        goto fail;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1400)) {
        snprintf(err, errlen, "bad file name");
        goto fail;
    }
    if (SUCCEEDED(MFCreateAttributes(&attr, 1)))        /* colour conversion to RGB32 in the reader */
        IMFAttributes_SetUINT32(attr, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromURL(wpath, attr, &r->reader);
    if (attr)
        IMFAttributes_Release(attr);
    if (FAILED(hr)) {
        snprintf(err, errlen, "the file cannot be opened as a video (%08lx)", (unsigned long)hr);
        goto fail;
    }
    IMFSourceReader_SetStreamSelection(r->reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(r->reader, VIDEO_STREAM, TRUE);
    hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) {
        IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        hr = IMFSourceReader_SetCurrentMediaType(r->reader, VIDEO_STREAM, NULL, type);
        IMFMediaType_Release(type);
    }
    if (FAILED(hr) || read_format(r) != 0) {
        snprintf(err, errlen, "Windows has no decoder for this video's format (%08lx); H.264 always works",
                 (unsigned long)hr);
        goto fail;
    }
    *w = r->w;
    *h = r->h;
    return r;
fail:
    hdreader_close(r);
    return NULL;
}

void hdreader_close(HdReader *r)
{
    if (!r)
        return;
    if (r->reader)
        IMFSourceReader_Release(r->reader);
    if (r->com)
        CoUninitialize();
    free(r);
}

int hdreader_seek(HdReader *r, double seconds)
{
    static const GUID time_format = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };   /* GUID_NULL: 100 ns units */
    PROPVARIANT pos;
    PropVariantInit(&pos);
    pos.vt = VT_I8;
    pos.hVal.QuadPart = (LONGLONG)(seconds * 1e7);
    return SUCCEEDED(IMFSourceReader_SetCurrentPosition(r->reader, &time_format, &pos)) ? 0 : -1;
}

static void copy_rows(HdReader *r, uint8_t *dst, const BYTE *row0, LONG pitch)
{
    for (int y = 0; y < r->h; y++)
        memcpy(dst + (size_t)y * (size_t)r->w * 4, row0 + (ptrdiff_t)y * pitch, (size_t)r->w * 4);
}

int hdreader_read(HdReader *r, uint8_t *bgrx, double *pts)
{
    for (;;) {
        DWORD flags = 0, index = 0;
        LONGLONG ts = 0;
        IMFSample *sample = NULL;
        IMFMediaBuffer *buf = NULL;
        IMF2DBuffer *buf2d = NULL;
        int ok = 0;
        if (FAILED(IMFSourceReader_ReadSample(r->reader, VIDEO_STREAM, 0, &index, &flags, &ts, &sample)))
            return -1;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            int w = r->w, h = r->h;
            if (read_format(r) != 0 || r->w != w || r->h != h) {    /* the queue's frames have one size */
                if (sample)
                    IMFSample_Release(sample);
                return -1;
            }
        }
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) {
            if (sample)
                IMFSample_Release(sample);
            return flags & MF_SOURCE_READERF_ERROR ? -1 : 0;
        }
        if (!sample)
            continue;                           /* a gap in the stream */
        /* the frame's own layout if the buffer tells it, else the media type's stride */
        if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &buf))
            && SUCCEEDED(IMFMediaBuffer_QueryInterface(buf, &IID_IMF2DBuffer, (void **)&buf2d))) {
            BYTE *row0 = NULL;
            LONG pitch = 0;
            if (SUCCEEDED(IMF2DBuffer_Lock2D(buf2d, &row0, &pitch))) {
                copy_rows(r, bgrx, row0, pitch);
                IMF2DBuffer_Unlock2D(buf2d);
                ok = 1;
            }
            IMF2DBuffer_Release(buf2d);
        }
        if (buf)
            IMFMediaBuffer_Release(buf);
        buf = NULL;
        if (!ok && SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buf))) {
            BYTE *data = NULL;
            DWORD len = 0;
            LONG pitch = r->stride, rowbytes = pitch < 0 ? -pitch : pitch;
            if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &data, NULL, &len))) {
                if (rowbytes >= r->w * 4 && (uint64_t)len >= (uint64_t)rowbytes * (uint64_t)r->h) {
                    copy_rows(r, bgrx, pitch < 0 ? data + (size_t)rowbytes * (size_t)(r->h - 1) : data, pitch);
                    ok = 1;
                }
                IMFMediaBuffer_Unlock(buf);
            }
            IMFMediaBuffer_Release(buf);
        }
        IMFSample_Release(sample);
        if (!ok)
            return -1;
        *pts = (double)ts / 1e7;
        return 1;
    }
}

#endif /* _WIN32 */
