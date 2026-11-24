/*
 * hdvideo_avf.m - macOS: video files decoded by AVFoundation (hdvideo.h).
 * An AVAssetReader delivers the frames of the first video track as 32-bit
 * BGRA pixel buffers (VideoToolbox decodes, in hardware where it can);
 * seeking makes a new reader that starts at the wanted time.  All calls come
 * from the one decoder thread of hdvideo.c.  Compiled with ARC.
 *
 * Not yet run: this file was written on a machine without macOS; it is
 * checked by building and playing a video on a Mac (docs/MACOS.md).
 */
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdvideo.h"

struct HdReader {
    void *asset, *track, *reader, *output;      /* retained Objective-C objects */
    int   w, h;
};

int hdreader_available(void) { return 1; }

static void drop(void **p)
{
    if (*p)
        CFRelease(*p);
    *p = NULL;
}

/* A reader that starts at `seconds`. */
static int start_reader(HdReader *r, double seconds, char *err, size_t errlen)
{
    @autoreleasepool {
        AVAsset *asset = (__bridge AVAsset *)r->asset;
        AVAssetTrack *track = (__bridge AVAssetTrack *)r->track;
        NSError *e = nil;
        if (r->reader)
            [(__bridge AVAssetReader *)r->reader cancelReading];
        drop(&r->output);
        drop(&r->reader);
        AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset error:&e];
        if (!reader) {
            if (err)
                snprintf(err, errlen, "%s", e.localizedDescription.UTF8String ?: "cannot read the video");
            return -1;
        }
        NSDictionary *settings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        AVAssetReaderTrackOutput *output =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
        output.alwaysCopiesSampleData = NO;
        if (![reader canAddOutput:output]) {
            if (err)
                snprintf(err, errlen, "macOS has no decoder for this video's format");
            return -1;
        }
        [reader addOutput:output];
        if (seconds > 0.0)
            reader.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(seconds, 90000), kCMTimePositiveInfinity);
        if (![reader startReading]) {
            if (err)
                snprintf(err, errlen, "%s", reader.error.localizedDescription.UTF8String ?: "cannot decode the video");
            return -1;
        }
        r->reader = (void *)CFBridgingRetain(reader);
        r->output = (void *)CFBridgingRetain(output);
        return 0;
    }
}

HdReader *hdreader_open(const char *path, int *w, int *h, char *err, size_t errlen)
{
    HdReader *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"    /* the synchronous calls: this is a worker thread */
        AVAssetTrack *track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        CGSize size = track ? track.naturalSize : CGSizeZero;
#pragma clang diagnostic pop
        if (!track || size.width < 1 || size.height < 1) {
            snprintf(err, errlen, "no video track in the file");
            free(r);
            return NULL;
        }
        r->asset = (void *)CFBridgingRetain(asset);
        r->track = (void *)CFBridgingRetain(track);
        r->w = (int)size.width;
        r->h = (int)size.height;
    }
    if (start_reader(r, 0.0, err, errlen) != 0) {
        hdreader_close(r);
        return NULL;
    }
    *w = r->w;
    *h = r->h;
    return r;
}

void hdreader_close(HdReader *r)
{
    if (!r)
        return;
    if (r->reader)
        [(__bridge AVAssetReader *)r->reader cancelReading];
    drop(&r->output);
    drop(&r->reader);
    drop(&r->track);
    drop(&r->asset);
    free(r);
}

int hdreader_seek(HdReader *r, double seconds)
{
    return start_reader(r, seconds, NULL, 0);
}

int hdreader_read(HdReader *r, uint8_t *bgrx, double *pts)
{
    @autoreleasepool {
        AVAssetReader *reader = (__bridge AVAssetReader *)r->reader;
        AVAssetReaderTrackOutput *output = (__bridge AVAssetReaderTrackOutput *)r->output;
        for (;;) {
            CMSampleBufferRef sb = [output copyNextSampleBuffer];
            if (!sb)
                return reader.status == AVAssetReaderStatusCompleted ? 0 : -1;
            CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
            if (!img) {                         /* a sample without a picture */
                CFRelease(sb);
                continue;
            }
            int ok = 0;
            CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
            const uint8_t *base = CVPixelBufferGetBaseAddress(img);
            size_t pitch = CVPixelBufferGetBytesPerRow(img);
            int w = (int)CVPixelBufferGetWidth(img), h = (int)CVPixelBufferGetHeight(img);
            if (base && w == r->w && h == r->h) {
                for (int y = 0; y < h; y++)
                    memcpy(bgrx + (size_t)y * (size_t)w * 4, base + (size_t)y * pitch, (size_t)w * 4);
                ok = 1;
            }
            CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
            *pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
            CFRelease(sb);
            return ok ? 1 : -1;
        }
    }
}
