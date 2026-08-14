#ifndef RTSP_SYNC_H
#define RTSP_SYNC_H

#include "util.h"

typedef struct _RtspSyncGeneration RtspSyncGeneration;

typedef gboolean (*RtspSyncCancelFunc)(gpointer user_data);

RtspSyncGeneration *rtsp_sync_generation_new(guint64 id, gpointer data,
                                              GDestroyNotify destroy_data);
RtspSyncGeneration *rtsp_sync_generation_ref(
    RtspSyncGeneration *generation);
void rtsp_sync_generation_unref(RtspSyncGeneration *generation);
void rtsp_sync_generation_activate(RtspSyncGeneration *generation);
void rtsp_sync_generation_deactivate(RtspSyncGeneration *generation);
gboolean rtsp_sync_generation_is_active(
    const RtspSyncGeneration *generation);
guint64 rtsp_sync_generation_id(const RtspSyncGeneration *generation);
gpointer rtsp_sync_generation_data(const RtspSyncGeneration *generation);

/* Returns TRUE only when the full delay elapsed. A generation deactivation or
 * external cancellation ends the wait within slice_us. */
gboolean rtsp_sync_generation_wait(RtspSyncGeneration *generation,
                                   gint64 duration_us, gint64 slice_us,
                                   RtspSyncCancelFunc cancelled,
                                   gpointer user_data);

guint rtsp_frame_id_rate_for_channel(const CmdArg *arg, guint channel);
gboolean rtsp_frame_id_from_pts(GstClockTime origin_pts, guint fps,
                                guint64 *frame_id);
gboolean rtsp_frame_id_from_caps_pts(const GstCaps *caps,
                                     GstClockTime origin_pts,
                                     guint64 *frame_id);
gboolean rtsp_h265_au_caps_compatible(const CmdArg *arg, guint channel,
                                      const GstCaps *caps);
gboolean rtsp_h265_annex_b_au_buffer(GstBuffer *buffer);
gboolean rtsp_frame_id_insertion_allowed(const CmdArg *arg, guint channel,
                                         const GstCaps *caps);
GstCaps *rtsp_media_fallback_caps(guint channel, const gchar *encoder);

#endif /* RTSP_SYNC_H */
