#!/usr/bin/env python3
"""Generate docs/REPORT.md, REPORT.docx, and REPORT.html for the project.

The formatted PDF is rendered from REPORT.html by make_pdf.py.  The editable
HWP edition is produced by make_hwp_text.py (plain text) or the experimental
convert_hwp.py (Hancom import).  Keeping the source in one place keeps all
editions consistent.
"""
from __future__ import annotations

import os
from pathlib import Path
from html import escape as html_escape

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
FONT = "맑은 고딕"
MONO = "Consolas"
NAVY = RGBColor(0x1F, 0x38, 0x64)
GRAY = RGBColor(0x59, 0x59, 0x59)

# ---------------------------------------------------------------------------
# Report content
# ---------------------------------------------------------------------------

BLOCKS: list[tuple] = [
    ("cover",),
    ("pagebreak",),

    ("h1", "1. 프로젝트 개요"),
    ("h2", "1.1 추진 배경"),
    ("p", "교재 357쪽의 simplesh.c는 fgets()로 한 줄을 읽고, getargs()로 인자를 "
          "분리한 뒤 fork()/execvp()로 명령을 실행하는 기본 쉘이다. exit, "
          "백그라운드, 인터럽트, 재지향, 파이프, 내장 명령이 빠져 있어 실제 쉘과 "
          "시스템 프로그래밍 학습 도구로 쓰기에는 부족하다."),
    ("p", "Lab 3의 5개 요구사항과 11개 내장 명령을 구현하고, 파서·파이프·"
          "시스템 호출·ELF·프로세스 상태를 확인하는 도구와 세션 기록 기능을 "
          "추가했다."),

    ("h2", "1.2 프로젝트 목표"),
    ("bullets", [
        "exit, &, SIGINT/SIGQUIT, 재지향/파이프를 구현한다.",
        "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep을 직접 구현한다.",
        "explain으로 파서 구조를, pipestat으로 파이프 단계를 보여 준다.",
        "trace/dbg로 시스템 호출과 ELF 실행을 들여다본다.",
        "inspect로 /proc 기반 프로세스 상태(fd, maps, namespace)를 조사한다.",
        "rec/replay로 세션을 기록하고 HTML 타임라인으로 남긴다.",
    ]),

    ("h2", "1.3 구현 범위"),
    ("table",
     ["구분", "내용"],
     [
         ["필수 기능", "exit, 백그라운드(&), SIGINT/SIGQUIT, 재지향(>, <), 파이프(|)"],
         ["필수 내장 명령", "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개)"],
         ["보조 내장 명령", "echo, export, unset, jobs, wait, help, true, false, :"],
         ["관찰 도구", "explain(파서), pipestat(파이프), trace(시스템 호출), dbg(ELF/ptrace), inspect(/proc)"],
         ["기록 도구", "rec/replay: 명령·출력·상태·시간, -x explain/trace, HTML export"],
         ["문서/배포", "README, MIT LICENSE, PDF/HWP/DOCX 보고서, 자동 테스트"],
     ]),

    ("pagebreak",),
    ("h2", "1.4 개발 환경"),
    ("table",
     ["항목", "내용"],
     [
         ["운영체제", "Ubuntu 26.04.1 LTS (WSL2, x86_64)"],
         ["컴파일러", "gcc 15.2.0 (-std=gnu11)"],
         ["빌드 도구", "GNU Make, Bash"],
         ["테스트", "Bash, Python 3.14 (pty/select)"],
         ["버전 관리", "Git, GitHub (gong000000/lab3)"],
         ["외부 라이브러리", "glibc, POSIX regex 외 없음"],
     ]),

    ("h2", "1.5 결과 요약"),
    ("p", "소스는 직접 작성 8,543줄, 생성 테이블 포함 8,680줄이다. 11개 필수 "
          "명령과 관찰 도구 5종(explain, pipestat, trace, dbg, inspect), 기록 "
          "도구(rec/replay)를 제공하며, 기능 94개·확장 21개·파일 입력 4개·추적 "
          "32개·디버거 44개·녹화 35개·조사 27개 테스트와 PTY 시그널 테스트를 "
          "통과했다."),
    ("table",
     ["항목", "결과"],
     [
         ["소스 규모", "직접 작성 8,543줄, 생성 포함 8,680줄"],
         ["필수 내장 명령", "11개"],
         ["기능 테스트", "94 / 94"],
         ["확장 테스트", "21 / 21 (explain, &&/||, glob, pipestat)"],
         ["파일 입력", "4 / 4"],
         ["추적", "32 / 32"],
         ["디버거", "44 / 44"],
         ["녹화", "35 / 35"],
         ["조사", "27 / 27"],
         ["시그널", "PTY 시그널 테스트 통과"],
     ]),

    ("pagebreak",),
    ("h1", "2. 설계 구성 요소"),
    ("h2", "2.1 시스템 구조"),
    ("code", """+---------------------------- 사용자 터미널 ----------------------------+
|  stdin/stdout/stderr, Ctrl-C(SIGINT), Ctrl-Z(SIGQUIT)              |
+----------------------------------+---------------------------------+
                                   |
                    +--------------v--------------+
                    | main.c : REPL, signal,      |
                    | termios, 명령 분리           |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | parser.c : tokenizer,       |
                    | quote/expansion, $(...)     |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | execute.c : fork/exec,      |
                    | pipe, dup2, wait            |
                    +----+-------------------+----+
                         |                   |
          +--------------v------+   +--------v------------------+
          | builtin.c + cmd_*.c |   | execvp() : 외부 명령      |
          | 11개 명령 + 관찰 도구 |   | (/bin/ls, date, ...)      |
          +--------------+------+   +---------------------------+
                         |
          +--------------v--------------+
          | jobs.c : 백그라운드 작업     |
          +--------------+--------------+
                         |
          +--------------v--------------+
          | explain / pipestat / trace  |
          | dbg / inspect / rec         |
          +-----------------------------+"""),

    ("h2", "2.2 모듈 구성"),
    ("table",
     ["파일", "역할", "줄 수"],
     [
         ["include/myshell.h", "공용 구조체와 함수 원형", "240"],
         ["src/main.c", "REPL, 명령 분리, 시그널, 터미널", "411"],
         ["src/parser.c", "토큰화, 확장, 문법 분석", "921"],
         ["src/execute.c", "fork/exec, 파이프, 재지향, 캡처", "469"],
         ["src/jobs.c", "백그라운드 작업", "154"],
         ["src/builtin.c", "내장 명령 디스패치", "522"],
         ["src/cmd_ls.c", "ls", "370"],
         ["src/cmd_fs.c", "mkdir/rmdir/ln/cp/rm/mv", "938"],
         ["src/cmd_text.c", "cat/grep", "469"],
         ["src/cmd_explain.c", "파서 시각화", "111"],
         ["src/cmd_pipestat.c", "파이프 단계 계측", "330"],
         ["src/cmd_trace.c", "ptrace 시스템 호출 추적", "563"],
         ["src/cmd_dbg.c", "ELF/ptrace 미니 디버거", "1253"],
         ["src/cmd_inspect.c", "/proc 기반 프로세스 조사", "276"],
         ["src/cmd_rec.c", "세션 녹화/재생/HTML", "1002"],
         ["src/util.c", "공용 유틸", "528"],
         ["src/trace_syscalls.h", "시스템 호출 이름 테이블 (생성)", "137"],
         ["합계", "직접 작성 8,543 + 생성 137 = 8,680", ""],
     ]),

    ("h2", "2.3 핵심 자료구조"),
    ("table",
     ["구조체", "주요 필드", "역할"],
     [
         ["command_t", "argv[], argc, redir", "한 명령의 인자/재지향"],
         ["pipeline_t", "commands[], background, text", "파이프 묶음"],
         ["job_t", "pgid, pids[], status, command", "백그라운드 작업"],
         ["shell_state_t", "last_status, jobs[], positional[]", "쉘 상태"],
         ["tracee_t", "pid, entered, nr, args[6]", "ptrace 추적 상태"],
         ["dbg_t", "pid, bias, symbols[], breakpoints[]", "디버거 상태"],
         ["rec_item_t", "cmd, out, err, status, ms", "녹화 명령 하나"],
     ]),

    ("h2", "2.4 핵심 설계"),
    ("h3", "2.4.1 파싱과 실행"),
    ("p", "main.c의 find_separator()가 따옴표를 고려해 ;와 &를 분리하고, "
          "parser.c가 단어/연산자를 토큰화한다. 따옴표, $VAR, $?, $$, $!, "
          "$(...)를 확장하고 파이프/재지향/&&/||/glob을 처리한다. cd, exit, "
          "jobs, wait, export처럼 쉘 상태를 바꾸는 내장 명령은 부모에서, "
          "나머지는 자식에서 실행한다."),
    ("h3", "2.4.2 시그널과 작업 제어"),
    ("p", "전경 파이프라인은 하나의 프로세스 그룹으로 묶고 tcsetpgrp()로 "
          "터미널을 넘긴다. 자식은 SIGINT/SIGQUIT 등을 기본값으로 복원하므로 "
          "Ctrl-C/Ctrl-Z는 전경 작업만 중단한다. 과제 명세에 맞춰 VQUIT를 "
          "Ctrl-Z로 매핑하고 VSUSP를 비활성화한다."),
    ("h3", "2.4.3 explain"),
    ("p", "parse_line() 결과를 실행하지 않고 파이프라인, 명령, 재지향, "
          "백그라운드 구조로 출력한다. “쉘이 입력을 어떻게 해석했는가”를 "
          "보여 주는 기본 도구이다."),
    ("h3", "2.4.4 pipestat"),
    ("p", "파이프 단계 사이에 relay 프로세스를 넣어 바이트 수, 시작/종료 시각, "
          "종료 코드를 수집한다. 어느 단계가 데이터를 많이 만들고 오래 걸리는지 "
          "보여 주어 파이프라인 학습에 사용한다."),
    ("h3", "2.4.5 trace"),
    ("p", "PTRACE_TRACEME로 자식을 띄우고 PTRACE_SYSCALL로 진입/종료를 반복한다. "
          "GETREGS로 시스템 호출 번호/인자/반환값을 읽고, 경로 인자는 PEEKDATA로 "
          "문자열을 읽는다. -c 요약, -f 자식 추적, -e 필터를 제공한다."),
    ("h3", "2.4.6 dbg"),
    ("p", "ELF .symtab/.dynsym에서 함수 심볼을 읽고, PIE는 /proc/PID/maps에서 "
          "로드 기준 주소를 계산한다. 0xCC(int3) 브레이크포인트를 심고, 정지 시 "
          "원본 바이트를 복원한 뒤 rip를 1 되돌린다. step/continue에서 한 명령을 "
          "실행한 후 브레이크포인트를 다시 심는다. regs, x/Nx, bt, finish, "
          "SIGSEGV 자동 분석을 지원한다."),
    ("h3", "2.4.7 inspect"),
    ("p", "inspect는 /proc/PID/status, fd, maps, ns를 읽어 프로세스의 상태를 "
          "보여 준다. 쉘 자신의 pid/ppid/pgid/sid, uid/gid, cwd, umask, "
          "rlimit, 백그라운드 작업도 함께 출력한다. 시스템 프로그래밍에서 "
          "프로세스와 파일 디스크립터가 어떻게 표현되는지 직접 확인하는 도구이다."),
    ("h3", "2.4.8 rec/replay"),
    ("p", "rec는 전경 명령의 stdout/stderr를 relay 프로세스로 실시간 중계하면서 "
          "동시에 임시 파일에 기록한다. 백그라운드(&) 명령은 지연 캡처 경로로 "
          "처리한다. replay는 명령을 재실행하지 않고 출력과 상태를 재생하며, "
          "rec export는 단독 HTML 타임라인을 만든다. -x는 explain과 trace -c "
          "요약을 추가한다."),
    ("code", """record: C/O/E/S/T/X (명령, stdout, stderr, 상태, ms, 진단)
replay: 저장된 출력/상태/시간을 재표시 (부작용 없음)
export: HTML + inline CSS, 서버 불필요"""),

    ("pagebreak",),
    ("h1", "3. 설계 제한 요소"),
    ("table",
     ["제한 요소", "설계 기준", "본 프로젝트의 대응"],
     [
         ["경제성", "1,000~10,000줄 원칙",
          "직접 작성 8,543줄, 생성 포함 8,680줄로 준수"],
         ["안정성", "외부 환경 변화에도 동작",
          "오류 반환값 검사, 좀비 회수, 재지향 복구, 257개 자동 테스트"],
         ["생산성", "학습/응용 가능",
          "11개 명령 + explain/pipestat/trace/dbg/inspect/rec"],
         ["윤리성", "안전한 파일 동작",
          "rm의 /·.·.. 보호, system()/popen() 미사용, MIT 라이선스"],
         ["표준성", "POSIX/Linux 인터페이스",
          "fork, execvp, waitpid, pipe, dup2, signal, termios, dirent, regex"],
     ]),

    ("h2", "3.1 경제성"),
    ("p", "소스 규모 산정 기준은 다음과 같다."),
    ("table",
     ["항목", "줄 수"],
     [
         ["include/myshell.h + src/*.c", "8,543"],
         ["빈 줄", "997"],
         ["주석", "204"],
         ["주석·빈 줄 제외 코드", "7,342"],
         ["생성된 trace_syscalls.h", "137"],
         ["테스트 코드(별도)", "1,201"],
         ["생성 포함 총계", "8,680"],
         ["생성·테스트 포함 총계", "9,881"],
     ]),
    ("p", "헤더와 기능별 소스를 분리해 빌드와 유지보수가 쉽도록 구성했다."),

    ("h2", "3.2 안정성"),
    ("p", "모든 시스템 콜의 반환값을 검사하고, 백그라운드 작업은 waitpid(WNOHANG)로 "
          "회수한다. 재지향 실패 시 fd를 복원하고, 쉘 종료 시 termios를 복구한다. "
          "기능·확장·추적·디버거·녹화·조사 테스트와 PTY 시그널 테스트로 회귀를 "
          "확인했다."),

    ("h2", "3.3 생산성"),
    ("p", "11개 명령은 외부 프로그램 없이 동작한다. explain/pipestat/trace/dbg/"
          "inspect는 “왜 이런 결과가 나왔는가”를 단계별로 보여 주어 시스템 "
          "프로그래밍 학습 시간을 줄인다. rec는 실습 과정을 HTML로 제출해 "
          "채점자가 서버 없이 재현할 수 있게 한다."),

    ("h2", "3.4 윤리성과 사회적 영향"),
    ("p", "rm은 /, ., ..를 거부하고 재귀 삭제는 심볼릭 링크를 따라가지 않는다. "
          "외부 명령은 execvp()로 직접 실행하며 system()/popen()을 쓰지 않는다. "
          "MIT 라이선스로 수정·배포를 허용한다."),

    ("pagebreak",),
    ("h1", "4. 구현 및 검증"),
    ("h2", "4.1 요구사항 1~5"),
    ("table",
     ["요구사항", "구현", "검증"],
     [
         ["1. exit", "builtin_exit, $?", "종료 상태 테스트"],
         ["2. &", "job_t, setpgid, jobs/wait/$!", "조기 복귀/jobs/wait"],
         ["3. Ctrl-C/Ctrl-Z", "SIGINT/SIGQUIT, 전경 프로세스 그룹", "PTY 시그널 테스트"],
         ["4. 재지향/파이프", "open/dup2/pipe, 다단 파이프", "파일 비교/2>&1/파이프"],
         ["5. 11개 명령", "cmd_ls/cmd_fs/cmd_text", "94개 기능 테스트"],
     ]),
    ("h2", "4.2 테스트 설계"),
    ("p", "tests/run_tests.sh는 stdout/stderr/종료 상태를 비교하는 94개 기능 "
          "테스트를 수행한다. test_ext는 explain/&&/glob/pipestat 21개, "
          "test_file_input은 4개, test_trace는 32개, test_dbg는 44개, "
          "test_rec는 35개, test_inspect는 27개를 검사한다. test_signals.py는 "
          "pty.fork()로 Ctrl-C/Ctrl-Z 후 쉘 생존을 확인한다."),
    ("h2", "4.3 테스트 결과"),
    ("code", """Passed: 94
File-input tests passed: 4
Extension tests passed: 21
Trace tests passed: 32
Debugger tests passed: 44
Recording tests passed: 35
Inspect tests passed: 27
SIGNAL TEST PASSED"""),

    ("pagebreak",),
    ("h1", "5. 프로젝트 결과"),
    ("h2", "5.1 구현 내용"),
    ("table",
     ["기능", "상태", "검증"],
     [
         ["필수 요구사항 1~5", "완료", "94개 기능 + PTY"],
         ["11개 내장 명령", "완료", "ls/pwd/cd/…/grep"],
         ["explain", "완료", "파이프/재지향 도식"],
         ["pipestat", "완료", "단계별 바이트/시간/상태"],
         ["trace", "완료", "시스템 호출/필터/요약"],
         ["dbg", "완료", "ELF/PIE, break/step/bt, SIGSEGV"],
         ["inspect", "완료", "/proc status/fd/maps/ns"],
         ["rec/replay", "완료", "출력/상태/시간, HTML"],
     ]),
    ("h2", "5.2 완성도 평가"),
    ("p", "원 과제 요구사항과 11개 명령을 모두 유지했고, 8,680줄로 구성했다. "
          "257개 자동 테스트와 PTY 시그널 테스트가 통과했다."),

    ("h2", "5.3 일정/역할/위험"),
    ("table",
     ["주차", "류동엽", "강동우", "증빙"],
     [
         ["11주", "simplesh 분석, 요구사항/파서 설계", "파일/텍스트 명령 분석", "요구사항 표"],
         ["12주", "토큰화/확장/파이프/재지향", "ls/mkdir/rmdir 초안", "parser.c/execute.c"],
         ["13주", "프로세스 그룹/시그널", "ln/cp/rm/mv 경계", "PTY 테스트"],
         ["14주", "11개 명령 통합, 94개 테스트", "cat/grep 옵션", "Passed: 94"],
         ["15주", "explain/pipestat/trace/dbg/inspect/rec", "테스트/문서/PTY", "기능별 커밋"],
     ]),
    ("table",
     ["위험", "대응", "결과"],
     [
         ["Ctrl-C가 쉘까지 종료", "자식 setpgid + tcsetpgrp", "PTY 통과"],
         ["ptrace 차단", "자기 자식만 PTRACE_TRACEME", "WSL2 통과"],
         ["PIE 주소 불일치", "/proc/PID/maps bias 계산", "dbg PIE 테스트"],
         ["브레이크포인트 재정지", "복원→SINGLESTEP→재설치", "dbg 통과"],
         ["백그라운드 녹화", "전경은 relay, & 명령은 지연 캡처", "rec 통과"],
         ["인터럽트/시스템 호출", "SIGINT/SIGQUIT 처리, ptrace 추적", "PTY/추적 테스트"],
     ]),
    ("p", "역할은 5개(pwd, cd, ls, mkdir, rmdir)와 6개(ln, cp, rm, mv, cat, "
          "grep)로 나누었고, 관찰 도구/코어는 류동엽, 테스트·문서·PTY 검증은 "
          "강동우가 담당했다."),

    ("pagebreak",),
    ("h1", "부록 A. 빌드 및 실행"),
    ("code", """make
./myshell
./myshell -c 'exit 7'
./myshell -c 'explain "cat a | grep b"'
./myshell -c 'pipestat "echo hi | cat | wc -c"'
./myshell -c 'trace -e write /bin/echo hi'
./myshell -c 'inspect'
./myshell -c 'rec export lab.rec out.html'
make test"""),
    ("h1", "부록 B. 테스트 항목"),
    ("table",
     ["영역", "내용", "개수"],
     [
         ["기능", "exit, 인용/확장, 재지향, 파이프, 작업, 11개 명령, 오류", "94"],
         ["확장", "explain, &&/||, glob, pipestat", "21"],
         ["파일 입력", "여러 줄, 마지막 줄, 백그라운드", "4"],
         ["추적", "필터/요약/자식/문자열/오류", "32"],
         ["디버거", "ELF/PIE, break/step/finish/bt, SIGSEGV", "44"],
         ["녹화", "rec/replay, 출력, HTML, -x, 오류", "35"],
         ["조사", "/proc status/fd/maps/ns/jobs/limits/오류", "27"],
         ["시그널", "PTY Ctrl-C/Ctrl-Z", "1 시나리오"],
     ]),
    ("h1", "부록 C. 소스 규모"),
    ("table",
     ["파일", "줄 수"],
     [
         ["include/myshell.h", "240"],
         ["src/main.c", "411"],
         ["src/parser.c", "921"],
         ["src/execute.c", "469"],
         ["src/jobs.c", "154"],
         ["src/builtin.c", "522"],
         ["src/cmd_ls.c", "370"],
         ["src/cmd_fs.c", "938"],
         ["src/cmd_text.c", "469"],
         ["src/cmd_explain.c", "111"],
         ["src/cmd_pipestat.c", "330"],
         ["src/cmd_trace.c", "563"],
         ["src/cmd_dbg.c", "1253"],
         ["src/cmd_inspect.c", "276"],
         ["src/cmd_rec.c", "1002"],
         ["src/util.c", "528"],
         ["src/trace_syscalls.h", "137"],
         ["합계", "8,680 / 직접 작성 8,543"],
     ]),
    ("h1", "부록 D. 참고문헌"),
    ("bullets", [
        "교재 357쪽, simplesh.c (fgets/getargs/fork/execvp/wait)",
        "Linux man pages: fork(2), execvp(3), waitpid(2), pipe(2), dup2(2), "
        "signal(7), sigaction(2), termios(3), tcsetpgrp(3)",
        "Linux man pages: opendir(3), readdir(3), lstat(2), link(2), symlink(2), "
        "rename(2), unlink(2), rmdir(2)",
        "POSIX regex: regcomp(3), regexec(3), regerror(3)",
        "Linux man pages: ptrace(2), waitpid(2) __WALL, sys/user.h",
        "Linux man pages: proc(5), getrlimit(2), readlink(2), gethostname(2)",
        "프로젝트 저장소: https://github.com/gong000000/lab3",
    ]),
]


# ---------------------------------------------------------------------------
# DOCX rendering helpers
# ---------------------------------------------------------------------------

def set_run_font(run, name=FONT, size=None, bold=None, color=None):
    run.font.name = name
    rpr = run._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    rfonts.set(qn("w:ascii"), name)
    rfonts.set(qn("w:hAnsi"), name)
    rfonts.set(qn("w:eastAsia"), name)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    if color is not None:
        run.font.color.rgb = color


def shade(cell, fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    tc_pr.append(shd)


def cell_margins(table, top=40, bottom=40, left=70, right=70):
    tbl_pr = table._tbl.tblPr
    mar = OxmlElement("w:tblCellMar")
    for side, value in (("top", top), ("left", left), ("bottom", bottom), ("right", right)):
        el = OxmlElement(f"w:{side}")
        el.set(qn("w:w"), str(value))
        el.set(qn("w:type"), "dxa")
        mar.append(el)
    tbl_pr.append(mar)


def repeat_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    header = OxmlElement("w:tblHeader")
    header.set(qn("w:val"), "true")
    tr_pr.append(header)


def set_cell_text(cell, text, bold=False, size=9.0, color=None, align=None):
    cell.text = ""
    p = cell.paragraphs[0]
    if align is not None:
        p.alignment = align
    p.paragraph_format.space_after = Pt(0)
    p.paragraph_format.line_spacing = 1.0
    run = p.add_run(text)
    set_run_font(run, FONT, size, bold, color)


def add_table(doc, headers, rows, widths=None):
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    cell_margins(table)

    header_cells = table.rows[0].cells
    for i, text in enumerate(headers):
        set_cell_text(header_cells[i], text, bold=True, size=9.0)
        shade(header_cells[i], "D9E2F3")
    repeat_header(table.rows[0])

    for row in rows:
        cells = table.add_row().cells
        for i, text in enumerate(row):
            set_cell_text(cells[i], str(text), size=9.0)

    if widths:
        for row in table.rows:
            for i, width in enumerate(widths):
                row.cells[i].width = Cm(width)
    return table


def add_code(doc, text):
    table = doc.add_table(rows=1, cols=1)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    cell = table.rows[0].cells[0]
    cell.width = Cm(16.5)
    shade(cell, "F3F3F3")
    cell_margins(table, top=70, bottom=70, left=100, right=100)

    lines = text.split("\n")
    first = True
    for line in lines:
        if first:
            p = cell.paragraphs[0]
            first = False
        else:
            p = cell.add_paragraph()
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        run = p.add_run(line if line else " ")
        set_run_font(run, MONO, 8.2, False)
    doc.add_paragraph().paragraph_format.space_after = Pt(0)


def add_page_number(paragraph):
    run = paragraph.add_run()
    fld_begin = OxmlElement("w:fldChar")
    fld_begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = "PAGE"
    fld_end = OxmlElement("w:fldChar")
    fld_end.set(qn("w:fldCharType"), "end")
    run._r.append(fld_begin)
    run._r.append(instr)
    run._r.append(fld_end)
    set_run_font(run, FONT, 9, False, GRAY)


def style_document(doc):
    styles = doc.styles
    normal = styles["Normal"]
    normal.font.name = FONT
    normal.font.size = Pt(10)
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), FONT)
    normal.paragraph_format.line_spacing = 1.18
    normal.paragraph_format.space_after = Pt(4)

    for name, size, color, before, after in (
        ("Heading 1", 15.5, NAVY, 12, 8),
        ("Heading 2", 12.5, NAVY, 10, 5),
        ("Heading 3", 11.0, RGBColor(0x33, 0x33, 0x33), 8, 4),
    ):
        st = styles[name]
        st.font.name = FONT
        st.font.size = Pt(size)
        st.font.bold = True
        st.font.color.rgb = color
        st.element.rPr.rFonts.set(qn("w:eastAsia"), FONT)
        st.paragraph_format.space_before = Pt(before)
        st.paragraph_format.space_after = Pt(after)
        st.paragraph_format.keep_with_next = True


def add_cover(doc):
    for _ in range(3):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run("프로젝트 결과보고서")
    set_run_font(run, FONT, 26, True, NAVY)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(12)
    run = p.add_run("간단한 쉘(Simple Shell) 구현 및 확장")
    set_run_font(run, FONT, 15, True, GRAY)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(4)
    run = p.add_run("오픈소스프로젝트(전공설계) · 2026학년도 2학기")
    set_run_font(run, FONT, 11, False, GRAY)

    for _ in range(4):
        doc.add_paragraph()

    rows = [
        ("과목", "오픈소스프로젝트(전공설계)"),
        ("프로젝트", "Lab 3 - 간단한 쉘 구현 및 확장"),
        ("팀명", "myshell"),
        ("팀원", "류동엽 (20223154, @gong000000)\n강동우 (20223080, @pizza0420)"),
        ("GitHub", "https://github.com/gong000000/lab3"),
        ("제출일", "2026. 11. 18."),
    ]
    table = doc.add_table(rows=0, cols=2)
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    cell_margins(table, top=80, bottom=80, left=120, right=120)
    for key, value in rows:
        cells = table.add_row().cells
        cells[0].width = Cm(3.2)
        cells[1].width = Cm(11.0)
        set_cell_text(cells[0], key, bold=True, size=10)
        shade(cells[0], "E8EEF7")
        set_cell_text(cells[1], value, size=10)

    doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = p.add_run("MIT License · https://github.com/gong000000/lab3")
    set_run_font(run, FONT, 9, False, GRAY)


def render_docx(path):
    doc = Document()
    section = doc.sections[0]
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(2.0)
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.2)

    style_document(doc)

    header = section.header.paragraphs[0]
    header.text = "오픈소스프로젝트(전공설계) · Lab 3 결과보고서"
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in header.runs:
        set_run_font(run, FONT, 8.5, False, GRAY)

    footer = section.footer.paragraphs[0]
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = footer.add_run("- ")
    set_run_font(run, FONT, 9, False, GRAY)
    add_page_number(footer)
    run = footer.add_run(" -")
    set_run_font(run, FONT, 9, False, GRAY)

    for block in BLOCKS:
        kind = block[0]
        if kind == "cover":
            add_cover(doc)
        elif kind == "pagebreak":
            doc.add_page_break()
        elif kind == "h1":
            doc.add_heading(block[1], level=1)
        elif kind == "h2":
            doc.add_heading(block[1], level=2)
        elif kind == "h3":
            doc.add_heading(block[1], level=3)
        elif kind == "p":
            p = doc.add_paragraph(block[1])
            p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
        elif kind == "bullets":
            for item in block[1]:
                p = doc.add_paragraph(style="List Bullet")
                p.paragraph_format.space_after = Pt(2)
                run = p.add_run(item)
                set_run_font(run, FONT, 10)
        elif kind == "numbered":
            for item in block[1]:
                p = doc.add_paragraph(style="List Number")
                p.paragraph_format.space_after = Pt(2)
                run = p.add_run(item)
                set_run_font(run, FONT, 10)
        elif kind == "table":
            headers, rows = block[1], block[2]
            widths = block[3] if len(block) > 3 else None
            add_table(doc, headers, rows, widths)
            doc.add_paragraph().paragraph_format.space_after = Pt(0)
        elif kind == "code":
            add_code(doc, block[1])
        elif kind == "quote":
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.5)
            run = p.add_run(block[1])
            set_run_font(run, FONT, 10, False, GRAY)
        else:
            raise ValueError(f"unknown block: {kind}")

    doc.save(path)


# ---------------------------------------------------------------------------
# Markdown rendering
# ---------------------------------------------------------------------------

def md_escape(text):
    return str(text).replace("|", "\\|")


def render_md(path):
    out = []
    for block in BLOCKS:
        kind = block[0]
        if kind == "cover":
            out.append("# 프로젝트 결과보고서\n")
            out.append("## 간단한 쉘(Simple Shell) 구현 및 확장\n")
            out.append("**오픈소스프로젝트(전공설계) · 2026학년도 2학기**\n")
            out.append("| 항목 | 내용 |")
            out.append("|---|---|")
            out.append("| 과목 | 오픈소스프로젝트(전공설계) |")
            out.append("| 프로젝트 | Lab 3 - 간단한 쉘 구현 및 확장 |")
            out.append("| 팀명 | myshell |")
            out.append("| 팀원 | 류동엽 (20223154, @gong000000), 강동우 (20223080, @pizza0420) |")
            out.append("| GitHub | https://github.com/gong000000/lab3 |")
            out.append("| 제출일 | 2026. 11. 18. |")
            out.append("")
        elif kind == "pagebreak":
            out.append("\n---\n")
        elif kind == "h1":
            out.append(f"\n# {block[1]}\n")
        elif kind == "h2":
            out.append(f"\n## {block[1]}\n")
        elif kind == "h3":
            out.append(f"\n### {block[1]}\n")
        elif kind == "p":
            out.append(block[1] + "\n")
        elif kind == "bullets":
            for item in block[1]:
                out.append(f"- {item}")
            out.append("")
        elif kind == "numbered":
            for i, item in enumerate(block[1], 1):
                out.append(f"{i}. {item}")
            out.append("")
        elif kind == "table":
            headers, rows = block[1], block[2]
            out.append("| " + " | ".join(md_escape(h) for h in headers) + " |")
            out.append("|" + "---|" * len(headers))
            for row in rows:
                out.append("| " + " | ".join(md_escape(c) for c in row) + " |")
            out.append("")
        elif kind == "code":
            out.append("```text")
            out.append(block[1])
            out.append("```\n")
        elif kind == "quote":
            out.append("> " + block[1] + "\n")
        else:
            raise ValueError(f"unknown block: {kind}")
    path.write_text("\n".join(out), encoding="utf-8")


def render_html(path):
    css = """
    @page { size: A4; margin: 20mm 22mm; }
    body { font-family: '맑은 고딕', 'Malgun Gothic', sans-serif; font-size: 10pt;
           line-height: 1.45; color: #222; }
    h1 { font-size: 16pt; color: #1F3864; border-bottom: 2px solid #1F3864;
         padding-bottom: 4px; margin-top: 22pt; margin-bottom: 8pt;
         break-after: avoid; page-break-after: avoid; }
    h2 { font-size: 12.5pt; color: #1F3864; margin-top: 14pt; margin-bottom: 5pt;
         break-after: avoid; page-break-after: avoid; }
    h3 { font-size: 11pt; color: #333333; margin-top: 10pt; margin-bottom: 4pt;
         break-after: avoid; page-break-after: avoid; }
    p { margin: 4pt 0 6pt; text-align: justify; orphans: 3; widows: 3; }
    table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt;
            font-size: 9pt; }
    thead { display: table-header-group; }
    tr { break-inside: avoid; page-break-inside: avoid; }
    th, td { border: 1px solid #888888; padding: 4px 6px; vertical-align: top; }
    th { background: #D9E2F3; font-weight: bold; }
    pre { background: #F3F3F3; border: 1px solid #C8C8C8; padding: 7px 9px;
          font-family: Consolas, 'Courier New', monospace; font-size: 8.2pt;
          line-height: 1.25; white-space: pre-wrap; margin: 6pt 0 10pt; }
    ul { margin: 4pt 0 8pt 18pt; padding: 0; }
    li { margin: 1pt 0; }
    .pagebreak { page-break-before: always; }
    .cover { text-align: center; }
    .cover .title { font-size: 28pt; font-weight: bold; color: #1F3864;
                    margin-top: 45mm; }
    .cover .subtitle { font-size: 15pt; font-weight: bold; color: #555555;
                       margin-top: 10pt; }
    .cover .course { font-size: 11pt; color: #666666; margin-top: 6pt; }
    .cover table { width: 72%; margin: 25mm auto 0; font-size: 10pt; }
    .cover .note { font-size: 9pt; color: #777777; margin-top: 20mm; }
    """
    out = [
        "<!DOCTYPE html>",
        "<html lang=\"ko\"><head><meta charset=\"utf-8\">",
        "<title>Lab 3 프로젝트 결과보고서</title>",
        f"<style>{css}</style></head><body>",
    ]

    for block in BLOCKS:
        kind = block[0]
        if kind == "cover":
            out.append("<p style=\"text-align:center; font-size:28pt; font-weight:bold; color:#1F3864;\">프로젝트 결과보고서</p>")
            for _ in range(4):
                out.append("<p>&nbsp;</p>")
            out.append("<p style=\"text-align:center; font-size:15pt; font-weight:bold; color:#555555;\">간단한 쉘(Simple Shell) 구현 및 확장</p>")
            out.append("<p style=\"text-align:center; font-size:11pt; color:#666666;\">오픈소스프로젝트(전공설계) · 2026학년도 2학기</p>")
            for _ in range(6):
                out.append("<p>&nbsp;</p>")
            out.append("<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\" align=\"center\" width=\"82%\">")
            out.append("<tr><th style=\"width:22%; background:#D9E2F3;\">항목</th><th style=\"background:#D9E2F3;\">내용</th></tr>")
            for key, value in (
                ("과목", "오픈소스프로젝트(전공설계)"),
                ("프로젝트", "Lab 3 - 간단한 쉘 구현 및 확장"),
                ("팀명", "myshell"),
                ("팀원", "류동엽 (20223154, @gong000000)<br>강동우 (20223080, @pizza0420)"),
                ("GitHub", "https://github.com/gong000000/lab3"),
                ("제출일", "2026. 11. 18."),
            ):
                out.append(f"<tr><td>{html_escape(key)}</td><td>{value}</td></tr>")
            out.append("</table>")
            for _ in range(3):
                out.append("<p>&nbsp;</p>")
            out.append("<p style=\"text-align:center; font-size:9pt; color:#777777;\">MIT License · https://github.com/gong000000/lab3</p>")
        elif kind == "pagebreak":
            # Chrome/Edge honor this when printing to PDF.  Hancom's HTML
            # importer ignores it, so convert_hwp.py inserts real breaks.
            out.append("<div style=\"page-break-before: always;\"></div>")
        elif kind == "h1":
            out.append(f"<h1>{html_escape(block[1])}</h1>")
        elif kind == "h2":
            out.append(f"<h2>{html_escape(block[1])}</h2>")
        elif kind == "h3":
            out.append(f"<h3>{html_escape(block[1])}</h3>")
        elif kind == "p":
            out.append(f"<p>{html_escape(block[1])}</p>")
        elif kind == "bullets":
            out.append("<ul>")
            for item in block[1]:
                out.append(f"<li>{html_escape(item)}</li>")
            out.append("</ul>")
        elif kind == "numbered":
            out.append("<ol>")
            for item in block[1]:
                out.append(f"<li>{html_escape(item)}</li>")
            out.append("</ol>")
        elif kind == "table":
            headers, rows = block[1], block[2]
            out.append("<table><thead><tr>")
            for h in headers:
                out.append(f"<th>{html_escape(h)}</th>")
            out.append("</tr></thead><tbody>")
            for row in rows:
                out.append("<tr>")
                for cell in row:
                    out.append(f"<td>{html_escape(str(cell))}</td>")
                out.append("</tr>")
            out.append("</tbody></table>")
        elif kind == "code":
            out.append(f"<pre>{html_escape(block[1])}</pre>")
        elif kind == "quote":
            out.append(f"<p style=\"color:#666\">{html_escape(block[1])}</p>")
        else:
            raise ValueError(f"unknown block: {kind}")

    out.append("</body></html>")
    path.write_text("\n".join(out), encoding="utf-8")


def main():
    DOCS.mkdir(parents=True, exist_ok=True)
    md_path = DOCS / "REPORT.md"
    docx_path = DOCS / "REPORT.docx"
    html_path = DOCS / "REPORT.html"
    render_md(md_path)
    render_docx(docx_path)
    render_html(html_path)
    print(f"wrote {md_path}")
    print(f"wrote {docx_path}")
    print(f"wrote {html_path}")


if __name__ == "__main__":
    main()
