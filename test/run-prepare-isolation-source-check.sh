#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

python3 - <<'PY'
import re
from pathlib import Path


def fail(message):
    raise SystemExit(message)


def without_comments(source):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.DOTALL)


def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        fail(f'missing source contract: {signature}')
    opening = source.find('{', start)
    if opening < 0:
        fail(f'missing function body: {signature}')
    depth = 1
    pos = opening + 1
    while pos < len(source) and depth:
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
        pos += 1
    if depth:
        fail(f'unterminated function body: {signature}')
    return source[opening + 1:pos - 1]


main = without_comments(Path('main.cpp').read_text(encoding='utf-8'))
video = without_comments(Path('videoBin.cpp').read_text(encoding='utf-8'))
video_header = without_comments(Path('videoBin.h').read_text(encoding='utf-8'))
main_body = function_body(main, 'gint main(')

checks = 0

# 1) VideoBin::init 은 드라이버 컨트롤만 쓰고 bin 을 만들지 않는다(#145).
#    prepare 가 실패한 CSI 는 bin 을 만들지 않아야, 설정으로 끈 CSI 와 같은 상태로
#    나머지 CSI 만 파이프라인에 들어간다. bin 생성과 파이프라인 추가는 build 가 맡는다.
init = function_body(video, 'gboolean VideoBin::init(')
for token in ('gst_bin_new', 'gst_element_factory_make', 'gst_bin_add'):
    if token in init:
        fail(f'VideoBin::init must not construct the bin ({token}) — '
             'prepare 실패 CSI 의 bin 이 생기지 않도록 build 로 옮긴다')
if 'gboolean build(guint8 csiNum);' not in re.sub(r'\s+', ' ', video_header):
    fail('VideoBin must declare gboolean build(guint8 csiNum);')
build = function_body(video, 'gboolean VideoBin::build(')
if 'gst_bin_new' not in build or not re.search(
        r'gst_bin_add\s*\(\s*GST_BIN\s*\(\s*pipeline\s*\)', build):
    fail('VideoBin::build must create the bin and add it to the pipeline')
checks += 1

# 2) 순서: 컨트롤 적용(init) → MAX9296 prepare → bin 구성(build).
#    드라이버는 VideoBin 컨트롤이 prepare 전에 적용됐다고 전제하고(crop fingerprint),
#    bin 구성은 prepare 결과로 CSI 를 끈 뒤여야 한다.
init_pos = main_body.find('videoBin[csiNum].init(')
prepare_pos = main_body.find('max9296_prepare_all(')
build_pos = main_body.find('.build(')
if min(init_pos, prepare_pos, build_pos) < 0:
    fail('main must call VideoBin init, max9296_prepare_all and VideoBin build')
if not init_pos < prepare_pos < build_pos:
    fail('startup order must be VideoBin::init -> max9296_prepare_all -> VideoBin::build')
if main_body.count('.build(') != 1:
    fail('main must build VideoBin bins in exactly one place')
checks += 1

# 3) 부분 실패(MAX9296_PREPARE_PARTIAL)는 실패한 CSI 의 두 채널만 끄고 계속한다.
#    끄는 것은 실행용 사본 cmdArg.cam[].enable 이라 다음 재시작은 설정대로 다시 시도한다.
#    운영자가 바로 보도록 LOG_EMERG 로 errno 를 남긴다(rsyslog *.emerg -> 로그인 터미널).
partial = re.search(
    r'if\s*\(\s*prepare_ret\s*==\s*MAX9296_PREPARE_PARTIAL\s*\)\s*\{', main_body)
if not partial:
    fail('main must handle MAX9296_PREPARE_PARTIAL after max9296_prepare_all')
partial_end = build_pos
partial_body = main_body[partial.end():partial_end]
if not prepare_pos < partial.start() < build_pos:
    fail('PARTIAL handling must run after max9296_prepare_all and before VideoBin::build')
if not re.search(r'if\s*\(\s*!\s*domain\.active\s*\|\|\s*domain\.error\s*==\s*0\s*\)\s*continue\s*;',
                 partial_body):
    fail('PARTIAL handling must skip prepared CSIs (only failed CSIs are disabled)')
if not re.search(r'cmdArg\.cam\[\s*csi\s*\*\s*2\s*\]\.enable\s*=\s*FALSE\s*;', partial_body) \
        or not re.search(r'cmdArg\.cam\[\s*csi\s*\*\s*2\s*\+\s*1\s*\]\.enable\s*=\s*FALSE\s*;',
                         partial_body):
    fail('PARTIAL handling must disable both channels of each failed CSI')
if not re.search(r'__LOG\s*\(\s*LOG_EMERG\s*,[^;]*errno', partial_body, flags=re.DOTALL):
    fail('PARTIAL handling must report the failed CSI and its errno at LOG_EMERG')
checks += 1

print(f'prepare isolation source contract: {checks} checks passed')
PY
