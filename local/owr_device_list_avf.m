/*/
\*\ OwrDeviceListAVF
/*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "owr_media_source.h"
#include "owr_local_media_source_private.h"
#include "owr_device_list_private.h"

#include <TargetConditionals.h>

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

typedef OwrLocalMediaSource *(^DeviceMapFunc)(AVCaptureDevice *, gint);

static GList *generate_source_device_list(NSString *mediaType, DeviceMapFunc map_func)
{
    NSArray *devices;
    GList *list = NULL;
    gint index = 0;

    devices = [AVCaptureDevice devicesWithMediaType:mediaType];

    for (AVCaptureDevice *av_device in devices) {
        list = g_list_prepend(list, map_func(av_device, index));
        ++index;
    }

    return g_list_reverse(list);
}

GList *_get_avf_video_sources()
{
    return generate_source_device_list(AVMediaTypeVideo,
            ^(AVCaptureDevice *av_device, gint index) {
        OwrLocalMediaSource *source;
        const gchar *name;

        name = [av_device.localizedName UTF8String];

        source = _owr_local_media_source_new(index, name,
            OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_CAPTURE);

        return source;
    });
}

GList *_get_avf_audio_sources()
{
    return generate_source_device_list(AVMediaTypeAudio,
            ^(AVCaptureDevice *av_device, gint index) {
        OwrLocalMediaSource *source;
        const gchar *name;

        name = [av_device.localizedName UTF8String];

        source = _owr_local_media_source_new(index, name,
            OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);

        return source;
    });
}
