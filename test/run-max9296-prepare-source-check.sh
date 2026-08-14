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


main_source = Path('main.cpp').read_text(encoding='utf-8')
parser_source = Path('parser.cpp').read_text(encoding='utf-8')
parser_header = Path('parser.h').read_text(encoding='utf-8')
makefile = Path('Makefile').read_text(encoding='utf-8')
util_source = Path('util.cpp').read_text(encoding='utf-8')

main = without_comments(main_source)
main_body = function_body(main, 'gint main(')
arg_parser = function_body(without_comments(parser_source),
                           'gint ParserClass::arg_parser(')
apply_sysfs = function_body(without_comments(parser_source),
                            'gint ParserClass::apply_camera_sysfs(')
safe_write = function_body(without_comments(util_source),
                           'int safe_write_file(')

if '#include "max9296Prepare.h"' not in main:
    fail('main.cpp does not include max9296Prepare.h')
if 'gint apply_camera_sysfs();' not in parser_header:
    fail('ParserClass does not declare apply_camera_sysfs')

arg_failure_gate = re.search(
    r'if\s*\(\s*parser->arg_parser\(\s*&argc\s*,\s*&argv\s*\)'
    r'\s*<=\s*0\s*\)\s*return\s+(?:-1|EXIT_FAILURE)\s*;',
    main_body)
if not arg_failure_gate:
    fail('FALSE arg_parser result must exit nonzero before owner lock/sysfs')

for forbidden in ('safe_write_file', 'DEFAULT_ENABLE_PATH_',
                  'DEFAULT_ROTATE_PATH_'):
    if forbidden in arg_parser:
        fail(f'ParserClass::arg_parser still performs hardware I/O: {forbidden}')

paths = (
    'DEFAULT_ENABLE_PATH_01',
    'DEFAULT_ENABLE_PATH_23',
    'DEFAULT_ROTATE_PATH_01',
    'DEFAULT_ROTATE_PATH_23',
)
if apply_sysfs.count('safe_write_file(') != 4:
    fail('ParserClass::apply_camera_sysfs must own exactly four sysfs writes')
for path in paths:
    if apply_sysfs.count(path) != 2:
        fail(f'apply_camera_sysfs must log and write {path} exactly once')
if len(re.findall(r'if\s*\(\s*ret\s*<\s*0\s*\)', apply_sysfs)) != 4:
    fail('all four apply_camera_sysfs writes must be checked')
if 'fflush(fp)' not in safe_write:
    fail('safe_write_file must flush buffered sysfs writes explicitly')
if not re.search(r'if\s*\(\s*fclose\(fp\)\s*!=\s*0\s*\)', safe_write):
    fail('safe_write_file must propagate fclose failure')
if safe_write.count('fclose(fp)') != 1:
    fail('safe_write_file must close its stream exactly once')
if 'errno = first_errno;' not in safe_write:
    fail('safe_write_file must preserve the first write/close errno')

json_pos = main_body.find('parser->json_parser(')
arg_pos = main_body.find('parser->arg_parser(')
check_pos = main_body.find('parser->check_arg(')
last_copy_pos = main_body.rfind('cmdArg = parser->arg;')
build_pos = main_body.find('max9296_prepare_build_targets(')
lock_pos = main_body.find('max9296_prepare_acquire_owner_lock(')
apply_pos = main_body.find('parser->apply_camera_sysfs(')
video_init_pos = main_body.find('videoBin[csiNum].init(')
disable_pos = main_body.find('cmdArg.cam[csiNum*2].enable = FALSE;',
                             video_init_pos)
prepare_pos = main_body.find('max9296_prepare_all(')

state_positions = [match.start() for match in re.finditer(
    r'gst_element_set_state\s*\(\s*pipeline\s*,\s*'
    r'GST_STATE_(?:PAUSED|PLAYING)\s*\)', main_body)]
if not state_positions:
    fail('main.cpp has no GST PAUSED/PLAYING transition')
first_state_pos = state_positions[0]

ordered = (
    ('json_parser', json_pos),
    ('arg_parser', arg_pos),
    ('check_arg', check_pos),
    ('last cmdArg copy', last_copy_pos),
    ('pure prepare preflight', build_pos),
    ('owner lock', lock_pos),
    ('camera sysfs apply', apply_pos),
    ('VideoBin::init', video_init_pos),
    ('channel-loop enable mutation', disable_pos),
    ('max9296 prepare', prepare_pos),
    ('first GST state transition', first_state_pos),
)
missing = [label for label, position in ordered if position < 0]
if missing:
    fail('missing startup source contracts: ' + ', '.join(missing))
for (left_label, left), (right_label, right) in zip(ordered, ordered[1:]):
    if left >= right:
        fail(f'startup order violation: {left_label} must precede {right_label}')

if main_body.count('max9296_prepare_generate_generation()') != 1:
    fail('startup must generate exactly one MAX9296 transaction generation')
if not re.search(
        r'if\s*\(\s*max9296_prepare_build_targets\('
        r'\s*&prepare_input\s*,\s*prepare_targets\s*\)\s*<\s*0\s*\)'
        r'\s*\{.*?return\s+EXIT_FAILURE\s*;',
        main_body[last_copy_pos:lock_pos], flags=re.DOTALL):
    fail('pure tuple preflight must fail before owner acquisition and sysfs I/O')
if main_body.count('parser->apply_camera_sysfs()') != 1:
    fail('camera sysfs must be applied exactly once')

if not re.search(
        r'if\s*\(\s*prepare_ret\s*<\s*0\s*\)\s*\{\s*'
        r'app_exit_code\s*=\s*EXIT_FAILURE\s*;\s*goto\s+main_end\s*;',
        main_body, flags=re.DOTALL):
    fail('prepare failure must set EXIT_FAILURE before goto main_end')

main_end_pos = main_body.find('main_end:')
release_pos = main_body.find('max9296_prepare_release_owner_lock(',
                             main_end_pos)
exit_pos = main_body.find('exit(app_exit_code)', main_end_pos)
if main_end_pos < 0 or release_pos < 0 or exit_pos < 0:
    fail('main_end must release the owner fd and exit(app_exit_code)')
if not main_end_pos < release_pos < exit_pos:
    fail('owner fd release must occur at main_end before exit(app_exit_code)')
main_end_body = main_body[main_end_pos:]
if main_end_body.count('max9296_prepare_release_owner_lock(') != 1:
    fail('main_end must release the owner fd exactly once')
if not re.search(
        r'if\s*\(\s*parser->apply_camera_sysfs\(\)\s*<\s*0\s*\)\s*\{'
        r'.*?max9296_prepare_release_owner_lock\(max9296_owner_fd\)\s*;'
        r'.*?return\s+EXIT_FAILURE\s*;',
        main_body[:main_end_pos], flags=re.DOTALL):
    fail('camera sysfs failure must release the owner fd and fail startup')

objects = re.search(r'^OBJS\s*=([^\n]*(?:\\\n[^\n]*)*)', makefile,
                    flags=re.MULTILINE)
if not objects or 'max9296Prepare.o' not in objects.group(1):
    fail('Makefile OBJS does not contain max9296Prepare.o')
if not re.search(r'^\$\(OUTPUT\)/testMax9296Prepare\s*:', makefile,
                 flags=re.MULTILINE):
    fail('Makefile has no bin/testMax9296Prepare target')
if not re.search(r'^max9296Prepare\.o\s*:', makefile, flags=re.MULTILINE):
    fail('Makefile has no max9296Prepare.o rule')

print('max9296 prepare source contract: PASSED')
PY
