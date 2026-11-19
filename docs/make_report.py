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
    ("h2", "1.1 배경"),
    ("p", "교재 357쪽의 simplesh.c는 fgets()로 한 줄을 입력받고, getargs()로 "
          "인자를 나눈 뒤 fork()와 execvp()로 명령을 실행하고 부모가 wait()로 "
          "기다리는 구조이다. 쉘의 기본 원리를 보여 주지만 exit, 백그라운드 실행, "
          "인터럽트 처리, 재지향, 파이프, 내장 명령이 없어 실제 사용에는 부족하다."),
    ("p", "쉘은 사용자와 운영체제 사이에서 명령을 해석하고 프로세스를 생성하는 "
          "프로그램이다. 따라서 쉘을 구현하려면 문자열 파싱, 프로세스 생성, 파일 "
          "디스크립터, 시그널, 프로세스 그룹, 대기 상태를 함께 다루어야 한다. "
          "이 프로젝트는 이 요소들을 직접 구현하는 것에 더해, 실행 중인 쉘과 "
          "프로세스의 내부 상태를 확인할 수 있는 도구를 같이 제공한다."),
    ("h2", "1.2 주제"),
    ("p", "주제는 “시스템 프로그래밍 학습용 투명 쉘”이다. 여기서 투명은 쉘 "
          "자신의 동작과 자식 프로세스의 상태를 사용자가 직접 확인할 수 있다는 "
          "뜻이다. 명령을 실행하는 기능과, 실행 과정을 보여 주는 기능을 같은 "
          "바이너리에 결합했다."),
    ("table",
     ["구분", "내용"],
     [
         ["실행", "명령 해석, 파이프, 재지향, 백그라운드, 내장 명령"],
         ["파서 확인", "explain: 파이프라인·재지향·연결자 구조 출력"],
         ["파이프 확인", "pipestat: 단계별 바이트, 시간, 종료 코드"],
         ["시스템 호출 확인", "trace: ptrace 기반 호출/인자/반환값, -c/-f/-e"],
         ["ELF/프로세스 확인", "dbg: 심볼, 브레이크포인트, 레지스터, 메모리, 백트레이스"],
         ["프로세스 상태 확인", "inspect: /proc status, fd, maps, namespace, process tree"],
         ["세션 기록", "rec/replay: 명령·출력·상태·시간, HTML 내보내기"],
     ]),

    ("h2", "1.3 목표"),
    ("bullets", [
        "exit, &, SIGINT/SIGQUIT, 재지향, 파이프를 구현한다.",
        "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep을 직접 구현한다.",
        "explain과 pipestat으로 파서와 파이프 단계를 확인할 수 있게 한다.",
        "trace와 dbg로 시스템 호출과 ELF 프로그램 실행을 추적한다.",
        "inspect로 /proc 기반 프로세스 상태를 조사한다.",
        "rec/replay로 세션을 기록하고 결과를 HTML로 남긴다.",
        "자동 테스트로 기능과 회귀를 검증한다.",
    ]),

    ("h2", "1.4 구현 범위"),
    ("table",
     ["구분", "내용"],
     [
         ["필수 기능", "exit, 백그라운드(&), SIGINT/SIGQUIT, 재지향(>, <), 파이프(|)"],
         ["필수 내장 명령", "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개)"],
         ["보조 내장 명령", "echo, export, unset, jobs, wait, help, true, false, :"],
         ["문법", "따옴표, $VAR, $?, $$, $!, $(...), glob, &&, ||, ;, &"],
         ["관찰 도구", "explain, pipestat, trace, dbg, inspect"],
         ["기록 도구", "rec, replay, HTML export"],
         ["문서/배포", "README, MIT LICENSE, 결과보고서, 테스트 스크립트"],
     ]),

    ("h2", "1.5 개발 환경"),
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

    ("h2", "1.6 결과 요약"),
    ("p", "직접 작성한 소스는 8,626줄, 생성 테이블을 포함하면 8,763줄이다. "
          "테스트 코드 1,214줄을 포함한 전체 합계는 9,977줄이다. 필수 기능 5개와 "
          "11개 내장 명령, 관찰 도구 5종, 기록 도구를 구현했고 263개 자동 테스트와 "
          "PTY 시그널 테스트를 통과했다."),
    ("table",
     ["항목", "결과"],
     [
         ["제품 소스", "직접 작성 8,626줄 + 생성 137줄 = 8,763줄"],
         ["테스트 코드", "1,214줄"],
         ["전체 합계", "9,977줄"],
         ["필수 내장 명령", "11개"],
         ["기능 테스트", "94 / 94"],
         ["확장 테스트", "21 / 21"],
         ["파일 입력", "4 / 4"],
         ["추적", "32 / 32"],
         ["디버거", "46 / 46"],
         ["녹화", "35 / 35"],
         ["조사", "31 / 31"],
         ["시그널", "PTY 테스트 통과"],
     ]),

    ("pagebreak",),
    ("h1", "2. 설계 구성 요소"),
    ("table",
     ["구성 요소", "수행 내용", "확인"],
     [
         ["목표/기준 설정", "Lab 3의 5개 요구사항, 11개 내장 명령, 관찰 도구 6종을 목표로 설정", "기능 94개"],
         ["합성", "libc/POSIX 시스템 콜과 기존 도구를 조합해 쉘과 관찰 도구를 구성", "외부 라이브러리 없음"],
         ["분석", "simplesh.c, 파서/실행기, 프로세스/시그널, 파일·텍스트 명령, ptrace/ELF/proc 분석", "모듈 분리"],
         ["제작", "main/parser/execute/jobs/builtin/cmd_* 모듈과 Makefile 작성", "8,763줄"],
         ["시험", "기능·확장·추적·디버거·녹화·조사·PTY 테스트", "263개 + PTY"],
         ["평가", "요구사항 충족, 완성도, 일정, 역할, 위험 처리 정리", "5장"],
         ["결과 도출", "README, LICENSE, 결과보고서, 자동 테스트 스크립트", "GitHub 공개"],
     ]),
    ("h2", "2.1 전체 구조"),
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
          | 11개 명령 + 도구     |   | (/bin/ls, date, ...)      |
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

    ("h2", "2.2 모듈"),
    ("table",
     ["파일", "역할"],
     [
         ["include/myshell.h", "공용 상수, 구조체, 함수 원형"],
         ["src/main.c", "REPL, 명령 분리, 시그널, 터미널/프로세스 그룹"],
         ["src/parser.c", "토큰화, 따옴표/변수/$(...) 확장, 문법 분석"],
         ["src/execute.c", "fork/exec, 파이프, 재지향, 명령 치환 캡처"],
         ["src/jobs.c", "백그라운드 작업 테이블과 좀비 회수"],
         ["src/builtin.c", "내장 명령 디스패치와 cd/echo/exit/jobs/wait"],
         ["src/cmd_ls.c", "ls 구현"],
         ["src/cmd_fs.c", "mkdir, rmdir, ln, cp, rm, mv"],
         ["src/cmd_text.c", "cat, grep"],
         ["src/cmd_explain.c", "파서 결과 시각화"],
         ["src/cmd_pipestat.c", "파이프 단계 계측"],
         ["src/cmd_trace.c", "ptrace 시스템 호출 추적"],
         ["src/cmd_dbg.c", "ELF/ptrace 디버거"],
         ["src/cmd_inspect.c", "/proc 기반 프로세스 조사"],
         ["src/cmd_rec.c", "세션 녹화/재생/HTML"],
         ["src/trace_syscalls.h", "시스템 호출 이름 테이블 (생성)"],
         ["src/util.c", "메모리, 경로, 파일 복사/삭제, 모드/크기 출력"],
     ]),

    ("h2", "2.3 자료구조"),
    ("table",
     ["구조체", "주요 필드", "역할"],
     [
         ["redirect_t", "in_path, out_path, err_path, append, merge",
          "한 명령의 입출력/에러 재지향"],
         ["command_t", "argv[], argc, redir", "한 명령의 인자와 재지향"],
         ["pipeline_t", "commands[], ncommands, background, text",
          "파이프로 연결된 명령 묶음"],
         ["command_list_t", "pipelines[], connectors[], npipelines",
          "&&/||/;로 연결된 명령 목록"],
         ["job_t", "id, pgid, pids[], done[], running, status, command",
          "백그라운드 작업의 프로세스 그룹과 상태"],
         ["shell_state_t", "last_status, jobs[], positional[]",
          "쉘의 종료 상태와 작업 테이블"],
         ["tracee_t", "pid, entered, nr, args[6], string_arg",
          "ptrace 추적 중인 프로세스의 호출 상태"],
         ["dbg_t / dbg_bp_t", "pid, bias, symbols[], breakpoints[], orig_word",
          "디버거의 ELF 심볼, PIE 기준 주소, 브레이크포인트"],
         ["rec_item_t", "cmd, out, err, diag, status, ms",
          "녹화된 명령 하나의 출력과 메타데이터"],
     ]),

    ("h2", "2.4 실행 흐름"),
    ("h3", "2.4.1 입력과 파싱"),
    ("p", "main.c의 find_separator()가 따옴표와 이스케이프를 고려해 최상위 ;와 "
          "&를 찾고, 각 세그먼트를 parser.c로 넘긴다. parser.c는 단어와 연산자를 "
          "분리하고 작은따옴표(문자 그대로), 큰따옴표($ 확장 허용), 백슬래시를 "
          "처리한다. $VAR, ${VAR}, $?, $$, $!, $(...)를 확장하고, 인용되지 않은 "
          "*, ?, []는 glob()으로 전개한다."),
    ("p", "문법은 명령 목록, 파이프라인, 명령, 재지향으로 나뉜다. &&와 ||는 "
          "connector_t로 기록되어 execute_command_list()가 직전 종료 상태에 따라 "
          "실행 여부를 결정한다. 2>, 2>>, 2>&1, &>, &>>는 redirect_t에 저장된다."),
    ("code", """list     := pipeline ((';' | '&') pipeline)*
pipeline := command ('|' command)*
command  := word+ redirect*"""),

    ("h3", "2.4.2 프로세스와 재지향"),
    ("p", "파이프라인은 명령 수만큼 pipe()와 fork()를 사용한다. 첫 자식이 "
          "setpgid(0, 0)로 프로세스 그룹을 만들고 이후 자식과 부모가 같은 pgid에 "
          "참여한다. 부모는 tcsetpgrp()로 터미널 전경 그룹을 자식에게 넘기고, "
          "모든 자식의 waitpid() 결과를 수집한 뒤 다시 쉘 그룹으로 복원한다."),
    ("p", "재지향은 파이프 연결 뒤에 적용된다. open()/dup2()로 표준 fd를 바꾸고 "
          "원래 fd를 닫는다. 2>&1은 stdout을 stderr에 복제하고, &>는 stdout과 "
          "stderr를 같은 파일로 보낸다. 내장 명령 중 cd, exit, jobs, wait, "
          "export처럼 쉘 상태를 바꾸는 명령은 부모에서 실행하며, 재지향이 있으면 "
          "fd를 저장·복원한다."),

    ("h3", "2.4.3 시그널과 작업 제어"),
    ("p", "쉘은 SIGINT/SIGQUIT/SIGTERM/SIGHUP 핸들러를 설치한다. 전경 작업 중에는 "
          "터미널 전경 그룹이 자식이므로 Ctrl-C는 자식에게 전달되고, 자식은 "
          "fork 직후 SIGINT/SIGQUIT/SIGTSTP 등을 기본값으로 복원한다. 백그라운드 "
          "작업은 job_t에 등록하고 waitpid(WNOHANG)로 주기적으로 회수한다."),
    ("p", "과제 명세의 Ctrl-Z=SIGQUIT을 맞추기 위해 termios의 VQUIT를 0x1A로 "
          "설정하고 VSUSP를 비활성화한다. 쉘 종료 시 저장한 termios를 복원한다."),

    ("h3", "2.4.4 내장 명령"),
    ("p", "11개 필수 명령은 cmd_ls.c, cmd_fs.c, cmd_text.c에 구현되어 있다. "
          "ls는 opendir/readdir/lstat를 사용하고 -a, -l, -R, -h를 지원한다. "
          "cp/rm/mv는 재귀 처리와 경로 검사를 포함하고, cat은 -n/-b/-E/-T, "
          "grep은 POSIX regex와 -i/-v/-n/-c/-l/-r/-E/-F/-q 등을 지원한다. "
          "echo, export, unset, jobs, wait, help, true, false, :는 builtin.c에 있다."),

    ("h3", "2.4.5 explain"),
    ("p", "explain은 parse_line()을 실행하되 명령을 실행하지 않고, 파이프라인·"
          "명령·재지향·백그라운드·연결자 구조를 출력한다. 예를 들어 "
          "cat a.txt | grep hello > out.txt는 두 개의 명령과 stdout 재지향을 "
          "가진 파이프라인으로 표시된다. 파서가 입력을 어떻게 해석했는지 확인하는 "
          "도구이다."),
    ("code", """explain: 1 pipeline(s)

[pipeline 1] foreground (first)
  ├─ cmd: cat a.txt
  └─ cmd: grep hello
       ├─ stdout → out.txt (truncate)"""),

    ("h3", "2.4.6 pipestat"),
    ("p", "pipestat는 각 파이프 단계와 다음 단계 사이에 relay 프로세스를 넣는다. "
          "relay는 데이터를 전달하면서 바이트 수를 세고, 부모는 단계별 시작/종료 "
          "시각과 종료 코드를 수집한다. 출력은 단계 번호, 명령, 바이트 수, "
          "소요 시간, 상태로 구성된다."),
    ("code", """shell> pipestat 'echo hello | cat | wc -c'
6

[1] echo hello: 6 bytes, 0.000s, status 0
[2] cat: 6 bytes, 0.000s, status 0
[3] wc -c: 2 bytes, 0.003s, status 0"""),

    ("h3", "2.4.7 trace"),
    ("p", "trace는 자식에서 PTRACE_TRACEME 후 SIGSTOP으로 멈추고, 부모가 "
          "PTRACE_SETOPTIONS와 PTRACE_SYSCALL로 추적을 시작한다. 시스템 호출 "
          "진입/종료 정지에서 PTRACE_GETREGS로 orig_rax, 인자 레지스터, rax를 "
          "읽는다. 경로 인자는 PTRACE_PEEKDATA로 자식 메모리를 읽어 문자열로 "
          "표시한다. -c는 호출 횟수 요약, -f는 fork/vfork/clone 자식 추적, -e는 "
          "이름 필터이다. 이름 테이블은 생성된 trace_syscalls.h를 사용한다."),
    ("code", """shell> trace -e write /bin/echo hi
hi
write(1, 0x..., 3) = 3"""),

    ("h3", "2.4.8 dbg"),
    ("p", "dbg는 ELF64의 .symtab(없으면 .dynsym)에서 함수 심볼을 읽는다. PIE "
          "실행 파일은 /proc/PID/maps에서 로드 기준 주소를 계산해 심볼 주소에 "
          "더한다. break는 대상 주소의 첫 바이트를 0xCC(int3)로 바꾸고, 정지하면 "
          "원래 바이트를 복원한 뒤 rip를 1 되돌린다. step/continue는 한 명령을 "
          "실행한 뒤 0xCC를 다시 심는다. finish는 프레임의 복귀 주소에 임시 "
          "브레이크포인트를 설치한다. bt는 rbp 체인을 따라 반환 주소를 심볼로 "
          "바꾸고, SIGSEGV 등에서는 rip와 백트레이스를 자동 출력한다."),
    ("code", """shell> dbg ./a.out
(dbg) break main
Breakpoint 1 at 0x401148 (main)
(dbg) run
Breakpoint 1, 0x401148 in main ()
(dbg) regs
rip=0x401148 rsp=0x7fffffffe4d8 rbp=0x7fffffffe580
(dbg) bt
#0  0x401148 in main ()
(dbg) step
Stopped at 0x40114c in main ()
(dbg) continue
v=42
[Inferior exited with status 0]"""),

    ("h3", "2.4.9 inspect"),
    ("p", "inspect는 /proc를 읽어 쉘과 다른 프로세스의 상태를 보여 준다. 기본 "
          "출력은 pid/ppid/pgid/sid, uid/gid, cwd, hostname, umask, rlimit, "
          "status, fd, maps, namespace이다. -f는 fd, -m은 memory map, -n은 "
          "namespace, -j는 jobs, -s는 status, -t는 process tree만 출력한다."),
    ("p", "process tree는 /proc/*/stat의 comm, state, ppid를 읽고 루트 PID의 "
          "자식을 재귀적으로 따라간다. 예를 들어 백그라운드 sleep을 실행한 "
          "직후 inspect -t를 호출하면 쉘 아래에 sleep 프로세스가 표시된다."),
    ("code", """shell> sleep 2 & inspect -t; wait
process tree (root=...):
myshell (...) [R]
  sleep (...) [R]"""),

    ("h3", "2.4.10 rec/replay"),
    ("p", "rec는 전경 명령의 stdout/stderr를 relay 프로세스로 실시간 중계하면서 "
          "임시 파일에도 기록한다. 명령이 끝나면 명령, stdout, stderr, 종료 코드, "
          "소요 시간을 길이 기반 레코드로 저장한다. 백그라운드(&) 명령은 지연 "
          "캡처 경로를 사용한다. replay는 명령을 다시 실행하지 않고 저장된 출력과 "
          "상태를 재표시하며, -s로 지연을 조절한다. rec export는 같은 레코드를 "
          "단독 HTML 타임라인으로 변환한다. rec start -x는 explain과 trace -c "
          "요약을 추가한다."),
    ("code", """shell> rec start lab.rec
recording to lab.rec
shell> echo hello
hello
shell> rec stop
recording stopped: lab.rec (2 commands)
shell> replay lab.rec -s 0
$ echo hello
hello
[status=0, ...ms]"""),

    ("pagebreak",),
    ("h2", "2.5 설계 구성 요소와 제한 요소의 관계"),
    ("table",
     ["구성 요소", "관련 제한 요소", "본 프로젝트의 처리"],
     [
         ["목표/기준 설정", "경제성", "필수 기능과 관찰 도구를 정하고 소스·테스트 합계를 9,977줄로 관리"],
         ["분석", "안정성", "파서/실행기/작업 제어를 모듈로 분리하고 오류 반환값 검사"],
         ["제작", "생산성/표준성", "11개 명령과 도구를 POSIX/Linux 인터페이스로 구현"],
         ["시험", "신뢰성", "263개 자동 테스트와 PTY 시그널 테스트로 회귀 확인"],
         ["평가", "윤리성/사회적 영향", "rm 안전장치, system/popen 미사용, MIT 라이선스"],
     ]),

    ("pagebreak",),
    ("h1", "3. 설계 제한 요소"),
    ("h2", "3.1 경제성"),
    ("p", "소스 규모는 다음 기준으로 계산했다."),
    ("table",
     ["항목", "줄 수"],
     [
         ["include/myshell.h + src/*.c", "8,626"],
         ["빈 줄", "1,007"],
         ["주석", "164"],
         ["주석·빈 줄 제외 코드", "7,455"],
         ["생성된 trace_syscalls.h", "137"],
         ["테스트 코드(별도)", "1,214"],
         ["제품 소스 합계", "8,763"],
         ["전체 합계", "9,977"],
     ]),
    ("p", "기능별 파일 분리와 공통 util 사용으로 모듈 간 결합을 줄였다. 외부 "
          "라이브러리 없이 libc/POSIX 인터페이스만 사용해 빌드 의존성을 최소화했다."),

    ("h2", "3.2 안정성"),
    ("p", "시스템 호출과 라이브러리 호출의 반환값을 검사하고 errno를 포함한 "
          "메시지를 출력한다. 백그라운드 작업은 주기적으로 회수하고 종료 시 "
          "정리한다. 파이프/재지향 실패 시 fd를 복원하고, 터미널 종료 시 termios를 "
          "복구한다. 기능·확장·추적·디버거·녹화·조사 테스트로 회귀를 확인한다."),

    ("h2", "3.3 생산성"),
    ("p", "11개 명령은 외부 프로그램 호출 없이 동작한다. explain과 pipestat은 "
          "파이프라인 구조와 단계별 비용을, trace와 dbg는 시스템 호출과 ELF 실행을, "
          "inspect는 /proc 상태를, rec은 세션 전체를 보여 준다. 실행 결과를 HTML로 "
          "남길 수 있어 별도 서버 없이 확인할 수 있다."),

    ("h2", "3.4 윤리성"),
    ("p", "rm은 /, ., ..를 거부하고 재귀 삭제는 심볼릭 링크를 따라가지 않는다. "
          "외부 명령은 execvp()로 직접 실행하며 system()/popen()을 사용하지 않는다. "
          "MIT 라이선스로 수정과 재배포를 허용한다."),

    ("h2", "3.5 표준성과 이식성"),
    ("p", "POSIX/Linux 시스템 콜과 C 라이브러리를 사용한다. Ubuntu 26.04 WSL2와 "
          "gcc 15.2에서 빌드·검증했으며, 터미널 제어와 ptrace 기능은 Linux "
          "환경을 전제로 한다."),

    ("h2", "3.6 신뢰성과 내구성"),
    ("p", "같은 테스트를 반복 실행해도 동일한 결과가 나오도록 임시 디렉터리와 "
          "종료 상태를 사용한다. 263개 자동 테스트와 PTY 시그널 테스트를 통과했고, "
          "오류 반환값과 파일 디스크립터 복원을 검사한다."),

    ("pagebreak",),
    ("h1", "4. 설계 내용: 구현 및 검증"),
    ("h2", "4.1 요구사항 1~5"),
    ("table",
     ["요구사항", "구현", "검증"],
     [
         ["1. exit", "builtin_exit, $?, 종료 상태 저장", "exit 7 후 상태 7"],
         ["2. &", "job_t, setpgid, jobs/wait/$!", "백그라운드 조기 복귀, jobs/wait"],
         ["3. Ctrl-C/Ctrl-Z", "SIGINT/SIGQUIT, 전경 프로세스 그룹, VQUIT 매핑", "PTY 시그널 테스트"],
         ["4. 재지향/파이프", "open/dup2/pipe, 2>&1, 다단 파이프", "파일 비교/에러 분리/파이프"],
         ["5. 11개 명령", "cmd_ls/cmd_fs/cmd_text, 옵션 처리", "94개 기능 테스트"],
     ]),

    ("h2", "4.2 도구별 검증"),
    ("table",
     ["도구", "검증 내용", "테스트"],
     [
         ["explain", "파이프/재지향/백그라운드 구조 출력", "확장 테스트"],
         ["pipestat", "단계별 바이트/시간/종료 코드", "확장 테스트"],
         ["trace", "write/openat/read/execve/exit_group, -c/-f/-e", "추적 32개"],
         ["dbg", "break/run/step/finish/bt/regs/x, PIE, SIGSEGV", "디버거 46개"],
         ["inspect", "status/fd/maps/ns/tree/jobs/limits", "조사 31개"],
         ["rec/replay", "출력/상태/시간, HTML, -x, 오류", "녹화 35개"],
     ]),

    ("h2", "4.3 테스트 방법"),
    ("p", "기능 테스트는 쉘을 -c로 실행해 stdout, stderr, 종료 상태를 기대값과 "
          "비교한다. 파일 시스템 테스트는 임시 디렉터리에서 수행하고 종료 후 "
          "삭제한다. PTY 테스트는 pty.fork()로 가상 터미널을 만들고 Ctrl-C와 "
          "Ctrl-Z를 전송해 쉘 생존과 프롬프트 복귀를 확인한다. 실행 권한은 "
          "tests/ 아래 스크립트에 100755로 설정되어 있다."),

    ("h2", "4.4 테스트 결과"),
    ("table",
     ["영역", "개수", "결과"],
     [
         ["기능", "94", "통과"],
         ["파일 입력", "4", "통과"],
         ["확장(explain/&&/glob/pipestat)", "21", "통과"],
         ["시스템 호출 추적", "32", "통과"],
         ["디버거", "46", "통과"],
         ["녹화/재생", "35", "통과"],
         ["프로세스 조사", "31", "통과"],
         ["PTY 시그널", "1 시나리오", "통과"],
         ["합계", "263", "통과"],
     ]),
    ("code", """Passed: 94
File-input tests passed: 4
Extension tests passed: 21
Trace tests passed: 32
Debugger tests passed: 46
Recording tests passed: 35
Inspect tests passed: 31
SIGNAL TEST PASSED"""),

    ("h2", "4.5 실행 예시"),
    ("code", """shell> explain 'cat a.txt | grep hello > out.txt'
explain: 1 pipeline(s)
[pipeline 1] foreground (first)
  ├─ cmd: cat a.txt
  └─ cmd: grep hello
       ├─ stdout → out.txt (truncate)

shell> pipestat 'echo hello | cat | wc -c'
6
[1] echo hello: 6 bytes, 0.000s, status 0
[2] cat: 6 bytes, 0.000s, status 0
[3] wc -c: 2 bytes, 0.003s, status 0

shell> trace -e write /bin/echo hi
hi
write(1, 0x..., 3) = 3

shell> inspect -t
process tree (root=...):
myshell (...) [R]"""),

    ("pagebreak",),
    ("h1", "5. 프로젝트 결과"),
    ("h2", "5.1 구현 현황"),
    ("table",
     ["기능", "상태", "검증"],
     [
         ["exit / & / signals / redirection / pipe", "완료", "기능 94"],
         ["11개 필수 명령", "완료", "기능 94"],
         ["explain / pipestat", "완료", "확장 21"],
         ["trace", "완료", "추적 32"],
         ["dbg", "완료", "디버거 46"],
         ["inspect + process tree", "완료", "조사 31"],
         ["rec / replay / HTML", "완료", "녹화 35"],
     ]),

    ("h2", "5.2 완성도 평가"),
    ("p", "원 과제의 5개 요구사항과 11개 내장 명령을 모두 구현했고, 94개 기능 "
          "테스트로 확인했다. 추가 도구는 확장 21개, 추적 32개, 디버거 46개, "
          "녹화 35개, 조사 31개 테스트로 검증했으며, PTY 시그널 테스트도 통과했다. "
          "전체 제품 소스와 테스트는 9,977줄이다."),
    ("p", "기능별 결과는 5.1에, 요구사항별 대응은 4.1에 정리했다. 테스트는 "
          "stdout·stderr·종료 상태를 비교하는 방식과 PTY 기반 시그널 검증을 "
          "함께 사용했다."),

    ("h2", "5.3 일정 및 주차별 기록"),
    ("table",
     ["주차", "류동엽", "강동우", "산출물"],
     [
         ["11주", "simplesh 분석, 요구사항/파서 설계", "파일/텍스트 명령 분석", "요구사항 표"],
         ["12주", "토큰화/확장/파이프/재지향", "ls/mkdir/rmdir 구현", "parser.c, execute.c"],
         ["13주", "프로세스 그룹/시그널/작업 제어", "ln/cp/rm/mv 경계 처리", "PTY 테스트"],
         ["14주", "11개 명령 통합, 기능 테스트", "cat/grep 옵션", "Passed: 94"],
         ["15주", "explain/pipestat/trace/dbg/inspect/rec", "테스트/문서/PTY", "도구별 테스트"],
     ]),
    ("p", "각 주차의 구현·테스트·문서 산출물은 표와 같이 남겼고, 통합 전에는 "
          "기능 테스트와 PTY 테스트를 다시 실행해 회귀를 확인했다."),

    ("h2", "5.4 역할 수행 평가"),
    ("table",
     ["팀원", "학번", "담당", "확인한 산출물"],
     [
         ["류동엽", "20223154", "쉘 코어, 파서/실행기, 시그널, explain/pipestat/trace/dbg/inspect/rec, pwd/cd/ls/mkdir/rmdir", "기능 94, 추적 32, 디버거 46, 녹화 35, 조사 31"],
         ["강동우", "20223080", "ln/cp/rm/mv/cat/grep, 테스트, 문서, PTY 검증", "명령 기능 테스트, PTY 시그널, 문서"],
     ]),
    ("p", "명령 구현은 5개(pwd, cd, ls, mkdir, rmdir)와 6개(ln, cp, rm, mv, "
          "cat, grep)로 나누었다. 두 사람이 서로의 테스트를 실행해 통합 오류를 "
          "확인했다."),

    ("h2", "5.5 위험 처리 평가"),
    ("table",
     ["위험", "대응", "결과"],
     [
         ["전경 작업 시그널", "setpgid + tcsetpgrp, 자식 신호 기본값 복원", "PTY 통과"],
         ["좀비 프로세스", "waitpid(WNOHANG) 주기 회수, wait 내장", "jobs 테스트"],
         ["파일 디스크립터 누수", "dup2 후 close, 재지향 실패 복원", "기능 테스트"],
         ["PIE 주소 불일치", "/proc/PID/maps bias 계산", "dbg PIE 테스트"],
         ["브레이크포인트 재정지", "원본 복원→SINGLESTEP→재설치", "dbg step/continue"],
         ["ptrace 차단", "PTRACE_TRACEME로 자기 자식만 추적", "WSL2 테스트"],
         ["녹화 출력", "전경 relay, 백그라운드 지연 캡처", "rec 테스트"],
     ]),

    ("h2", "5.6 저장소"),
    ("p", "소스, 테스트, README, LICENSE, 보고서를 GitHub에 공개했다. 저장소 "
          "주소는 https://github.com/gong000000/lab3 이고 MIT 라이선스를 사용한다."),

    ("h1", "6. 소감"),
    ("h2", "류동엽"),
    ("p", "프로세스 그룹과 터미널 제어, ptrace, ELF, /proc을 직접 다루면서 쉘이 "
          "운영체제와 상호작용하는 프로그램임을 확인했다. 기능을 추가할 때마다 "
          "기존 동작을 깨지 않도록 테스트를 먼저 확인하는 습관이 중요했다."),
    ("h2", "강동우"),
    ("p", "파일·텍스트 명령의 경계 조건과 테스트를 정리하면서 오류 처리와 "
          "사용자 메시지의 일관성이 중요하다는 것을 배웠다. trace/dbg/rec/inspect "
          "시나리오를 검증하며 도구 간 출력 형식을 맞추는 작업도 진행했다."),
    ("h2", "함께"),
    ("p", "구현과 테스트를 분리해 진행하고, 통합 전에 전체 테스트를 실행해 "
          "회귀를 확인했다. 기능 수를 늘리는 것보다 동작을 검증하고 설명하는 "
          "과정이 결과보고서 작성에 도움이 되었다."),

    ("pagebreak",),
    ("h1", "부록 A. 빌드 및 실행"),
    ("code", """make clean
make
./myshell
./myshell -c 'exit 7'
./myshell -c 'explain "cat a | grep b > out"'
./myshell -c 'pipestat "echo hi | cat | wc -c"'
./myshell -c 'trace -e write /bin/echo hi'
./myshell -c 'inspect -t'
make test"""),
    ("h1", "부록 B. 테스트 항목"),
    ("table",
     ["영역", "주요 검사", "개수"],
     [
         ["기능", "exit, 인용/확장, 재지향, 파이프, 작업, 11개 명령, 오류", "94"],
         ["확장", "explain, &&/||, glob, pipestat", "21"],
         ["파일 입력", "여러 줄 입력, 마지막 줄 중복, 백그라운드", "4"],
         ["추적", "필터, 요약, 자식 추적, 경로 문자열, 오류", "32"],
         ["디버거", "ELF/PIE, break/step/finish/bt, SIGSEGV", "46"],
         ["녹화", "rec/replay, 출력, HTML, -x, 오류", "35"],
         ["조사", "status/fd/maps/ns/tree/jobs/limits", "31"],
         ["시그널", "PTY Ctrl-C/Ctrl-Z", "1 시나리오"],
     ]),
    ("h1", "부록 C. 소스 규모"),
    ("table",
     ["항목", "줄 수"],
     [
         ["include/myshell.h + src/*.c", "8,626"],
         ["생성된 trace_syscalls.h", "137"],
         ["제품 소스 합계", "8,763"],
         ["테스트 코드", "1,214"],
         ["전체 합계", "9,977"],
     ]),
    ("h1", "부록 D. 참고문헌"),
    ("bullets", [
        "교재 357쪽, simplesh.c (fgets/getargs/fork/execvp/wait)",
        "Linux man pages: fork(2), execvp(3), waitpid(2), pipe(2), dup2(2), signal(7), sigaction(2), termios(3), tcsetpgrp(3)",
        "Linux man pages: opendir(3), readdir(3), lstat(2), link(2), symlink(2), rename(2), unlink(2), rmdir(2)",
        "POSIX regex: regcomp(3), regexec(3), regerror(3)",
        "Linux man pages: ptrace(2), waitpid(2) __WALL, sys/user.h, proc(5)",
        "Linux man pages: getrlimit(2), readlink(2), gethostname(2)",
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
    run = p.add_run("시스템 프로그래밍 학습용 투명 쉘")
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
        ("프로젝트", "Lab 3 - 시스템 프로그래밍 학습용 투명 쉘"),
        ("팀명", "myshell"),
        ("팀원", "류동엽 (20223154, @gong000000)\n강동우 (20223080, @pizza0420)"),
        ("GitHub", "https://github.com/gong000000/lab3"),
        ("제출일", "2026. 11. 19."),
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
            out.append("## 시스템 프로그래밍 학습용 투명 쉘\n")
            out.append("**오픈소스프로젝트(전공설계) · 2026학년도 2학기**\n")
            out.append("| 항목 | 내용 |")
            out.append("|---|---|")
            out.append("| 과목 | 오픈소스프로젝트(전공설계) |")
            out.append("| 프로젝트 | Lab 3 - 시스템 프로그래밍 학습용 투명 쉘 |")
            out.append("| 팀명 | myshell |")
            out.append("| 팀원 | 류동엽 (20223154, @gong000000), 강동우 (20223080, @pizza0420) |")
            out.append("| GitHub | https://github.com/gong000000/lab3 |")
            out.append("| 제출일 | 2026. 11. 19. |")
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
            out.append("<p style=\"text-align:center; font-size:15pt; font-weight:bold; color:#555555;\">시스템 프로그래밍 학습용 투명 쉘</p>")
            out.append("<p style=\"text-align:center; font-size:11pt; color:#666666;\">오픈소스프로젝트(전공설계) · 2026학년도 2학기</p>")
            for _ in range(6):
                out.append("<p>&nbsp;</p>")
            out.append("<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\" align=\"center\" width=\"82%\">")
            out.append("<tr><th style=\"width:22%; background:#D9E2F3;\">항목</th><th style=\"background:#D9E2F3;\">내용</th></tr>")
            for key, value in (
                ("과목", "오픈소스프로젝트(전공설계)"),
                ("프로젝트", "Lab 3 - 시스템 프로그래밍 학습용 투명 쉘"),
                ("팀명", "myshell"),
                ("팀원", "류동엽 (20223154, @gong000000)<br>강동우 (20223080, @pizza0420)"),
                ("GitHub", "https://github.com/gong000000/lab3"),
                ("제출일", "2026. 11. 19."),
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
