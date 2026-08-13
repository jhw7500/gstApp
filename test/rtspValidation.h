#ifndef RTSP_VALIDATION_H
#define RTSP_VALIDATION_H

#include <glib.h>

typedef struct
{
    guint8 version;
    guint8 channel;
    guint64 frame_id;
    guint64 origin_pts;
} RtspFrameIdRecord;

typedef enum
{
    RTSP_FRAME_ID_PARSE_OK = 0,
    RTSP_FRAME_ID_PARSE_NOT_FRAME,
    RTSP_FRAME_ID_PARSE_NOT_FOUND,
    RTSP_FRAME_ID_PARSE_MALFORMED,
    RTSP_FRAME_ID_PARSE_WRONG_IDENTITY,
    RTSP_FRAME_ID_PARSE_WRONG_VERSION,
    RTSP_FRAME_ID_PARSE_WRONG_CHANNEL
} RtspFrameIdParseStatus;

gboolean rtsp_h265_au_contains_idr(const guint8 *data, gsize size,
                                   gboolean *contains_idr);

RtspFrameIdParseStatus rtsp_frame_id_parse_au(
    const guint8 *data, gsize size, guint8 expected_channel,
    RtspFrameIdRecord *record);

typedef enum
{
    RTSP_FRAME_ORDER_FIRST = 0,
    RTSP_FRAME_ORDER_NEXT,
    RTSP_FRAME_ORDER_GAP,
    RTSP_FRAME_ORDER_DUPLICATE,
    RTSP_FRAME_ORDER_BACKWARD
} RtspFrameOrder;

RtspFrameOrder rtsp_frame_order_check(gboolean has_previous,
                                      guint64 previous, guint64 current);

gboolean rtsp_frame_sync_sei_validation_success(
    gboolean expect_sei, guint64 accepted_frames, guint64 missing_sei,
    guint64 invalid_sei, guint64 duplicates, guint64 backwards);

typedef struct
{
    gboolean expect_sei;
    gboolean expect_gap;
    gboolean allow_startup_loss;
    guint64 accepted_frames;
    guint64 missing_sei;
    guint64 invalid_sei;
    guint64 duplicates;
    guint64 backwards;
    guint64 gap_events;
    guint64 groups_after_gap;
    guint64 startup_missing_sei;
    guint64 startup_gap_events;
} RtspFrameSyncChannelValidation;

gboolean rtsp_frame_sync_channel_validation_success(
    const RtspFrameSyncChannelValidation *validation);

gboolean rtsp_validation_stop_is_expected(const gchar *stop_reason,
                                           gboolean fatal_bus,
                                           gboolean eos_seen,
                                           gboolean expect_eos);

typedef struct
{
    gboolean gap_observed;
    gboolean decoded_before_gap;
    gint64 gap_previous_decoded_ns;
    gint64 observed_gap_ns;
    guint64 recovery_idr_count;
    guint64 decoded_after_recovery_idr;
    guint64 recovery_idr_pts;
} RtspDecoderRecoveryWindow;

gboolean rtsp_decoder_recovery_observe_gap(
    RtspDecoderRecoveryWindow *window, guint64 decoded_samples,
    gint64 previous_decoded_ns, gint64 decoded_ns,
    gint64 expected_gap_ns);

typedef struct
{
    guint64 decoded_samples;
    guint64 idr_count;
    guint64 recovery_idr_count;
    guint64 decoded_after_recovery_idr;
    gboolean fatal_bus;
    gboolean eos_seen;
    gboolean expect_eos;
    gboolean expect_recovery;
    gboolean decoded_before_gap;
    gboolean gap_observed;
    gint64 expected_gap_ns;
    gint64 observed_gap_ns;
    const gchar *stop_reason;
} RtspDecoderValidation;

gboolean rtsp_decoder_validation_success(
    const RtspDecoderValidation *validation);

#endif /* RTSP_VALIDATION_H */
