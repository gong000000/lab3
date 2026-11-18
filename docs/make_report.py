#!/usr/bin/env python3
"""Generate docs/REPORT.md and docs/REPORT.docx for the Lab 3 project.

The DOCX is later converted to HWP/PDF with Hancom Office (see
convert_hwp.py).  Keeping the report source in one place makes the
Markdown and DOCX versions consistent.
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
          "분리한 뒤 fork()와 execvp()로 명령을 실행하고 부모가 wait()로 기다리는 "
          "가장 기본적인 쉘이다. 이 프로그램은 쉘의 핵심 원리(명령 해석, 프로세스 "
          "생성, 프로그램 적재, 부모-자식 동기화)를 보여 주지만, 실제 쉘에 필요한 "
          "종료 처리, 백그라운드 실행, 인터럽트 처리, 재지향, 파이프, 내장 명령이 "
          "모두 빠져 있다."),
    ("p", "본 프로젝트는 simplesh.c의 구조를 그대로 유지하면서 위 기능을 단계적으로 "
          "확장하여, 리눅스에서 실제로 사용할 수 있는 작은 POSIX 쉘(myshell)을 "
          "완성하는 것을 목표로 한다. 단순히 외부 명령을 실행하는 수준을 넘어, "
          "과제에서 요구한 11개 명령(ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, "
          "cat, grep)을 시스템 콜과 C 라이브러리로 직접 구현하였다."),
    ("p", "여기에 TCP 기반 원격 쉘(netserv, netcli)을 추가했다. 서버는 셸의 "
          "파서와 실행기를 그대로 사용해 원격 명령을 실행하고, 토큰 인증, 동시 "
          "접속, 명령 타임아웃, stdout/stderr 캡처를 지원한다."),

    ("h2", "1.2 프로젝트 목표"),
    ("bullets", [
        "교재의 simplesh.c를 기반으로 명령 해석 → 프로세스 생성 → 실행 → 대기의 "
        "전체 흐름을 이해하고 구현한다.",
        "exit, &, SIGINT/SIGQUIT, 재지향(>, <), 파이프(|)를 추가하여 실제 쉘과 "
        "유사한 사용성을 제공한다.",
        "11개 명령을 외부 프로그램 호출 없이 직접 구현하여 파일 시스템과 텍스트 "
        "처리 시스템 콜을 익힌다.",
        "TCP 원격 쉘 서버/클라이언트를 구현해 네트워크 시스템 프로그래밍을 적용한다.",
        "자동화된 기능 테스트, 네트워크 테스트, PTY 시그널 테스트로 안정성을 검증한다.",
        "MIT 라이선스로 GitHub에 공개한다.",
    ]),

    ("h2", "1.3 구현 범위"),
    ("table",
     ["구분", "내용"],
     [
         ["필수 기능", "exit, 백그라운드(&), SIGINT/SIGQUIT, 재지향(>, <), 파이프(|)"],
         ["필수 내장 명령", "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep"],
         ["추가 내장 명령", "echo, export, unset, jobs, wait, help, true, false, :"],
         ["네트워크 확장", "netserv(원격 쉘 서버), netcli(원격 쉘 클라이언트)"],
         ["파서/문법 확장", "&&, ||, glob(*, ?, []), explain(파서 시각화)"],
         ["편의 기능", "undo/휴지통, serve(HTTP), watch(주기 실행), prompt(확장 프롬프트)"],
         ["추가 문법", ">>, 2>, 2>>, 2>&1, &>, &>>, 다단 파이프, 세미콜론(;), 따옴표, 변수 확장"],
         ["문서/배포", "README, MIT LICENSE, 결과보고서(HWP/PDF), 자동 테스트"],
     ]),

    ("pagebreak",),
    ("h2", "1.4 개발 환경"),
    ("table",
     ["항목", "내용"],
     [
         ["운영체제", "Ubuntu 26.04.1 LTS (WSL2, Linux 6.6)"],
         ["컴파일러", "gcc 15.2.0 (GNU C17, -std=gnu11)"],
         ["빌드 도구", "GNU Make 4.4.1"],
         ["테스트", "Bash 5.x, Python 3.14 (pty/select 기반 시그널 테스트)"],
         ["버전 관리", "Git 2.53, GitHub (gong000000/lab3)"],
         ["외부 라이브러리", "glibc, POSIX regex 외 없음"],
     ]),

    ("h2", "1.5 결과 요약"),
    ("p", "최종 프로그램은 총 6,913줄의 C 소스(헤더 포함)로 구성되며, 11개 필수 "
          "내장 명령과 9개 보조 내장 명령, TCP 원격 쉘을 제공한다. 기능 테스트 "
          "94개, 확장 테스트 32개, 네트워크 테스트 23개, 파일 입력 테스트 4개와 PTY 시그널 "
          "테스트를 통과했고, ASan/UBSan 빌드에서도 기능 테스트를 통과했다. "
          "저장소에는 MIT LICENSE와 결과보고서, 테스트 스크립트가 포함되어 있다."),
    ("table",
     ["항목", "결과"],
     [
         ["소스 규모", "헤더 포함 6,913줄 (설계 지침 1,000~10,000줄 충족)"],
         ["필수 내장 명령", "ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개)"],
         ["보조 내장 명령", "echo, export, unset, jobs, wait, help, true, false, : (9개)"],
         ["기능 테스트", "94 / 94 통과"],
         ["확장 테스트", "32 / 32 통과 (explain·&&/||·glob·undo·serve·watch)"],
         ["네트워크 테스트", "23 / 23 통과 (인증·동시 접속·타임아웃·원격 파이프)"],
         ["시그널 테스트", "PTY에서 Ctrl-C/Ctrl-Z 후 쉘 생존 확인"],
         ["문서/배포", "README, MIT LICENSE, HWP/PDF 보고서, 테스트 스크립트"],
     ]),

    ("pagebreak",),
    ("h1", "2. 설계 구성 요소"),
    ("h2", "2.1 요구사항 분석"),
    ("table",
     ["ID", "요구사항", "설계/구현 방향", "검증 방법"],
     [
         ["R1", "exit로 프로그램 종료",
          "exit [status] 내장 명령. 부모 프로세스에서 should_exit 플래그를 세우고 "
          "종료 상태를 $?와 프로세스 종료 코드로 전달.",
          "종료 코드 확인, exit 이후 명령이 실행되지 않는지 확인"],
         ["R2", "명령 끝 &로 백그라운드 실행",
          "명령 목록을 실행 시점에 분리하고, &가 붙은 파이프라인은 fork 후 "
          "wait하지 않고 job 테이블에 등록. jobs/wait/$! 제공.",
          "1초 sleep이 800ms 이내에 프롬프트로 복귀하는지 측정"],
         ["R3", "SIGINT/SIGQUIT 처리",
          "쉘은 SIGINT/SIGQUIT을 핸들러로 처리하고, 자식은 기본 동작으로 복원. "
          "전경 작업을 별도 프로세스 그룹으로 만들고 tcsetpgrp()로 터미널 제어권 "
          "이동. 과제 명세에 맞춰 Ctrl-Z를 SIGQUIT에 매핑.",
          "PTY에서 Ctrl-C/Ctrl-Z를 전송한 뒤 쉘 생존과 프롬프트 복귀 확인"],
         ["R4", "재지향 및 파이프",
          "open()/dup2()로 <, >, >>, 2>, 2>>, 2>&1, &>, &>> 구현. 파이프는 "
          "pipe()와 fork()를 명령 수만큼 반복하고, 마지막 명령의 종료 상태를 사용.",
          "파일 내용 비교, 다단 파이프, stderr 분리/병합 테스트"],
         ["R5", "11개 명령 구현",
          "각 명령을 별도 C 모듈로 구현. 시스템 콜(lstat, opendir, link, symlink, "
          "readlink, rename, unlink, rmdir 등)과 POSIX regex 사용.",
          "명령별 옵션/오류/경계 조건을 자동 테스트로 검증"],
         ["R6", "복합 응용 확장",
          "TCP 소켓 기반 netserv/netcli 구현. 셸의 parser/executor를 재사용해 "
          "원격 명령을 실행하고, 토큰 인증·타임아웃·동시 접속·출력 캡처를 지원.",
          "인증 성공/실패, 원격 파이프/재지향, 동시 접속, 타임아웃 테스트"],
         ["R7", "파서/문법 확장",
          "&&/|| 연결자와 glob(*, ?, [])를 parser에 추가하고, explain으로 "
          "파이프·재지향·백그라운드 구조를 실행 없이 출력.",
          "조건부 실행, 글롭 확장, explain 도식 테스트"],
         ["R8", "편의/운영 기능",
          "rm/mv/cp -f 대상을 휴지통으로 이동하고 undo로 복원. serve(HTTP), "
          "watch, prompt on/off 추가.",
          "undo 복원, HTTP GET/ traversal 차단, watch 실행, 프롬프트 테스트"],
     ]),

    ("pagebreak",),
    ("h2", "2.2 시스템 구조"),
    ("code", """+---------------------------- 사용자 터미널 ----------------------------+
|  stdin/stdout/stderr, Ctrl-C(SIGINT), Ctrl-Z(SIGQUIT)              |
+----------------------------------+---------------------------------+
                                   |
                    +--------------v--------------+
                    |  main.c : REPL, signal,     |
                    |  termios, process group     |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | parser.c : tokenizer,       |
                    | quote/expansion, grammar    |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | execute.c : fork/exec,      |
                    | pipe, dup2, wait            |
                    +----+-------------------+----+
                         |                   |
          +--------------v------+   +--------v------------------+
          | builtin.c + cmd_*.c |   | execvp() : 외부 명령      |
          | 11개 명령 + 보조     |   | (/bin/ls, date, ...)      |
          +--------------+------+   +---------------------------+
                         |
          +--------------v--------------+
          | jobs.c : 백그라운드 작업     |
          | 테이블, waitpid(WNOHANG)     |
          +--------------+--------------+
                         |
          +--------------v--------------+
          | cmd_net.c : netserv/netcli  |
          | socket, auth, timeout       |
          +-----------------------------+"""),

    ("h2", "2.3 모듈 구성"),
    ("table",
     ["파일", "역할", "줄 수"],
     [
         ["include/myshell.h", "공용 상수, 구조체, 함수 원형", "254"],
         ["src/main.c", "REPL, 명령 목록 분리, 시그널, 터미널/프로세스 그룹 초기화", "419"],
         ["src/parser.c", "토큰화, 따옴표/변수/틸드 확장, &&/||/glob, 파이프라인/재지향 문법", "797"],
         ["src/execute.c", "fork/exec, 파이프, dup2, 전경/백그라운드 대기", "369"],
         ["src/jobs.c", "백그라운드 job 테이블, 좀비 회수, 종료 시 SIGHUP", "154"],
         ["src/builtin.c", "내장 명령 디스패치, cd/pwd/echo/exit/jobs/wait/prompt", "557"],
         ["src/cmd_ls.c", "ls (-a, -l, -R, -h)", "370"],
         ["src/cmd_fs.c", "mkdir, rmdir, ln, cp, rm, mv (휴지통 연동)", "952"],
         ["src/cmd_text.c", "cat, grep (POSIX regex)", "469"],
         ["src/cmd_net.c", "TCP 원격 쉘 서버/클라이언트, 인증·타임아웃·출력 캡처", "953"],
         ["src/cmd_explain.c", "파서 결과 ASCII 시각화", "111"],
         ["src/cmd_trash.c", "휴지통 이동, undo 매니페스트", "306"],
         ["src/cmd_serve.c", "HTTP GET/HEAD 파일 서버", "544"],
         ["src/cmd_watch.c", "주기 실행", "125"],
         ["src/util.c", "메모리/경로/파일 복사·삭제/모드 출력 유틸", "528"],
     ]),

    ("h2", "2.4 핵심 자료구조"),
    ("table",
     ["구조체", "주요 필드", "역할"],
     [
         ["redirect_t", "in_path, out_path, out_append, err_path, merge_err",
          "한 명령의 입력/출력/에러 재지향 정보"],
         ["command_t", "argv[], argc, redir", "한 명령의 인자와 재지향"],
         ["pipeline_t", "commands[], ncommands, background, text",
          "파이프로 연결된 명령 묶음과 백그라운드 여부"],
         ["command_list_t", "pipelines[], npipelines", "세미콜론/앰퍼샌드로 구분된 명령 목록"],
         ["job_t", "id, pgid, pids[], done[], running, status, command",
          "백그라운드 작업의 프로세스 그룹과 완료 상태"],
         ["shell_state_t", "interactive, last_status, last_bg_pid, jobs[], termios",
          "쉘 전체 상태($?, $!, 작업 테이블, 터미널 설정)"],
         ["net_buf_t", "data, len, truncated",
          "원격 명령의 stdout/stderr 캡처 버퍼"],
         ["connector_t", "ALWAYS, AND, OR",
          "&&/||로 연결된 파이프라인의 실행 조건"],
         ["token_t", "text, glob",
          "단어 토큰과 인용되지 않은 글롭 확장 여부"],
     ]),

    ("h2", "2.5 핵심 설계"),
    ("h3", "2.5.1 파싱과 확장"),
    ("p", "파서는 두 단계로 동작한다. 먼저 main.c의 find_separator()가 따옴표와 "
          "이스케이프를 고려하여 최상위 세미콜론(;)과 앰퍼샌드(&)를 찾아 명령 "
          "목록을 실행 단위로 나눈다. 이 시점에 각 명령을 바로 파싱·실행하므로, "
          "export로 바꾼 환경변수나 직전 명령의 $?가 다음 명령에 정확히 반영된다. "
          "이어서 parser.c의 tokenizer가 단어와 연산자를 분리하고, 작은따옴표/"
          "큰따옴표, 백슬래시, $VAR/${VAR}, $?, $$, $!, 선행 ~를 확장한다. "
          "&&/||는 connector_t로 연결하고, 인용되지 않은 글롭은 glob()으로 "
          "전개한다."),
    ("code", """/* main.c: 실행 시점에 ;와 &를 분리 (개념 발췌) */
static char *find_separator(char *start, int *background)
{
    int in_single = 0, in_double = 0, at_word_start = 1;
    for (char *p = start; *p; ++p) {
        if (*p == '\\\\' && !in_single) { if (p[1]) ++p; continue; }
        if (in_single) { if (*p == '\\'') in_single = 0; continue; }
        if (in_double) { if (*p == '"') in_double = 0; continue; }
        if (*p == '\\'') { in_single = 1; continue; }
        if (*p == '"') { in_double = 1; continue; }
        if (*p == '#' && at_word_start) return NULL;
        if (*p == ';') { *background = 0; return p; }
        if (*p == '&') {
            if (p[1] == '>') continue;          /* &> 재지향 */
            if (p[1] == '&') { ++p; continue; } /* &&는 파서가 오류 처리 */
            if (p > start && p[-1] == '>') continue;
            *background = 1; return p;
        }
        at_word_start = isspace((unsigned char)*p);
    }
    return NULL;
}"""),
    ("p", "문법은 다음과 같다. 빈 명령, 파이프 끝의 |, 재지향 뒤 파일명 누락 등은 "
          "실행 전에 오류로 처리하고, 오류가 발생한 명령 목록만 건너뛴 뒤 쉘은 "
          "계속 동작한다."),
    ("code", """list       := pipeline (';' pipeline)* ['&'] [';']
pipeline   := command ('|' command)*
command    := word+ redirect*
redirect   := ('<' | '>' | '>>' | '2>' | '2>>' | '&>' | '&>>') word"""),

    ("h3", "2.5.2 실행과 내장 명령"),
    ("p", "단일 명령이 cd, exit, jobs, wait, export, unset처럼 쉘 자체의 상태를 "
          "바꾸는 내장 명령이면 부모 프로세스에서 실행한다. 이때 재지향이 있으면 "
          "표준 파일 디스크립터를 dup()로 저장하고 open()/dup2()로 잠시 바꾼 뒤 "
          "복원한다. 그 밖의 명령은 모두 자식 프로세스에서 실행하므로, 파이프 "
          "중간의 cd가 부모의 작업 디렉터리를 바꾸지 않는다."),
    ("p", "파이프라인은 명령 수만큼 pipe()를 만들고 fork()한다. 첫 자식이 "
          "setpgid(0, 0)로 새 프로세스 그룹을 만들고, 이후 자식과 부모가 같은 "
          "pgid에 참여한다. 부모는 tcsetpgrp()로 전경 프로세스 그룹에 터미널을 "
          "넘기고, 각 자식의 종료를 waitpid()로 수집한다. 마지막 명령의 종료 "
          "상태가 파이프라인 전체의 상태가 된다."),
    ("code", """/* execute.c: 파이프라인 생성 (요약) */
for (i = 0; i < n; i++) {
    if (i < n - 1 && pipe(pipefd) < 0) goto fork_failed;
    pid = fork();
    if (pid == 0) {
        setpgid(0, pgid ? pgid : getpid());
        reset_child_signals();
        if (prev_read != -1) { dup2(prev_read, STDIN_FILENO); close(prev_read); }
        if (i < n - 1) { dup2(pipefd[1], STDOUT_FILENO); close(pipefd[1]); }
        apply_redirections(&cmd->redir);
        if (is_builtin(cmd->argv[0]))
            _exit(run_builtin(cmd->argc, cmd->argv, sh, 1));
        execvp(cmd->argv[0], cmd->argv);
        _exit(127);
    }
    ...
}"""),

    ("h3", "2.5.3 시그널과 작업 제어"),
    ("p", "쉘은 SIGINT와 SIGQUIT에 대해 핸들러를 설치하지만, 전경 작업이 실행 중일 "
          "때는 터미널의 전경 프로세스 그룹이 자식이므로 시그널은 자식에게 직접 "
          "전달된다. 자식은 fork 직후 SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, "
          "SIGCHLD, SIGPIPE 등을 모두 SIG_DFL로 복원한다. 따라서 Ctrl-C는 실행 중인 "
          "전경 명령만 중단시키고 쉘은 살아남아 다음 프롬프트를 출력한다."),
    ("p", "과제 명세의 “SIGQUIT: Ctrl-Z”를 만족하기 위해 쉘 시작 시 termios의 "
          "VQUIT를 Ctrl-Z(0x1A)로 설정하고 VSUSP를 비활성화한다. 이는 일반 "
          "리눅스의 Ctrl-Z(SIGTSTP, 작업 일시정지)와 다른 동작이며, 쉘 종료 시 "
          "저장해 둔 termios로 복구한다. 이 매핑은 README와 help 명령에도 명시했다."),
    ("code", """/* main.c: Ctrl-Z를 SIGQUIT으로 매핑 */
if (tcgetattr(STDIN_FILENO, &sh->saved_termios) == 0) {
    struct termios t = sh->saved_termios;
    t.c_cc[VQUIT] = 0x1a;                 /* Ctrl-Z */
#ifdef VSUSP
    t.c_cc[VSUSP] = _POSIX_VDISABLE;      /* SIGTSTP 비활성화 */
#endif
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
        sh->termios_modified = 1;
}"""),

    ("h3", "2.5.4 테스트 설계"),
    ("p", "기능 테스트는 쉘을 비대화식(-c)으로 실행해 stdout, stderr, 종료 상태를 "
          "각각 비교하는 방식으로 작성했다. 파일 시스템 명령은 임시 디렉터리에서 "
          "수행하고, 테스트가 끝나면 작업 디렉터리를 삭제한다. 시그널은 실제 "
          "터미널이 필요한 기능이므로 Python의 pty.fork()로 가상 터미널을 만들고 "
          "Ctrl-C/Ctrl-Z 바이트를 전송한 뒤 프롬프트 복귀와 쉘 생존을 확인한다."),

    ("h3", "2.5.5 네트워크 확장"),
    ("p", "netserv는 TCP 소켓을 열고 접속마다 자식 프로세스를 만들어 명령을 "
          "처리한다. 클라이언트가 보낸 명령은 셸의 parser/execute 경로로 실행되며, "
          "stdout과 stderr는 파이프로 캡처해 상태 코드와 함께 돌려준다. 기본 "
          "바인딩은 127.0.0.1이고, 외부 주소를 열 때는 -s 토큰을 요구한다. 명령당 "
          "타임아웃(-t)과 최대 접속 수(-n)를 두어 무한 대기와 과도한 동시 접속을 "
          "막는다."),
    ("code", """request : "MSH1" | token_len | command_len | token | command
response: status | stdout_len | stderr_len | stdout | stderr"""),

    ("h3", "2.5.6 문법/편의 기능 확장"),
    ("p", "parser에 &&/|| 토큰과 connector_t를 추가했다. execute_command_list()는 "
          "직전 종료 상태를 보고 다음 파이프라인을 실행하거나 건너뛴다. 인용되지 "
          "않은 *, ?, []는 glob()으로 확장하고, 인용된 패턴은 문자 그대로 둔다. "
          "explain은 parse_line() 결과를 실행하지 않고 파이프라인·재지향·"
          "백그라운드 구조로 출력한다."),
    ("p", "rm, mv 덮어쓰기, cp -f 덮어쓰기는 대상을 ~/.myshell_trash로 옮기고 "
          "매니페스트에 기록한다. undo는 마지막 항목을 원래 경로로 되돌린다. "
          "serve는 HTTP GET/HEAD와 디렉터리 목록을 제공하고, realpath()로 문서 "
          "루트 밖 접근을 차단한다. watch는 명령을 주기적으로 재실행하며, "
          "prompt on은 종료 코드·소요 시간·실행 중 작업 수를 프롬프트에 표시한다."),

    ("h2", "2.6 설계 구성 요소와의 대응"),
    ("table",
     ["설계 구성 요소", "본 프로젝트에서의 수행 내용"],
     [
         ["목표/기준 설정", "교재 357쪽 simplesh.c를 기준으로 5개 필수 요구사항과 11개 내장 명령, 원격 쉘 확장을 목표로 설정"],
         ["분석", "요구사항을 R1~R6으로 분해하고, 프로세스/시그널/파일 시스템/텍스트/네트워크로 나누어 분석"],
         ["제작", "parser, executor, job manager, builtin, cmd_* 모듈로 구현. Makefile로 재현 가능한 빌드 제공"],
         ["시험", "94개 기능 + 32개 확장 + 23개 네트워크 + 4개 파일 입력 테스트 + PTY 시그널 테스트"],
     ]),

    ("pagebreak",),
    ("h1", "3. 설계 제한 요소"),
    ("table",
     ["제한 요소", "설계 기준", "본 프로젝트의 대응"],
     [
         ["경제성", "3~4인이 한 학기에 구현 가능한 1,000~10,000줄",
          "헤더 포함 6,913줄. 외부 라이브러리 없이 libc/POSIX regex만 사용하여 유지보수 비용 최소화"],
         ["안정성", "외부 환경 변화에도 지속적으로 동작",
          "오류 반환값 검사, 좀비 회수, 재지향 실패 복구, 94개 자동 테스트, ASan/UBSan 검증"],
         ["생산성", "모든 기능이 동작하고 응용 가능",
          "11개 필수 명령 + 9개 보조 명령, 다단 파이프/재지향, 옵션 지원, README/보고서 제공"],
         ["윤리성/사회적 영향", "안전하고 책임 있는 소프트웨어",
          "rm의 /, ., .. 보호, system()/popen() 미사용, MIT 라이선스로 투명한 재사용 허용"],
         ["표준성", "POSIX/Linux 표준 인터페이스 사용",
          "fork, execvp, waitpid, pipe, dup2, signal/sigaction, termios, dirent, regex 사용"],
         ["이식성", "리눅스/WSL에서 빌드 및 실행",
          "Ubuntu 26.04 WSL2와 gcc 15.2에서 검증. GNU 확장은 _GNU_SOURCE로 명시"],
     ]),

    ("h2", "3.1 경제성"),
    ("p", "전체 소스는 6,913줄로 설계 지침의 1,000~10,000줄 범위를 만족한다. 명령별 "
          "파일을 분리하고 공통 유틸을 util.c에 모아, 2인 팀이 기능 단위로 "
          "병렬 개발할 수 있도록 했다. 외부 라이브러리 의존성이 없어 빌드 환경 "
          "구축 비용도 작다."),

    ("h2", "3.2 안정성"),
    ("p", "모든 시스템 콜과 라이브러리 호출의 반환값을 검사하고, 실패 시 errno를 "
          "포함한 메시지를 출력한다. 백그라운드 작업은 waitpid(WNOHANG)로 주기적으로 "
          "회수하여 좀비 프로세스를 방지하고, 쉘 종료 시 실행 중인 작업에 SIGHUP을 "
          "보낸다. 파이프/재지향 실패 시 파일 디스크립터를 복원하며, ASan/UBSan "
          "빌드에서도 94개 기능 테스트와 32개 확장 테스트를 통과했다."),

    ("h2", "3.3 생산성"),
    ("p", "ls는 -a/-l/-R/-h, cp는 -r/-f/-i/-p, grep은 -i/-v/-n/-c/-l/-r/-E/-F/-q "
          "등 실사용에 필요한 옵션을 제공한다. 내장 명령은 외부 프로그램을 "
          "호출하지 않으므로 PATH나 외부 패키지 설치 여부와 무관하게 동작한다. "
          "netserv/netcli로 원격 명령 실행도 가능하며, 테스트는 make test로 "
          "재현한다."),

    ("h2", "3.4 윤리성과 사회적 영향"),
    ("p", "쉘은 파일 삭제·복사·이동 권한을 가지므로 안전장치가 필요하다. rm은 "
          "/, ., ..를 거부하고, 재귀 삭제는 심볼릭 링크를 따라가지 않는다. "
          "외부 명령은 execvp()로 직접 실행하며 system()/popen()을 사용하지 않아 "
          "문자열 주입으로 인한 추가 쉘 해석을 피했다. 네트워크 서버는 기본적으로 "
          "127.0.0.1에만 바인딩하고, 외부 주소는 토큰 인증을 요구한다. MIT "
          "라이선스는 자유로운 수정·배포를 허용한다."),

    ("h2", "3.5 표준성과 이식성"),
    ("p", "POSIX/Linux 시스템 콜과 C 표준 라이브러리를 사용했으며, 특정 배포판에 "
          "종속된 기능은 사용하지 않았다. WSL2의 Ubuntu 26.04에서 개발·검증했고, "
          "일반 리눅스에서도 동일하게 빌드된다. 다만 터미널 제어(tcsetpgrp)와 "
          "termios는 POSIX 터미널이 있는 환경을 전제로 한다."),

    ("pagebreak",),
    ("h1", "4. 설계 내용"),
    ("h2", "4.1 요구사항 분해와 구현 전략"),
    ("p", "교재의 simplesh.c는 입력, 인자 분리, fork, exec, wait의 다섯 단계로 "
          "구성된다. 본 프로젝트는 이 흐름을 유지하되, 입력 단계에 실행 시점 분리와 "
          "확장을, 실행 단계에 재지향/파이프/프로세스 그룹을, 대기 단계에 작업 "
          "테이블과 시그널 처리를 추가했다. 내장 명령은 실행 단계에서 분기하여 "
          "부모 프로세스 또는 자식 프로세스에서 실행한다."),
    ("table",
     ["단계", "교재 simplesh.c", "myshell 확장"],
     [
         ["입력", "fgets(buf, 256, stdin)", "getline()으로 길이 제한 없이 읽고, ;/&를 실행 시점에 분리"],
         ["인자 분리", "getargs()가 공백만 처리", "따옴표, 이스케이프, $VAR, $?, $$, $!, ~ 확장"],
         ["실행", "무조건 fork + execvp", "내장 명령 분기, 파이프/재지향, 다중 명령"],
         ["대기", "wait((int *)0)", "waitpid()로 개별 수집, 백그라운드 job 회수, 종료 상태 저장"],
         ["시그널", "없음", "SIGINT/SIGQUIT 핸들러, 자식 신호 복원, 전경 프로세스 그룹"],
     ]),

    ("h2", "4.2 파서와 확장"),
    ("p", "토크나이저는 연산자(&>>, &>, 2>>, 2>, >>, >, <, |, ;, &)를 단어보다 먼저 "
          "인식한다. 단어는 작은따옴표(문자 그대로), 큰따옴표($ 확장 허용), "
          "백슬래시 이스케이프를 지원하며, 빈 따옴표(\"\")도 하나의 빈 인자로 "
          "보존한다. 변수 확장은 getenv()를 사용하고, 정의되지 않은 변수는 빈 "
          "문자열로 확장한다. $?는 직전 명령의 종료 상태, $$는 쉘 PID, $!는 마지막 "
          "백그라운드 프로세스 그룹 ID를 나타낸다."),
    ("p", "명령 목록을 실행 시점에 분리하는 설계는 단순히 $?뿐 아니라 "
          "“export X=hello; echo $X”와 같은 한 줄 명령에서도 올바른 값을 보장한다. "
          "만약 전체 줄을 한 번에 확장하면 두 번째 명령의 $X가 이전 값을 보게 되므로, "
          "이 분리는 쉘 의미론상 중요한 선택이었다."),

    ("h2", "4.3 실행기와 프로세스 제어"),
    ("p", "전경 파이프라인은 모든 자식을 하나의 프로세스 그룹으로 묶고, 부모가 "
          "tcsetpgrp()로 터미널 전경 그룹을 자식 그룹으로 바꾼다. 자식이 모두 "
          "끝나면 다시 쉘 그룹으로 복원한다. 이 구조 덕분에 Ctrl-C/Ctrl-Z가 쉘 "
          "전체가 아니라 전경 작업에만 전달된다. 백그라운드 파이프라인은 같은 "
          "방식으로 그룹을 만들되 터미널을 넘겨받지 않고, job 테이블에 등록한다."),
    ("p", "재지향은 파이프 연결 후에 적용한다. 따라서 “cmd1 | cmd2 > file”에서 "
          "cmd2의 표준 출력은 파이프가 아니라 file로 향하며, cmd1의 출력은 "
          "cmd2의 표준 입력으로 들어간다. 2>와 &>는 각각 stderr만, stdout+stderr "
          "동시를 처리한다. 파일은 O_CREAT|O_TRUNC(또는 O_APPEND)로 열고, dup2()로 "
          "표준 fd에 복제한 뒤 원래 fd를 닫는다."),

    ("h2", "4.4 시그널과 터미널"),
    ("p", "쉘은 SIGINT/SIGQUIT 핸들러에서 화면에 개행만 출력하고 종료하지 않는다. "
          "전경 작업 중에는 터미널이 자식 그룹을 가리키므로 시그널이 쉘에 오지 "
          "않고, 자식은 기본 동작으로 종료된다. 부모는 waitpid()로 자식의 종료를 "
          "확인하고 128+signal을 $?에 저장한다. SIGQUIT으로 종료된 경우에는 "
          "“Quit (core dumped)” 메시지를 출력한다."),

    ("h2", "4.5 내장 명령 구현"),
    ("p", "11개 명령은 모두 C로 직접 구현했다. 파일 시스템 명령은 lstat/stat로 "
          "심볼릭 링크와 대상 파일을 구분하고, 재귀 복사·삭제는 dirent로 디렉터리를 "
          "순회한다. cat은 getline()으로 줄 단위 처리하며 -n/-b/-E/-T를 지원한다. "
          "grep은 POSIX regcomp/regexec를 사용하고, -F는 strstr/strcasestr로 "
          "고정 문자열을 찾는다. 재귀 grep은 디렉터리를 순회하되 심볼릭 링크를 "
          "따라가지 않아 무한 루프를 방지한다."),
    ("table",
     ["명령", "핵심 구현", "대표 옵션"],
     [
         ["ls", "opendir/readdir, qsort, lstat, getpwuid/getgrgid, strftime", "-a, -l, -R, -h"],
         ["pwd", "getcwd(NULL, 0)", "-L, -P"],
         ["cd", "chdir, getcwd, PWD/OLDPWD 갱신", "cd, cd DIR, cd -"],
         ["mkdir", "mkdir, 재귀 생성 헬퍼", "-p, -m, -v"],
         ["rmdir", "rmdir, 부모 디렉터리 순차 제거", "-p, -v"],
         ["ln", "link, symlink, readlink, unlink", "-s, -f, -v"],
         ["cp", "open/read/write, 재귀 복사, chmod/utimensat", "-r, -f, -i, -p, -v"],
         ["rm", "lstat, unlink, 재귀 삭제, 안전 경로 검사", "-r, -f, -i, -v"],
         ["mv", "rename, EXDEV 시 복사 후 삭제", "-f, -i, -n, -v"],
         ["cat", "getline, 줄 번호/탭/줄 끝 표시", "-n, -b, -E, -T"],
         ["grep", "regcomp/regexec, 재귀 탐색, 파일명/줄 번호 prefix", "-i, -v, -n, -c, -l, -r, -E, -F, -q"],
     ]),

    ("h2", "4.6 테스트 설계와 자동화"),
    ("p", "tests/run_tests.sh는 94개의 기능 검사를 수행한다. 각 검사는 stdout, "
          "stderr, 종료 상태를 기대값과 비교한다. tests/test_net.sh는 인증, 원격 "
          "파이프/재지향, 동시 접속, 타임아웃을 23개 항목으로 확인하고, "
          "tests/test_file_input.sh는 파일 입력에서 마지막 줄이 중복되지 않는지 "
          "4개 항목으로 검사한다. tests/test_signals.py는 pty.fork()로 가상 "
          "터미널을 열고 Ctrl-C/Ctrl-Z 후 쉘 생존을 확인한다. 전체는 make test로 "
          "실행한다."),

    ("h2", "4.7 협업과 형상 관리"),
    ("p", "기능별로 소스를 분리하고, 커밋은 “기능 구현 → 테스트 → 문서” 단위로 "
          "나누었다. 파일 시스템 명령은 cmd_fs.c, 텍스트 명령은 cmd_text.c, "
          "ls는 cmd_ls.c, 네트워크 기능은 cmd_net.c로 분리했다. 통합 전에는 "
          "make test를 실행해 회귀를 확인했다. 저장소에는 README, LICENSE, "
          "Makefile, 테스트, 보고서를 함께 두었다."),

    ("pagebreak",),
    ("h2", "4.8 원격 쉘 확장"),
    ("p", "netserv는 getaddrinfo/socket/bind/listen/accept로 서버를 열고, 접속마다 "
          "fork()로 자식 프로세스를 만든다. 자식은 MSH1 요청을 읽어 토큰을 "
          "확인하고, 명령을 capture_command()로 실행한다. capture_command()는 "
          "stdout/stderr 파이프를 poll()로 동시에 읽어 교착을 피하고, 제한 시간이 "
          "지나면 프로세스 그룹에 SIGKILL을 보낸다. 출력은 1MiB로 제한한다."),
    ("table",
     ["구성", "구현"],
     [
         ["서버", "netserv [-p PORT] [-a ADDR] [-s TOKEN] [-t SEC] [-n COUNT] [-1]"],
         ["클라이언트", "netcli [-p PORT] [-s TOKEN] [-t SEC] [-i] HOST [COMMAND...]"],
         ["프로토콜", "MSH1 + 길이 기반 요청/응답, 네트워크 바이트 순서"],
         ["인증", "선택적 공유 토큰, 비루프백 주소는 토큰 필수"],
         ["자원 보호", "명령 타임아웃, 최대 접속 수, 출력 크기 제한"],
     ]),
    ("p", "원격 명령은 별도 셸 상태에서 실행되며 stdin은 /dev/null로 연결한다. "
          "따라서 cat처럼 입력을 기다리는 명령이 서버를 멈추지 않는다. "
          "백그라운드 연산자(&)는 원격 세션에서 거부하고, 파이프와 재지향은 "
          "그대로 사용할 수 있다."),

    ("h2", "4.9 확장 기능 구현"),
    ("table",
     ["기능", "구현 위치", "핵심 처리"],
     [
         ["explain", "cmd_explain.c", "parse_line() 결과를 pipeline/cmd/redirect 단위로 출력"],
         ["&& / ||", "parser.c, execute.c", "connector_t와 직전 종료 상태로 실행/건너뜀"],
         ["glob", "parser.c", "인용되지 않은 *, ?, []를 glob(3)으로 확장"],
         ["undo/휴지통", "cmd_trash.c, cmd_fs.c", "rename/EXDEV 복사 후 매니페스트 기록, undo로 복원"],
         ["serve", "cmd_serve.c", "GET/HEAD, 디렉터리 목록, realpath 기반 경로 탈출 차단"],
         ["watch", "cmd_watch.c", "shell_execute_line()을 주기적으로 호출"],
         ["prompt", "main.c, builtin.c", "종료 코드·소요 시간·실행 중 작업 수 표시"],
     ]),

    ("pagebreak",),
    ("h1", "5. 프로젝트 결과"),
    ("h2", "5.1 프로젝트 구현 내용"),
    ("table",
     ["기능", "구현 상태", "검증 결과"],
     [
         ["exit", "완료", "종료 상태 3, exit 이후 명령 미실행 확인"],
         ["백그라운드 &", "완료", "sleep 1이 800ms 이내에 복귀, jobs/wait/$! 동작"],
         ["SIGINT (Ctrl-C)", "완료", "sleep/cat 전경 작업 중단 후 쉘 생존"],
         ["SIGQUIT (Ctrl-Z)", "완료", "VQUIT 매핑, sleep/cat 중단 후 쉘 생존"],
         ["<, >, >>", "완료", "파일 내용 비교, append 확인"],
         ["2>, 2>>, 2>&1, &>, &>>", "완료", "stderr 분리/병합/append 확인"],
         ["파이프 |", "완료", "2단/3단 파이프, 내장 명령 파이프 확인"],
         ["ls", "완료", "-a, -l, -R, -h 및 오류 처리"],
         ["pwd, cd", "완료", "cd -, PWD/OLDPWD, 파이프 내 cd 격리"],
         ["mkdir, rmdir", "완료", "-p, -m, 비어 있지 않은 디렉터리 오류"],
         ["ln", "완료", "하드 링크, 심볼릭 링크, -f 덮어쓰기"],
         ["cp", "완료", "-r, -p, -f, -i, 자기 자신 복사 방지"],
         ["rm", "완료", "-r, -f, -i, /·.·.. 보호"],
         ["mv", "완료", "이름 변경, 디렉터리 이동, EXDEV 복사 후 삭제"],
         ["cat", "완료", "-n, -b, -E, -T, 다중 파일"],
         ["grep", "완료", "-i, -v, -n, -c, -l, -r, -E, -F, -q"],
         ["netserv/netcli", "완료", "토큰 인증, 원격 파이프/재지향, 동시 접속, 타임아웃"],
         ["&& / ||", "완료", "연결자별 실행/건너뜀, 종료 상태 유지"],
         ["glob", "완료", "인용되지 않은 *, ?, [] 확장"],
         ["explain", "완료", "파이프·재지향·백그라운드 도식 출력"],
         ["undo / 휴지통", "완료", "rm/mv/cp -f 복원, EXDEV 처리"],
         ["serve", "완료", "GET/HEAD, 디렉터리 목록, ../ 차단"],
         ["watch / prompt", "완료", "주기 실행, 확장 프롬프트"],
     ]),
    ("p", "다음은 실제 실행 예시이다."),
    ("code", """shell> pwd
/home/student/lab3
shell> echo hello > out.txt
shell> cat < out.txt
hello
shell> cat out.txt | grep -n hello
1:hello
shell> netserv -1 -p 9090 &
[1] 12345
shell> netcli -p 9090 127.0.0.1 'cat out.txt | grep hello'
hello
shell> sleep 2 &
[1] 12345
shell> jobs
[1]  Running  sleep 2 &
shell> wait
shell> echo done
done
shell> ls -l out.txt
-rw-r--r-- 1 student student 6 2026-11-18 18:00 out.txt
shell> exit"""),

    ("h2", "5.2 프로젝트 완성도 평가"),
    ("p", "기능 테스트 94개, 확장 테스트 32개, 네트워크 테스트 23개, 파일 "
          "입력 테스트 4개가 모두 통과했고, PTY 시그널 테스트도 통과했다"
          "(Passed: 94, Failed: 0; SIGNAL TEST PASSED). ASan/UBSan을 켠 빌드에서도 기능 테스트를 "
          "통과했다. 다음 기능은 설계 범위에서 제외했다."),
    ("table",
     ["제외 기능", "이유", "영향"],
     [
         ["1>&2", "2>&1만 지원. 반대 방향 fd 복제는 미지원", "일부 스크립트 호환성 제한"],
         ["undo 범위", "마지막 한 건만 복원", "여러 작업 연속 복원 불가"],
         ["serve 인증", "HTTP 서버는 별도 인증 없음. 기본 127.0.0.1 바인딩", "외부 공개 시 주의"],
         ["원격 cd", "원격 명령을 명령마다 자식 프로세스에서 실행", "cd가 다음 원격 명령에 유지되지 않음"],
         ["fg, bg", "SIGTSTP 일시정지 모델을 사용하지 않음", "중단된 작업 재개 불가"],
         ["명령 치환 $(...)", "필수 범위 아님", "명령 결과를 인자로 사용 불가"],
     ]),

    ("h2", "5.3 일정 계획 평가"),
    ("table",
     ["주차", "계획", "실제 수행", "평가"],
     [
         ["11주", "주제 선정, 요구사항 분석, 설계", "교재 357쪽 분석, R1~R8 분해, 모듈/자료구조 설계", "계획대로"],
         ["12주", "파서/실행기 구현", "따옴표·변수 확장, 파이프·재지향, 내장 명령 디스패치", "계획대로"],
         ["13주", "시그널/작업 제어", "프로세스 그룹, tcsetpgrp, VQUIT 매핑, jobs/wait", "계획대로"],
         ["14주", "11개 명령 구현 및 통합", "파일/텍스트 명령, 94개 기능 테스트, ASan 검증", "테스트 보강으로 일부 지연"],
         ["15주", "확장 기능, 보고서/GitHub 정리", "netserv/netcli, explain, undo, serve, watch, 59개 추가 테스트", "15주 내 완료"],
     ]),
    ("p", "14주차에 재귀 복사/삭제와 grep 옵션의 경계 조건을 보강했고, 15주차에 "
          "원격 쉘 확장과 네트워크 테스트를 추가했다."),

    ("h2", "5.4 역할 수행 평가"),
    ("table",
     ["팀원", "학번", "GitHub", "담당 업무", "기여도"],
     [
         ["류동엽", "20223154", "gong000000",
          "쉘 코어(파서/실행기/시그널/작업 제어/재지향/파이프), explain, netserv 서버, serve, pwd, cd, ls, mkdir, rmdir",
          "50%"],
         ["강동우", "20223080", "pizza0420",
          "netcli, undo/휴지통, watch, prompt, ln, cp, rm, mv, cat, grep, 확장/기능 테스트, 문서",
          "50%"],
     ]),
    ("p", "류동엽은 쉘 코어와 netserv, 강동우는 파일/텍스트 명령과 netcli·테스트를 "
          "담당했다. 명령 구현은 5개(pwd, cd, ls, mkdir, rmdir)와 6개(ln, cp, rm, "
          "mv, cat, grep)로 나눴다. 통합 전 두 사람이 상대 모듈의 테스트를 실행했다."),

    ("h2", "5.5 위험 처리"),
    ("table",
     ["위험", "영향", "대응", "결과"],
     [
         ["터미널 전경 그룹 경쟁", "Ctrl-C가 쉘까지 종료", "자식 setpgid + 부모 tcsetpgrp, 자식 신호 기본값 복원", "PTY 테스트 통과"],
         ["좀비 프로세스", "백그라운드 작업 누적", "waitpid(WNOHANG) 주기적 회수, wait 내장 명령", "jobs에서 Done 확인"],
         ["파일 디스크립터 누수", "장시간 사용 시 fd 고갈", "dup2 후 원본 close, 재지향 실패 시 복원", "ASan/UBSan 통과"],
         ["재귀 복사 자기 참조", "무한 재귀/디스크 고갈", "realpath 기반 포함 관계 검사", "cp/mv 자기 복사 오류 확인"],
         ["rm 오조작", "중요 파일 삭제", "/, ., .. 거부, 심볼릭 링크 비추적", "안전 경로 테스트 통과"],
         ["따옴표/이스케이프 파싱", "명령 오해석", "토크나이저 상태 머신, 94개 테스트", "구문 오류/인용 테스트 통과"],
         ["원격 명령 무한 대기", "서버 자원 고갈", "poll 기반 타임아웃, 프로세스 그룹 SIGKILL", "sleep 5 → 1초 타임아웃 확인"],
         ["외부 노출", "무단 원격 명령 실행", "기본 127.0.0.1, 비루프백 주소는 토큰 필수", "인증 성공/실패 테스트"],
         ["출력 폭주", "메모리 고갈", "stdout/stderr 1MiB 제한, 초과분 폐기", "출력 제한 코드 적용"],
         ["HTTP 경로 탈출", "문서 루트 밖 파일 노출", "realpath + 문서 루트 prefix 검사, ../ 차단", "curl ../etc/passwd → 403"],
         ["휴지통 증가", "홈 디렉터리 사용량 증가", "필요 시 ~/.myshell_trash 수동 정리, rm --no-trash 제공", "테스트에서 동작 확인"],
     ]),

    ("h2", "5.6 GitHub 저장소와 라이선스"),
    ("p", "프로젝트는 https://github.com/gong000000/lab3 에 공개되어 있으며, "
          "팀원 강동우(pizza0420)를 Collaborator(write 권한)로 등록했다. "
          "팀원이 직접 commit/push할 수 있다. 저장소 루트에는 "
          "MIT LICENSE가 있어 누구나 자유롭게 사용·수정·재배포할 수 있다. "
          "README에는 빌드, 실행, 테스트, 지원 옵션, 알려진 제한을 정리했다."),

    ("pagebreak",),
    ("h1", "6. 소감"),
    ("h2", "류동엽"),
    ("p", "프로세스 그룹과 터미널 제어를 직접 다루면서 fork/exec/wait만으로는 "
          "쉘이 완성되지 않는다는 것을 알았다. Ctrl-C/Ctrl-Z를 전경 작업에만 "
          "전달하기 위해 setpgid()와 tcsetpgrp()를 적용했고, PTY 테스트로 "
          "확인했다. 네트워크 확장에서는 인증, 타임아웃, 출력 캡처를 구현했다."),
    ("h2", "강동우"),
    ("p", "cp/rm의 재귀 처리와 grep 옵션을 구현하면서 심볼릭 링크, EXDEV, 종료 "
          "상태 같은 경계 조건을 정리했다. 94개 기능 테스트, 32개 확장 "
          "테스트, 23개 네트워크 테스트를 작성해 변경 사항을 확인했다."),
    ("h2", "함께"),
    ("p", "쉘 코어와 네트워크 확장을 분리해 작업하고, make test로 통합 동작을 "
          "확인했다."),

    ("pagebreak",),
    ("h1", "부록 A. 빌드 및 실행"),
    ("code", """# 빌드
make

# 대화형 실행
./myshell

# 한 줄 실행
./myshell -c 'echo hello > out.txt; cat out.txt | grep hello'

# 원격 쉘: 서버를 백그라운드로 실행하고 클라이언트로 명령 실행
./myshell -c 'netserv -1 -p 9090 & sleep 0.3; netcli -p 9090 127.0.0.1 echo hello; wait'

# 전체 테스트
make test

# 시그널 테스트 (Linux/WSL)
python3 tests/test_signals.py"""),
    ("h1", "부록 B. 테스트 항목 요약"),
    ("table",
     ["영역", "주요 검사", "개수"],
     [
         ["종료/시퀀스", "exit 상태, true/false, 세미콜론", "5"],
         ["인용/확장", "따옴표, $HOME, ${VAR}, $?, 주석, ~", "9"],
         ["재지향", ">, >>, <, 2>, 2>>, 2>&1, &>, 내장 명령 재지향", "15"],
         ["파이프", "2단/3단, 내장 명령 파이프, 파이프라인 상태", "6"],
         ["백그라운드", "조기 복귀, jobs, wait, $!, Done, 백그라운드 exit", "8"],
         ["pwd/cd", "pwd, cd, cd -, 실패, 다중 cd, 파이프 내 cd", "6"],
         ["mkdir/rmdir", "-p, 기존 디렉터리, -m, 비어 있지 않음, -p 삭제", "5"],
         ["ln", "하드/심볼릭, 기존 파일 오류, -sf", "4"],
         ["cp", "파일, -r, 자기 복사, 없는 원본", "4"],
         ["mv", "이름 변경, 디렉터리, 자기 참조", "4"],
         ["rm", "파일, -r, -f, 디렉터리 오류", "4"],
         ["cat", "파일, -n, -b, 없는 파일", "5"],
         ["grep", "기본, -i, -v, -n, -c, -F, -E, -q, -r", "11"],
         ["ls", "파일, -a, -l, -R", "5"],
         ["문법 오류", "닫히지 않은 따옴표, 끝 파이프, 복구", "3"],
     ]),
    ("p", "합계 94개. tests/run_tests.sh의 Passed/Failed 카운터와 일치한다."),
    ("table",
     ["추가 테스트", "내용", "개수"],
     [
         ["네트워크", "인증 성공/실패, 원격 파이프/재지향, 동시 접속, 타임아웃", "23"],
         ["파일 입력", "여러 줄 스크립트, 마지막 줄 중복, 백그라운드 혼합", "4"],
         ["확장", "explain, &&/||, glob, undo/휴지통, serve, watch", "32"],
         ["시그널", "PTY에서 Ctrl-C/Ctrl-Z 및 확장 프롬프트", "6"],
     ]),
    ("p", "make test는 위 테스트를 모두 실행한다."),

    ("h1", "부록 C. 소스 규모"),
    ("table",
     ["파일", "줄 수", "비고"],
     [
         ["include/myshell.h", "254", "공용 선언"],
         ["src/main.c", "419", "REPL/시그널/터미널"],
         ["src/parser.c", "797", "토큰화/확장/문법"],
         ["src/execute.c", "369", "fork/pipe/redirection/wait"],
         ["src/jobs.c", "154", "백그라운드 작업"],
         ["src/builtin.c", "557", "내장 명령"],
         ["src/cmd_ls.c", "370", "ls"],
         ["src/cmd_fs.c", "952", "mkdir/rmdir/ln/cp/rm/mv"],
         ["src/cmd_text.c", "469", "cat/grep"],
         ["src/cmd_net.c", "953", "netserv/netcli"],
         ["src/cmd_explain.c", "111", "explain"],
         ["src/cmd_trash.c", "311", "undo/trash"],
         ["src/cmd_serve.c", "544", "serve"],
         ["src/cmd_watch.c", "125", "watch"],
         ["src/util.c", "528", "공통 유틸"],
         ["합계", "6,913", "헤더 포함"],
     ]),

    ("h1", "부록 D. 참고문헌"),
    ("bullets", [
        "교재 357쪽, simplesh.c (fgets/getargs/fork/execvp/wait 예제)",
        "Linux man pages: fork(2), execvp(3), waitpid(2), pipe(2), dup2(2), "
        "signal(7), sigaction(2), termios(3), tcsetpgrp(3)",
        "Linux man pages: opendir(3), readdir(3), lstat(2), link(2), symlink(2), "
        "readlink(2), rename(2), unlink(2), rmdir(2)",
        "POSIX regex: regcomp(3), regexec(3), regerror(3)",
        "Linux man pages: socket(2), bind(2), listen(2), accept(2), connect(2), "
        "getaddrinfo(3), poll(2)",
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
