#ifndef HEALTH_PRODUCER_H
#define HEALTH_PRODUCER_H

#include <glib.h>

/* camera health v1 producer.
 *
 * /run/pim-camera/gstApp.json 을 1초 주기로 원자적 publish 한다. pim-package 의
 * camera_healthd 가 이 파일을 읽어 max9296 / pim-healthd 스냅샷과 합친다.
 *
 * 이 경로는 read-only 다. legacy flag 를 건드리지 않고 recovery 를 요청하지
 * 않으며, 파이프라인 상태를 바꾸지 않는다. 관측만 한다.
 *
 * 담당 블록은 gstreamer 와 recording 두 개이며 채널별 scope 로 발행한다.
 *
 * 정상 운용 중에는 OK / FAIL / N/A 만 낸다. UNKNOWN 은 내지 않는다 -
 * aggregator 가 관측 하나라도 UNKNOWN 이면 전체를 DEGRADED 로 떨어뜨리므로,
 * 증거가 약하다는 이유로 UNKNOWN 을 발행하면 정상 하드웨어에서도 HEALTHY 에
 * 영원히 도달하지 못한다.
 */

/* main loop 진입 전에 호출한다. 1초 타이머를 등록한다. */
void healthProducerStart(void);

/* 버스에서 GST_MESSAGE_ERROR 를 받은 시점에 호출한다. */
void healthProducerNotePipelineError(const gchar *detail);

/* splitmuxsink fragment 가 닫힌 시점에 호출한다. 채널별 녹화 생존 신호다. */
void healthProducerNoteFragmentClosed(gint ch, const gchar *location);

#endif
