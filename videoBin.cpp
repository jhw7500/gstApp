/*
 *
 * Cantops videoBin.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "videoBin.h"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Custom V4L2 controls for per-channel settings in dual-channel mode */
#define V4L2_CID_EXPOSURE_AUTO_CH0      (V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_EXPOSURE_AUTO_CH1      (V4L2_CID_USER_BASE + 0x1001)
#define V4L2_CID_AUTO_WHITE_BALANCE_CH0 (V4L2_CID_USER_BASE + 0x1002)
#define V4L2_CID_AUTO_WHITE_BALANCE_CH1 (V4L2_CID_USER_BASE + 0x1003)
#define V4L2_CID_AUTOGAIN_CH0           (V4L2_CID_USER_BASE + 0x1004)
#define V4L2_CID_AUTOGAIN_CH1           (V4L2_CID_USER_BASE + 0x1005)
#define V4L2_CID_GAIN_CH0               (V4L2_CID_USER_BASE + 0x1006)
#define V4L2_CID_GAIN_CH1               (V4L2_CID_USER_BASE + 0x1007)
#define V4L2_CID_HFLIP_CH0              (V4L2_CID_USER_BASE + 0x1008)
#define V4L2_CID_HFLIP_CH1              (V4L2_CID_USER_BASE + 0x1009)
#define V4L2_CID_VFLIP_CH0              (V4L2_CID_USER_BASE + 0x100A)
#define V4L2_CID_VFLIP_CH1              (V4L2_CID_USER_BASE + 0x100B)

static void prepare_format(GstElement *object, gint arg0, GstCaps *caps, gpointer data)
{
    guint8 *csi = (guint8 *)data;
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s [%d]", _FILE_, __LINE__, __FUNCTION__, *csi);
    __LOG(LOG_INFO, "[GST][%s:%d] csi%d caps : %s", _FILE_, __LINE__, *csi, gst_caps_to_string(caps));
}

/**
 * Set V4L2 subdev control value via ioctl
 * @param csiNum CSI number (0 or 1)
 * @param ctrl_id V4L2 control ID (e.g., V4L2_CID_EXPOSURE_AUTO)
 * @param value Control value
 * @return 0 on success, -1 on failure
 */
static int set_v4l2_subdev_control(int csiNum, unsigned int ctrl_id, int value)
{
    char dev_path[32];
    int fd, ret;
    struct v4l2_control ctrl;

    // csiNum 0 -> /dev/v4l-subdev3 (max9296 1-0048, I2C bus 1)
    // csiNum 1 -> /dev/v4l-subdev2 (max9296 2-0048, I2C bus 2)
    snprintf(dev_path, sizeof(dev_path), "/dev/v4l-subdev%d", csiNum == 0 ? 3 : 2);

    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        __LOG(LOG_ERR, "[GST][%s:%d] Failed to open %s: %s",
              _FILE_, __LINE__, dev_path, strerror(errno));
        return -1;
    }

    ctrl.id = ctrl_id;
    ctrl.value = value;

    ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        __LOG(LOG_ERR, "[GST][%s:%d] VIDIOC_S_CTRL failed for ctrl 0x%08x on %s: %s",
              _FILE_, __LINE__, ctrl_id, dev_path, strerror(errno));
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Set ctrl 0x%08x = %d on %s",
              _FILE_, __LINE__, ctrl_id, value, dev_path);
    }

    close(fd);
    return ret;
}

void VideoBin::getIoMode()
{
    gint ioMode;

    g_object_get(be.src, "io-mode", &ioMode, NULL);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] csi%d get io-mode : %d", _FILE_, __LINE__, csi, ioMode);
    g_print("csi%d get io-mode : %d\n", videoData.csi, ioMode);
}

void VideoBin::setIoMode(guint16 data)
{
    gint ioMode;

    g_object_set(be.src, "io-mode", data, NULL);
    g_object_get(be.src, "io-mode", &ioMode, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] csi%d set io-mode : %d", _FILE_, __LINE__, videoData.csi, ioMode);
    g_print("csi%d set io-mode : %d\n", videoData.csi, ioMode);
}

VideoBin* VideoBin::getInstance()
{
	static VideoBin instance;
	return &instance;
}

GstPad* VideoBin::getBinRtspSrcPad(guint8 ch)
{
    if(srcRtspPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    return srcRtspPad;
}

GstPad* VideoBin::getBinRecordSrcPad(guint8 ch)
{
    if(srcRecordPad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    return srcRecordPad;
}

GstPad* VideoBin::getBinCaptureSrcPad(guint8 ch)
{
    if(srcCapturePad == NULL)
        __LOG(LOG_ERR, "[GST][%s:%d] %s ch:%d pad is null", _FILE_, __LINE__, __FUNCTION__, ch);
    else
        __LOG(LOG_INFO, "[GST][%s:%d] %s ch:%d", _FILE_, __LINE__, __FUNCTION__, ch);

    return srcCapturePad;
}

gboolean VideoBin::addBinRtspSrcPad(guint8 ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcRtspPad = gst_ghost_pad_new(g_strdup_printf("srcpad_ch%d", ch), gst_element_get_request_pad(be.teeCrop, "src_%u"));

    return gst_element_add_pad(be.bin, srcRtspPad);
}

gboolean VideoBin::addBinRecordSrcPad(guint8 ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcRecordPad = gst_ghost_pad_new(g_strdup_printf("record_pad_ch%d", ch), gst_element_get_request_pad(be.teeCrop, "src_%u"));

    if(cmdArg.levelMode == MODE_TEST)
    {
        gst_pad_add_probe(srcRecordPad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.bin, NULL);
    }

    return gst_element_add_pad(be.bin, srcRecordPad);
}

gboolean VideoBin::addBinCaptureSrcPad(guint8 ch)
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, ch);
    srcCapturePad = gst_ghost_pad_new(g_strdup_printf("capture_pad_ch%d", ch), gst_element_get_request_pad(be.teeCrop, "src_%u"));

    return gst_element_add_pad(be.bin, srcCapturePad);
}

VideoBin::VideoBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    be.bin = NULL;
    srcRecordPad = NULL;
    srcRtspPad = NULL;
}

VideoBin::~VideoBin()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d]", _FILE_, __LINE__, __FUNCTION__, videoData.csi);
}

gboolean VideoBin::addCrop(CropDir dir)
{
    gint ret = 0;
    //g_print("csi:%d, dir:%d\n", csi, dir);
    __LOG(LOG_INFO, "[GST][%s:%d] video crop csi : %d, dir : %d", _FILE_, __LINE__, videoData.csi, dir);

    be.crop[dir] = gst_element_factory_make("videocrop", g_strdup_printf("crop%d", dir));
    be.tee[dir] = gst_element_factory_make("tee", g_strdup_printf("tee%d", dir));
    be.overlay[dir] = gst_element_factory_make("timeoverlay", g_strdup_printf("overaly%d", dir));
    be.queue[dir] = gst_element_factory_make(QUEUE_TYPE, g_strdup_printf("queue%d", dir));
    be.convert2[dir] = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("convert%d", dir));

    if (dir % 2 == 0)
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.width, NULL);
        //g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", 0, "right", cmdArg.res[cmdArg.resMode].width, NULL);
    else
        g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.width, "right", 0, NULL);
        //g_object_set(be.crop[dir], "top", 0, "bottom", 0, "left", cmdArg.res[cmdArg.resMode].width, "right", 0, NULL);

    if (!be.crop[dir] || !be.tee[dir] || !be.overlay[dir] || !be.queue[dir] || !be.convert2[dir]) {
        __LOG(LOG_CRIT, "[GST][%s:%d] video crop [%d] create error", _FILE_, __LINE__, dir);
        return ret;
    }

    g_object_set(be.overlay[dir], "valignment", 2, NULL);
    g_object_set(be.overlay[dir], "halignment", 0, NULL);
    g_object_set(be.overlay[dir], "font-desc", "Times New Roman Italic, 16", NULL);
    g_object_set(be.overlay[dir], "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(be.overlay[dir], "show-times-as-dates", TRUE, NULL);
    {
        GDateTime *epoch_datetime = g_date_time_new_now_local();
        g_object_set(be.overlay[dir], "datetime-epoch", epoch_datetime, NULL);
        g_date_time_unref(epoch_datetime);
    }
    g_object_set(be.queue[dir], "max-size-time", 300*GST_MSECOND, "max-size-buffers", cmdArg.main_fps[videoData.csi], "leaky", 1, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "width", G_TYPE_INT, cmdArg.width,
                                        "height", G_TYPE_INT, cmdArg.height,
                                        "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[videoData.csi], 1,
                                        NULL);

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(be.bin), be.crop[dir], be.tee[dir], be.overlay[dir], be.queue[dir], be.convert2[dir], NULL);
    
    if(cmdArg.overlay_en) ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.overlay[dir], be.convert2[dir], be.tee[dir], NULL);
    else ret = gst_element_link_many(be.teeCrop, be.crop[dir], be.convert2[dir], be.tee[dir], NULL);

    if (!ret) {
        __LOG(LOG_CRIT, "[GST][%s:%d] crop [%d] link err", _FILE_, __LINE__, dir);
        return ret;
    }

#if 0
    guint8 ch = csi * 2 + dir;
    g_print("ch:%d\n", ch);
    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_record_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error0");
    else
        g_message("videoBin_record_src_ch%d", ch);

    if (!gst_element_add_pad(ve.bin, gst_ghost_pad_new(g_strdup_printf("videoBin_rtsp_src_ch%d", ch), gst_element_get_request_pad(ve.tee[dir], "src_%u"))))
        g_error("error1");
    else
        g_message("videoBin_rtsp_src_ch%d", ch);
#endif

    return ret;
}

gboolean VideoBin::init(guint8 csiNum)
{
    GstCaps *caps;
    gboolean ret = 0;
    gboolean crop_en = cmdArg.crop_en[csiNum];
    videoData.csi = csiNum;
    gint wdt_timeout;

    if(be.bin != NULL)
    {
        //__LOG(LOG_NOTICE, "[GST][%s:%d] %s(%d) already init", _FILE_, __LINE__, __FUNCTION__, csi);
        return 1;
    }

    if((cmdArg.cam[0].enable || cmdArg.cam[1].enable) && (cmdArg.cam[2].enable || cmdArg.cam[3].enable))
        wdt_timeout = cmdArg.wdt_timeout_long;
    else
        wdt_timeout = cmdArg.wdt_timeout_short;

    //if(!cmdArg.stream_en[STREAM_REC])
    //    wdt_timeout = 0;
    
    __LOG(LOG_INFO, "[GST][%s:%d] %s[%d] crop : %s, wdt_timeout : %d", _FILE_, __LINE__, __FUNCTION__, csiNum, crop_en? "enable":"disable", wdt_timeout);

    be.bin = gst_bin_new(g_strdup_printf("videoBin%d", csiNum));
    be.src = gst_element_factory_make("v4l2src", "src");
    be.convert = gst_element_factory_make("imxvideoconvert_g2d", "convert");
    be.capsfilter = gst_element_factory_make("capsfilter", "caps");
    be.teeCrop = gst_element_factory_make("tee", "teeCrop");
    be.queue_main = gst_element_factory_make(QUEUE_TYPE, "queue_main");
    be.deinterlace = gst_element_factory_make("deinterlace", "deinterlace");
    be.rate = gst_element_factory_make("videorate", "videorate");
    be.watchdog = gst_element_factory_make("watchdog", "watchdog");

    if (!be.bin || !be.src || !be.capsfilter || !be.teeCrop || !be.convert || !be.queue_main || !be.deinterlace || !be.rate || !be.watchdog)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video main element create error", _FILE_, __LINE__);
        return ret;
    }
    gst_bin_add_many(GST_BIN(be.bin), be.src, be.convert, be.capsfilter, be.teeCrop, be.queue_main, be.deinterlace, be.rate, be.watchdog, NULL);

    g_object_set(be.src, "io-mode", cmdArg.ioMode, NULL);   //0:auto, 1:rw, 2:mmap, 3:userptr, 4:dmabuf, 5:dmabuf-import
    g_object_set(be.src, "do-timestamp", TRUE, NULL);
    //g_object_set(be.deinterlace, "mode", 1, NULL);
    //g_object_set(be.src, "pixel-aspect-ratio", "1/1", NULL);
    g_signal_connect(be.src, "prepare-format", G_CALLBACK(prepare_format), &csiNum);
    g_object_set(be.queue_main, "max-size-time", 300*GST_MSECOND, "max-size-buffers", cmdArg.main_fps[csiNum], "leaky", LEAKY_DOWNSTREAM, NULL);
    g_object_set(be.watchdog, "timeout", wdt_timeout, NULL);

    // V4L2 controls setup - per-channel settings in dual mode
    // csiNum 0 -> channels 0,1 / csiNum 1 -> channels 2,3
    guint8 ch_base = csiNum * 2;
    guint8 ch0 = ch_base;
    guint8 ch1 = ch_base + 1;
    gboolean ch0_enabled = cmdArg.cam[ch0].enable;
    gboolean ch1_enabled = cmdArg.cam[ch1].enable;
    gboolean dual_mode = ch0_enabled && ch1_enabled;

    // Exposure time is shared (global) across both channels
    if (ch0_enabled || ch1_enabled) {
        // Use exp_time from i2c config (shared between channels)
        guint32 exp_time = ch0_enabled ? cmdArg.cam[ch0].exp_time : cmdArg.cam[ch1].exp_time;
        set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE, exp_time);
    }

    if (dual_mode) {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Dual-channel mode detected for csi%d (ch%d + ch%d)",
            _FILE_, __LINE__, csiNum, ch0, ch1);

        // Channel 0 settings
        int ae_ch0 = cmdArg.cam[ch0].ae_on ? 0 : 1;  // 0=auto, 1=manual
        set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE_AUTO_CH0, ae_ch0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTOGAIN_CH0, cmdArg.cam[ch0].ae_on ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_GAIN_CH0, cmdArg.cam[ch0].ae_gain);

        gint awb_ch0 = (g_strcmp0(cmdArg.cam[ch0].awb, "auto") == 0) ? 1 : 0;
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTO_WHITE_BALANCE_CH0, awb_ch0);

        set_v4l2_subdev_control(csiNum, V4L2_CID_HFLIP_CH0, cmdArg.cam[ch0].hflip ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_VFLIP_CH0, cmdArg.cam[ch0].vflip ? 1 : 0);

        __LOG(LOG_NOTICE, "[GST][%s:%d] CH0 controls: ae=%d gain=%d awb=%d hflip=%d vflip=%d",
            _FILE_, __LINE__, ae_ch0, cmdArg.cam[ch0].ae_gain, awb_ch0,
            cmdArg.cam[ch0].hflip, cmdArg.cam[ch0].vflip);

        // Channel 1 settings
        int ae_ch1 = cmdArg.cam[ch1].ae_on ? 0 : 1;  // 0=auto, 1=manual
        set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE_AUTO_CH1, ae_ch1);
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTOGAIN_CH1, cmdArg.cam[ch1].ae_on ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_GAIN_CH1, cmdArg.cam[ch1].ae_gain);

        gint awb_ch1 = (g_strcmp0(cmdArg.cam[ch1].awb, "auto") == 0) ? 1 : 0;
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTO_WHITE_BALANCE_CH1, awb_ch1);

        set_v4l2_subdev_control(csiNum, V4L2_CID_HFLIP_CH1, cmdArg.cam[ch1].hflip ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_VFLIP_CH1, cmdArg.cam[ch1].vflip ? 1 : 0);

        __LOG(LOG_NOTICE, "[GST][%s:%d] CH1 controls: ae=%d gain=%d awb=%d hflip=%d vflip=%d",
            _FILE_, __LINE__, ae_ch1, cmdArg.cam[ch1].ae_gain, awb_ch1,
            cmdArg.cam[ch1].hflip, cmdArg.cam[ch1].vflip);
    } else {
        // Single-channel mode: use legacy controls (apply to whichever channel is enabled)
        CamConfig *cam_cfg = ch0_enabled ? &cmdArg.cam[ch0] : &cmdArg.cam[ch1];
        guint8 active_ch = ch0_enabled ? ch0 : ch1;

        __LOG(LOG_NOTICE, "[GST][%s:%d] Single-channel mode for csi%d (ch%d)",
            _FILE_, __LINE__, csiNum, active_ch);

        int exposure_auto = cam_cfg->ae_on ? 0 : 1;  // 0=auto, 1=manual
        set_v4l2_subdev_control(csiNum, V4L2_CID_EXPOSURE_AUTO, exposure_auto);
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTOGAIN, cam_cfg->ae_on ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_GAIN, cam_cfg->ae_gain);

        gint awb_auto = (g_strcmp0(cam_cfg->awb, "auto") == 0) ? 1 : 0;
        set_v4l2_subdev_control(csiNum, V4L2_CID_AUTO_WHITE_BALANCE, awb_auto);

        set_v4l2_subdev_control(csiNum, V4L2_CID_HFLIP, cam_cfg->hflip ? 1 : 0);
        set_v4l2_subdev_control(csiNum, V4L2_CID_VFLIP, cam_cfg->vflip ? 1 : 0);

        __LOG(LOG_NOTICE, "[GST][%s:%d] V4L2 subdev controls set: csi%d ch%d ae=%d gain=%d awb=%s hflip=%d vflip=%d",
            _FILE_, __LINE__, csiNum, active_ch, cam_cfg->ae_on, cam_cfg->ae_gain,
            cam_cfg->awb, cam_cfg->hflip, cam_cfg->vflip);
    }

    if(cmdArg.levelMode == MODE_TEST)
    {
        //GstPad *srcpad = gst_element_get_static_pad(be.src, "src");
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.src, NULL);
        //gst_pad_add_probe(srcpad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_function, be.capsfilter, NULL);
        //gst_object_unref(srcpad);
    }

    if (csiNum == 0)
    {
        __LOG(LOG_INFO, "[GST][%s:%d] %s : video4", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video4", NULL);
    }
    else if (csiNum == 1)
    {
        __LOG(LOG_INFO, "[GST][%s:%d] %s : video3", _FILE_, __LINE__, __FUNCTION__);
        g_object_set(be.src, "device", "/dev/video3", NULL);
    }

    if(crop_en)
    {
        caps = gst_caps_new_simple("video/x-raw", 
                                    //"format", G_TYPE_STRING, "RGBx",
                                    //"format", G_TYPE_STRING, "NV12",
                                    "width", G_TYPE_INT, cmdArg.width*2,
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[csiNum], 1,
                                    //"colorimetry", G_TYPE_STRING, "1:4:5:1",
                                    //"interlace-mode", G_TYPE_STRING, "progressive",
                                    //"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                                    NULL);
                                    
        
        ret = gst_element_link_many(be.src, be.watchdog, be.convert, be.capsfilter, be.teeCrop, NULL);
        //ret = gst_element_link_filtered(be.src, be.teeCrop, caps);
        if (!ret)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
            return ret;
        }
    }
    else
    {
        caps = gst_caps_new_simple("video/x-raw", 
                                    //"format", G_TYPE_STRING, "NV12",
                                    "width", G_TYPE_INT, cmdArg.width,
                                    "height", G_TYPE_INT, cmdArg.height,
                                    "framerate", GST_TYPE_FRACTION, cmdArg.main_fps[csiNum], 1,
                                    //"colorimetry", G_TYPE_STRING, "1:4:5:1",
                                    //"interlace-mode", G_TYPE_STRING, "progressive",
                                    //"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                                    NULL);

        ret = gst_element_link_many(be.src, be.watchdog, be.capsfilter, be.teeCrop, NULL);
        //ret = gst_element_link_filtered(be.src, be.teeCrop, caps);
        if (!ret)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] video main link err", _FILE_, __LINE__);
            return ret;
        }
    }

    g_object_set(be.capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    ret = gst_bin_add(GST_BIN(pipeline), be.bin);
    if (!ret)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video bin add error in pipeline", _FILE_, __LINE__);
        return ret;
    }

    return ret;
}
