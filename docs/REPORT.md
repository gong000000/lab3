# 프로젝트 결과보고서

## 간단한 쉘(Simple Shell) 구현 및 확장

**오픈소스프로젝트(전공설계) · 2026학년도 2학기**

| 항목 | 내용 |
|---|---|
| 과목 | 오픈소스프로젝트(전공설계) |
| 프로젝트 | Lab 3 - 간단한 쉘 구현 및 확장 |
| 팀명 | myshell |
| 팀원 | 류동엽 (20223154, @gong000000), 강동우 (20223080, @pizza0420) |
| GitHub | https://github.com/gong000000/lab3 |
| 제출일 | 2026. 11. 18. |


---


# 1. 프로젝트 개요


## 1.1 추진 배경

교재 357쪽의 simplesh.c는 fgets()로 한 줄을 읽고 getargs()로 인자를 분리한 뒤 fork()/execvp()로 명령을 실행하는 가장 기본적인 쉘이다. 이 프로그램은 쉘의 핵심 원리를 보여 주지만 exit, 백그라운드, 인터럽트, 재지향, 파이프, 내장 명령이 빠져 있다.

본 프로젝트는 simplesh.c의 구조를 유지하면서 Lab 3의 5개 요구사항과 11개 내장 명령을 구현하고, 그 위에 “시스템 프로그래밍 학습용 투명 쉘” 컨셉의 관찰·기록 도구를 추가했다. 최종 소스는 10,000줄 이하를 지키기 위해 범위를 조정했고, 원 과제 요구사항은 모두 보존했다.


## 1.2 프로젝트 목표

- exit, &, SIGINT/SIGQUIT, 재지향/파이프를 구현해 실제 쉘처럼 동작시킨다.
- ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep을 직접 구현한다.
- readline 없이 UTF-8 라인 에디터와 히스토리를 구현한다.
- if/for/while, 함수, $(...)를 지원하는 스크립트 해석기를 구현한다.
- explain, trace, rec/replay로 실행 과정을 관찰·기록·재현한다.
- 자동화 테스트와 PTY 테스트로 회귀를 방지하고 10,000줄 이하를 유지한다.


## 1.3 구현 범위

| 구분 | 내용 |
|---|---|
| 필수 기능 | exit, 백그라운드(&), SIGINT/SIGQUIT, 재지향(>, <), 파이프(\|) |
| 필수 내장 명령 | ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개) |
| 보조 내장 명령 | echo, export, unset, jobs, wait, help, true, false, :, test, [, read, shift, source, ., trace, history, rec, replay |
| 라인 편집 | raw 모드, UTF-8 폭 보정, 방향키/Home/End, Tab, Ctrl-R, 히스토리 |
| 스크립트 | $(...), if/elif/else, for, while/until, break/continue, 함수, return |
| 관찰/기록 | explain(파서), trace(시스템 호출), rec/replay(세션 녹화·HTML) |
| 범위 제외 | 네트워크/HMAC, dbg, pipestat, serve, watch, undo/휴지통 (10,000줄 준수) |
| 문서/배포 | README, MIT LICENSE, 결과보고서(PDF/HWP/DOCX), 자동 테스트 |


---


## 1.4 개발 환경

| 항목 | 내용 |
|---|---|
| 운영체제 | Ubuntu 26.04.1 LTS (WSL2, x86_64) |
| 컴파일러 | gcc 15.2.0 (-std=gnu11) |
| 빌드 도구 | GNU Make, Bash |
| 테스트 | Bash, Python 3.14 (pty/select) |
| 버전 관리 | Git, GitHub (gong000000/lab3) |
| 외부 라이브러리 | glibc, POSIX regex 외 없음 |


## 1.5 결과 요약

최종 프로그램은 직접 작성 소스 9,672줄(생성 테이블 포함 9,809줄)로 10,000줄 이하를 만족한다. 11개 필수 내장 명령, UTF-8 라인 에디터, 스크립트 해석기, trace, rec/replay를 제공하며, 기능 94개·확장 16개·파일 입력 5개·스크립트 56개·추적 32개·녹화 35개 테스트와 PTY 시그널/라인 에디터 테스트를 통과했다.

| 항목 | 결과 |
|---|---|
| 소스 규모 | 직접 작성 9,672줄, 생성 테이블 포함 9,809줄 (10,000줄 이하) |
| 필수 내장 명령 | ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개) |
| 기능 테스트 | 94 / 94 통과 |
| 확장 테스트 | 16 / 16 통과 (explain, &&/\|\|, glob) |
| 파일 입력 | 5 / 5 통과 |
| 스크립트 | 56 / 56 통과 |
| 추적 | 32 / 32 통과 |
| 녹화 | 35 / 35 통과 |
| 터미널 | PTY 시그널, 한글 편집, Tab, Ctrl-R, 히스토리 통과 |


---


# 2. 설계 구성 요소


## 2.1 시스템 구조

```text
+---------------------------- 사용자 터미널 ----------------------------+
| stdin/stdout/stderr, Ctrl-C(SIGINT), Ctrl-Z(SIGQUIT)              |
+----------------------------------+---------------------------------+
                                   |
                    +--------------v--------------+
                    | main.c : REPL, signal,      |
                    | termios, line editor 연결    |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | parser.c : tokenizer,       |
                    | quote/expansion, grammar    |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | script.c : if/for/while,    |
                    | functions, script files     |
                    +--------------+--------------+
                                   |
                    +--------------v--------------+
                    | execute.c : fork/exec,      |
                    | pipe, dup2, wait            |
                    +----+-------------------+----+
                         |                   |
          +--------------v------+   +--------v------------------+
          | builtin.c + cmd_*.c |   | execvp() : 외부 명령      |
          | 11개 명령 + 확장     |   | (/bin/ls, date, ...)      |
          +--------------+------+   +---------------------------+
                         |
          +--------------v--------------+
          | jobs.c : 백그라운드 작업     |
          +--------------+--------------+
                         |
          +--------------v--------------+
          | cmd_trace.c / cmd_rec.c     |
          | ptrace / 세션 녹화           |
          +-----------------------------+
```


## 2.2 모듈 구성

| 파일 | 역할 | 줄 수 |
|---|---|---|
| include/myshell.h | 공용 상수, 구조체, 함수 원형 | 294 |
| src/main.c | REPL, 명령 분리, 시그널, 터미널 | 525 |
| src/parser.c | 토큰화, 확장, 문법 분석 | 958 |
| src/execute.c | fork/exec, 파이프, 재지향, 캡처 | 487 |
| src/script.c | 제어문, 함수, 스크립트 실행 | 1643 |
| src/lineedit.c | raw 모드 에디터, 히스토리, Tab, Ctrl-R | 929 |
| src/jobs.c | 백그라운드 작업 | 154 |
| src/builtin.c | 내장 명령 디스패치 | 803 |
| src/cmd_ls.c | ls | 370 |
| src/cmd_fs.c | mkdir/rmdir/ln/cp/rm/mv | 952 |
| src/cmd_text.c | cat/grep | 469 |
| src/cmd_trace.c | ptrace 시스템 호출 추적 | 573 |
| src/cmd_rec.c | 세션 녹화/재생/HTML | 837 |
| src/cmd_explain.c | 파서 시각화 | 111 |
| src/util.c | 공용 유틸 | 567 |
| src/trace_syscalls.h | 시스템 호출 이름 테이블 (생성) | 137 |
| 합계 | 직접 작성 9,672 + 생성 137 = 9,809 |  |


## 2.3 핵심 자료구조

| 구조체 | 주요 필드 | 역할 |
|---|---|---|
| command_t | argv[], argc, redir | 한 명령의 인자와 재지향 |
| pipeline_t | commands[], background, text | 파이프 묶음 |
| job_t | pgid, pids[], status, command | 백그라운드 작업 |
| shell_state_t | last_status, jobs[], history[], functions[], positional[] | 쉘 전체 상태 |
| snode_t | type, cmd, branches[], body, cond | 스크립트 문장 트리 |
| tracee_t | pid, entered, nr, args[6] | ptrace 추적 상태 |
| rec_item_t | cmd, out, err, status, ms | 녹화된 명령 하나 |


## 2.4 핵심 설계


### 2.4.1 파싱과 실행

main.c의 find_separator()가 따옴표를 고려해 ;와 &를 분리하고, parser.c가 단어/연산자를 토큰화한다. 따옴표, $VAR, $?, $$, $!, $(...)를 확장하고, 파이프/재지향/&&/||/glob을 처리한다. 내장 명령 중 cd, exit, jobs, wait, export처럼 쉘 상태를 바꾸는 것은 부모에서, 나머지는 자식에서 실행한다.


### 2.4.2 시그널과 작업 제어

전경 파이프라인은 하나의 프로세스 그룹으로 묶고 tcsetpgrp()로 터미널을 넘긴다. 자식은 SIGINT/SIGQUIT 등을 기본값으로 복원하므로 Ctrl-C/Ctrl-Z는 전경 작업만 중단한다. 과제 명세에 맞춰 VQUIT를 Ctrl-Z로 매핑하고 VSUSP를 비활성화한다.


### 2.4.3 UTF-8 라인 에디터

termios raw 모드에서 입력을 읽고, 화면을 다시 그릴 때 mbrtowc()와 wcwidth()로 한글의 표시 폭을 계산한다. 방향키/Home/End/Delete, Ctrl-U/K/W, 히스토리, Tab 완성, Ctrl-R 역검색을 지원한다. 히스토리는 ~/.myshell_history에 저장된다.


### 2.4.4 스크립트 해석기

script.c는 소스를 문장으로 나눈 뒤 snode_t 트리를 만든다. 명령 노드는 기존 parse_line()/execute_command_list()에 위임해 파이프/재지향/glob을 그대로 재사용한다. if/for/while, break/continue/return, 함수, 위치 매개변수, source, 스크립트 파일을 지원한다.


### 2.4.5 trace

PTRACE_TRACEME로 자식을 띄우고 PTRACE_SYSCALL로 진입/종료를 반복한다. GETREGS로 시스템 호출 번호/인자/반환값을 읽고, 경로 인자는 PEEKDATA로 문자열을 읽는다. -c 요약, -f 자식 추적, -e 필터를 제공한다.


### 2.4.6 세션 녹화·재생

rec는 REPL 명령 실행 시 stdout/stderr를 tmpfile()로 돌리고, 명령 종료 후 원래 터미널로 복사하면서 길이 기반 레코드에 명령/출력/상태/시간을 기록한다. replay는 명령을 재실행하지 않고 출력과 상태를 재생하며, rec export는 단독 HTML 타임라인을 만든다. -x는 explain 도식과 trace -c 요약을 추가한다(진단을 위해 명령을 한 번 더 실행).

```text
record: C/O/E/S/T/X (명령, stdout, stderr, 상태, ms, 진단)
replay: 저장된 출력/상태/시간을 재표시 (부작용 없음)
export: HTML + inline CSS, 서버 불필요
```


---


# 3. 설계 제한 요소

| 제한 요소 | 설계 기준 | 본 프로젝트의 대응 |
|---|---|---|
| 경제성 | 1,000~10,000줄 원칙 | 직접 작성 9,672줄, 생성 테이블 포함 9,809줄로 준수 |
| 안정성 | 외부 환경 변화에도 동작 | 오류 반환값 검사, 좀비 회수, 재지향 복구, 238개 자동 테스트 |
| 생산성 | 응용 가능한 기능 | 11개 명령, 라인 에디터/스크립트, trace/rec로 학습·검증 도구 제공 |
| 윤리성 | 안전한 파일/네트워크 동작 | rm의 /·.·.. 보호, system()/popen() 미사용, MIT 라이선스 |
| 표준성 | POSIX/Linux 인터페이스 | fork, execvp, waitpid, pipe, dup2, signal, termios, dirent, regex |


## 3.1 경제성

지침의 상한을 지키기 위해 기능 범위를 조정했다. 산정 기준은 다음과 같다.

| 항목 | 줄 수 |
|---|---|
| include/myshell.h + src/*.c | 9,672 |
| 빈 줄 | 1,120 |
| 주석 | 363 |
| 주석·빈 줄 제외 코드 | 8,189 |
| 생성된 trace_syscalls.h | 137 |
| 테스트 코드(별도) | 1,314 |
| 생성 포함 총계 | 9,809 |

원 과제 1~5와 11개 명령을 유지하기 위해 핵심 쉘 코드를 우선했고, 네트워크/HMAC, dbg, pipestat, serve, watch, undo는 범위에서 제외했다. 이 결정으로 10,000줄 이하를 만족하면서 라인 에디터·스크립트·trace·explain·rec라는 투명 쉘 핵심은 유지했다.


## 3.2 안정성

모든 시스템 콜의 반환값을 검사하고, 백그라운드 작업은 waitpid(WNOHANG)로 회수한다. 재지향 실패 시 fd를 복원하고, 라인 에디터는 종료 시 termios를 복구한다. 기능·스크립트·추적·녹화·PTY 테스트와 ASan/UBSan 빌드로 회귀를 확인했다.


## 3.3 생산성

11개 명령은 외부 프로그램 없이 동작하고, 라인 에디터와 스크립트로 반복 작업을 줄인다. rec로 실습 과정을 HTML로 제출하면 채점자가 서버 없이 명령 흐름과 출력을 재현할 수 있다.


## 3.4 윤리성과 사회적 영향

rm은 /, ., ..를 거부하고 재귀 삭제는 심볼릭 링크를 따라가지 않는다. 외부 명령은 execvp()로 직접 실행하며 system()/popen()을 쓰지 않는다. MIT 라이선스로 수정·배포를 허용한다.


---


# 4. 구현 및 검증


## 4.1 요구사항 1~5

| 요구사항 | 구현 | 검증 |
|---|---|---|
| 1. exit | builtin_exit, $? | 종료 상태 테스트 |
| 2. & | job_t, setpgid, jobs/wait/$! | 조기 복귀/jobs/wait |
| 3. Ctrl-C/Ctrl-Z | SIGINT/SIGQUIT, 전경 프로세스 그룹 | PTY 시그널 테스트 |
| 4. 재지향/파이프 | open/dup2/pipe, 다단 파이프 | 파일 비교/2>&1/파이프 |
| 5. 11개 명령 | cmd_ls/cmd_fs/cmd_text | 94개 기능 테스트 |


## 4.2 테스트 설계

tests/run_tests.sh는 stdout/stderr/종료 상태를 비교하는 94개 기능 테스트를 수행한다. test_ext는 explain/&&/glob 16개, test_file_input은 5개, test_script는 56개, test_trace는 32개, test_rec는 35개를 검사한다. test_signals.py와 test_lineedit.py는 pty.fork()로 시그널과 한글 편집, Tab, Ctrl-R, 히스토리 지속성을 확인한다.


## 4.3 테스트 결과

```text
Passed: 94
File-input tests passed: 5
Extension tests passed: 16
Script tests passed: 56
Trace tests passed: 32
Recording tests passed: 35
SIGNAL TEST PASSED
LINE EDITOR TEST PASSED
```


---


# 5. 프로젝트 결과


## 5.1 구현 내용

| 기능 | 상태 | 검증 |
|---|---|---|
| 필수 요구사항 1~5 | 완료 | 94개 기능 + PTY 테스트 |
| 11개 내장 명령 | 완료 | ls/pwd/cd/…/grep |
| 라인 에디터 | 완료 | 한글, 방향키, Tab, Ctrl-R |
| 히스토리 | 완료 | 지속성, !!/!N, history -c |
| 스크립트 | 완료 | if/for/while, 함수, 인자, 파일 |
| trace | 완료 | 시스템 호출/필터/요약/자식 |
| rec/replay | 완료 | 출력/상태/시간, HTML, -x |


## 5.2 완성도 평가

원 과제 요구사항과 11개 명령을 모두 유지했고, 10,000줄 제한을 위해 부가 확장을 과감히 제외했다. 238개 자동 테스트와 PTY 테스트가 통과했고, 소스 규모는 생성 테이블 포함 9,809줄이다.

제외한 기능은 보고서의 범위 결정에 명시했다. 네트워크/HMAC, dbg, pipestat, serve, watch, undo는 구현 이력은 있으나 최종 제출본에서는 빌드·문서·테스트에서 제거해 10,000줄을 보장했다.


## 5.3 일정/역할/위험

| 주차 | 류동엽 | 강동우 | 증빙 |
|---|---|---|---|
| 11주 | simplesh 분석, 요구사항/파서 설계 | 파일/텍스트 명령 분석 | 요구사항 표 |
| 12주 | 토큰화/확장/파이프/재지향 | ls/mkdir/rmdir 초안 | parser.c, execute.c |
| 13주 | 프로세스 그룹/시그널 | ln/cp/rm/mv 경계 | PTY 테스트 |
| 14주 | 11개 명령 통합, 94개 테스트 | cat/grep 옵션 | Passed: 94 |
| 15주 | 라인 에디터/스크립트/trace/rec | 테스트/문서/PTY | 기능별 커밋 |

| 위험 | 대응 | 결과 |
|---|---|---|
| Ctrl-C가 쉘까지 종료 | 자식 setpgid + tcsetpgrp | PTY 통과 |
| 한글 커서 어긋남 | mbrtowc/wcwidth 폭 계산 | PTY 통과 |
| 파서 회귀 | 명령 노드를 기존 parser/executor에 위임 | 94개 통과 |
| ptrace 차단 | PTRACE_TRACEME로 자기 자식만 추적 | WSL2 통과 |
| 녹화 출력 지연 | 명령 종료 후 tmpfile 복사, 화면 제어형 제외 | rec 통과 |
| 10,000줄 초과 | 부가 확장 제거, 산정표 공개 | 9,809줄 |

역할은 5개(pwd, cd, ls, mkdir, rmdir)와 6개(ln, cp, rm, mv, cat, grep)로 나누었고, 라인 에디터/스크립트/trace/rec는 류동엽, 테스트·문서·PTY 검증은 강동우가 담당했다.


# 6. 소감


## 류동엽

프로세스 그룹과 터미널 제어, ptrace, UTF-8 편집을 직접 구현하면서 쉘의 내부 동작을 깊이 이해했다. 특히 10,000줄 제한을 맞추기 위해 기능을 덜어내는 판단이 어려웠고, 남길 것과 버릴 것의 기준을 세우는 것이 설계의 일부임을 배웠다.


## 강동우

재귀 파일 처리와 grep 옵션의 경계 조건을 정리하고, 기능/스크립트/추적/녹화 테스트를 작성했다. 테스트가 있어 범위를 줄이는 변경도 자신 있게 할 수 있었다.


## 함께

기능을 많이 넣는 것보다 요구사항을 지키고 검증 가능한 범위를 유지하는 것이 중요하다는 것을 배웠다.


---


# 부록 A. 빌드 및 실행

```text
make
./myshell
./myshell script.sh arg1 arg2
./myshell -c 'for i in 1 2 3; do echo $i; done'
./myshell -c 'trace -e write /bin/echo hi'
./myshell -c 'rec export lab.rec out.html'
make test
```


# 부록 B. 테스트 항목

| 영역 | 내용 | 개수 |
|---|---|---|
| 기능 | exit, 인용/확장, 재지향, 파이프, 작업, 11개 명령, 오류 | 94 |
| 확장 | explain, &&/\|\|, glob | 16 |
| 파일 입력 | 여러 줄, 마지막 줄, 백그라운드 | 5 |
| 스크립트 | 치환, 제어문, 함수, 스크립트, 오류 | 56 |
| 추적 | 필터/요약/자식/문자열/오류 | 32 |
| 녹화 | rec/replay, 출력, HTML, -x, 오류 | 35 |
| 터미널 | PTY 시그널, 한글 편집, Tab, Ctrl-R | 2 시나리오 |


# 부록 C. 소스 규모

| 파일 | 줄 수 |
|---|---|
| include/myshell.h | 294 |
| src/main.c | 525 |
| src/parser.c | 958 |
| src/execute.c | 487 |
| src/script.c | 1643 |
| src/lineedit.c | 929 |
| src/jobs.c | 154 |
| src/builtin.c | 803 |
| src/cmd_ls.c | 370 |
| src/cmd_fs.c | 952 |
| src/cmd_text.c | 469 |
| src/cmd_trace.c | 573 |
| src/cmd_rec.c | 837 |
| src/cmd_explain.c | 111 |
| src/util.c | 567 |
| src/trace_syscalls.h | 137 |
| 합계 | 9,809 / 직접 작성 9,672 |


# 부록 D. 참고문헌

- 교재 357쪽, simplesh.c (fgets/getargs/fork/execvp/wait)
- Linux man pages: fork(2), execvp(3), waitpid(2), pipe(2), dup2(2), signal(7), sigaction(2), termios(3), tcsetpgrp(3)
- Linux man pages: opendir(3), readdir(3), lstat(2), link(2), symlink(2), rename(2), unlink(2), rmdir(2)
- POSIX regex: regcomp(3), regexec(3), regerror(3)
- Linux man pages: ptrace(2), waitpid(2) __WALL, sys/user.h
- Linux man pages: termios(3), wcwidth(3), mbrtowc(3)
- 프로젝트 저장소: https://github.com/gong000000/lab3
