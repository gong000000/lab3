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

교재 357쪽의 simplesh.c는 fgets()로 한 줄을 읽고, getargs()로 인자를 분리한 뒤 fork()/execvp()로 명령을 실행하는 기본 쉘이다. exit, 백그라운드, 인터럽트, 재지향, 파이프, 내장 명령이 빠져 있어 실제 쉘과 시스템 프로그래밍 학습 도구로 쓰기에는 부족하다.

본 프로젝트는 Lab 3의 5개 요구사항과 11개 내장 명령을 먼저 완성하고, 그 위에 “시스템 프로그래밍 학습용 투명 쉘” 컨셉을 올렸다. 명령을 실행하는 데서 끝나지 않고, 파서·파이프·시스템 호출·ELF·프로세스 상태를 관찰하고 세션을 기록·재생하는 도구를 같은 바이너리에 결합했다.


## 1.2 프로젝트 목표

- exit, &, SIGINT/SIGQUIT, 재지향/파이프를 구현한다.
- ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep을 직접 구현한다.
- explain으로 파서 구조를, pipestat으로 파이프 단계를 보여 준다.
- trace/dbg로 시스템 호출과 ELF 실행을 들여다본다.
- inspect로 /proc 기반 프로세스 상태(fd, maps, namespace)를 조사한다.
- rec/replay로 세션을 기록하고 HTML 타임라인으로 남긴다.
- 원 과제 요구사항을 유지하면서 소스를 10,000줄 이하로 관리한다.


## 1.3 구현 범위

| 구분 | 내용 |
|---|---|
| 필수 기능 | exit, 백그라운드(&), SIGINT/SIGQUIT, 재지향(>, <), 파이프(\|) |
| 필수 내장 명령 | ls, pwd, cd, mkdir, rmdir, ln, cp, rm, mv, cat, grep (11개) |
| 보조 내장 명령 | echo, export, unset, jobs, wait, help, true, false, :, test, [ |
| 관찰 도구 | explain(파서), pipestat(파이프), trace(시스템 호출), dbg(ELF/ptrace), inspect(/proc) |
| 기록 도구 | rec/replay: 명령·출력·상태·시간, -x explain/trace, HTML export |
| 범위 제외 | 라인 에디터/스크립트 자동화, 네트워크/HMAC, serve, watch, undo |
| 문서/배포 | README, MIT LICENSE, PDF/HWP/DOCX 보고서, 자동 테스트 |


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

최종 소스는 직접 작성 8,611줄, 생성 테이블 포함 8,748줄로 10,000줄 이하를 만족한다. 11개 필수 명령과 관찰 도구 5종(explain, pipestat, trace, dbg, inspect), 기록 도구(rec/replay)를 제공하며, 기능 94개·확장 21개·파일 입력 4개·추적 32개·디버거 44개·녹화 35개·조사 27개 테스트와 PTY 시그널 테스트를 통과했다.

| 항목 | 결과 |
|---|---|
| 소스 규모 | 직접 작성 8,611줄, 생성 포함 8,748줄 (10,000줄 이하) |
| 필수 내장 명령 | 11개 |
| 기능 테스트 | 94 / 94 |
| 확장 테스트 | 21 / 21 (explain, &&/\|\|, glob, pipestat) |
| 파일 입력 | 4 / 4 |
| 추적 | 32 / 32 |
| 디버거 | 44 / 44 |
| 녹화 | 35 / 35 |
| 조사 | 27 / 27 |
| 시그널 | PTY 시그널 테스트 통과 |


---


# 2. 설계 구성 요소


## 2.1 시스템 구조

```text
+---------------------------- 사용자 터미널 ----------------------------+
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
          +-----------------------------+
```


## 2.2 모듈 구성

| 파일 | 역할 | 줄 수 |
|---|---|---|
| include/myshell.h | 공용 구조체와 함수 원형 | 263 |
| src/main.c | REPL, 명령 분리, 시그널, 터미널 | 419 |
| src/parser.c | 토큰화, 확장, 문법 분석 | 935 |
| src/execute.c | fork/exec, 파이프, 재지향, 캡처 | 469 |
| src/jobs.c | 백그라운드 작업 | 154 |
| src/builtin.c | 내장 명령 디스패치 | 625 |
| src/cmd_ls.c | ls | 370 |
| src/cmd_fs.c | mkdir/rmdir/ln/cp/rm/mv | 952 |
| src/cmd_text.c | cat/grep | 469 |
| src/cmd_explain.c | 파서 시각화 | 111 |
| src/cmd_pipestat.c | 파이프 단계 계측 | 330 |
| src/cmd_trace.c | ptrace 시스템 호출 추적 | 573 |
| src/cmd_dbg.c | ELF/ptrace 미니 디버거 | 1263 |
| src/cmd_inspect.c | /proc 기반 프로세스 조사 | 274 |
| src/cmd_rec.c | 세션 녹화/재생/HTML | 837 |
| src/util.c | 공용 유틸 | 567 |
| src/trace_syscalls.h | 시스템 호출 이름 테이블 (생성) | 137 |
| 합계 | 직접 작성 8,611 + 생성 137 = 8,748 |  |


## 2.3 핵심 자료구조

| 구조체 | 주요 필드 | 역할 |
|---|---|---|
| command_t | argv[], argc, redir | 한 명령의 인자/재지향 |
| pipeline_t | commands[], background, text | 파이프 묶음 |
| job_t | pgid, pids[], status, command | 백그라운드 작업 |
| shell_state_t | last_status, jobs[], positional[] | 쉘 상태 |
| tracee_t | pid, entered, nr, args[6] | ptrace 추적 상태 |
| dbg_t | pid, bias, symbols[], breakpoints[] | 디버거 상태 |
| rec_item_t | cmd, out, err, status, ms | 녹화 명령 하나 |


## 2.4 핵심 설계


### 2.4.1 파싱과 실행

main.c의 find_separator()가 따옴표를 고려해 ;와 &를 분리하고, parser.c가 단어/연산자를 토큰화한다. 따옴표, $VAR, $?, $$, $!, $(...)를 확장하고 파이프/재지향/&&/||/glob을 처리한다. cd, exit, jobs, wait, export처럼 쉘 상태를 바꾸는 내장 명령은 부모에서, 나머지는 자식에서 실행한다.


### 2.4.2 시그널과 작업 제어

전경 파이프라인은 하나의 프로세스 그룹으로 묶고 tcsetpgrp()로 터미널을 넘긴다. 자식은 SIGINT/SIGQUIT 등을 기본값으로 복원하므로 Ctrl-C/Ctrl-Z는 전경 작업만 중단한다. 과제 명세에 맞춰 VQUIT를 Ctrl-Z로 매핑하고 VSUSP를 비활성화한다.


### 2.4.3 explain

parse_line() 결과를 실행하지 않고 파이프라인, 명령, 재지향, 백그라운드 구조로 출력한다. “쉘이 입력을 어떻게 해석했는가”를 보여 주는 가장 기본적인 투명성 도구이다.


### 2.4.4 pipestat

파이프 단계 사이에 relay 프로세스를 넣어 바이트 수, 시작/종료 시각, 종료 코드를 수집한다. 어느 단계가 데이터를 많이 만들고 오래 걸리는지 보여 주어 파이프라인 학습에 사용한다.


### 2.4.5 trace

PTRACE_TRACEME로 자식을 띄우고 PTRACE_SYSCALL로 진입/종료를 반복한다. GETREGS로 시스템 호출 번호/인자/반환값을 읽고, 경로 인자는 PEEKDATA로 문자열을 읽는다. -c 요약, -f 자식 추적, -e 필터를 제공한다.


### 2.4.6 dbg

ELF .symtab/.dynsym에서 함수 심볼을 읽고, PIE는 /proc/PID/maps에서 로드 기준 주소를 계산한다. 0xCC(int3) 브레이크포인트를 심고, 정지 시 원본 바이트를 복원한 뒤 rip를 1 되돌린다. step/continue에서 한 명령을 실행한 후 브레이크포인트를 다시 심는다. regs, x/Nx, bt, finish, SIGSEGV 자동 분석을 지원한다.


### 2.4.7 inspect

inspect는 /proc/PID/status, fd, maps, ns를 읽어 프로세스의 상태를 보여 준다. 쉘 자신의 pid/ppid/pgid/sid, uid/gid, cwd, umask, rlimit, 백그라운드 작업도 함께 출력한다. 시스템 프로그래밍에서 프로세스와 파일 디스크립터가 어떻게 표현되는지 직접 확인하는 도구이다.


### 2.4.8 rec/replay

rec는 REPL 명령 실행 시 stdout/stderr를 tmpfile()로 돌리고, 명령 종료 후 원래 터미널로 복사하면서 명령/출력/상태/시간을 길이 기반 레코드에 기록한다. replay는 명령을 재실행하지 않고 출력과 상태를 재생하며, rec export는 단독 HTML 타임라인을 만든다. -x는 explain과 trace -c 요약을 추가한다.

```text
record: C/O/E/S/T/X (명령, stdout, stderr, 상태, ms, 진단)
replay: 저장된 출력/상태/시간을 재표시 (부작용 없음)
export: HTML + inline CSS, 서버 불필요
```


---


# 3. 설계 제한 요소

| 제한 요소 | 설계 기준 | 본 프로젝트의 대응 |
|---|---|---|
| 경제성 | 1,000~10,000줄 원칙 | 직접 작성 8,611줄, 생성 포함 8,748줄로 준수 |
| 안정성 | 외부 환경 변화에도 동작 | 오류 반환값 검사, 좀비 회수, 재지향 복구, 257개 자동 테스트 |
| 생산성 | 학습/응용 가능 | 11개 명령 + explain/pipestat/trace/dbg/inspect/rec |
| 윤리성 | 안전한 파일 동작 | rm의 /·.·.. 보호, system()/popen() 미사용, MIT 라이선스 |
| 표준성 | POSIX/Linux 인터페이스 | fork, execvp, waitpid, pipe, dup2, signal, termios, dirent, regex |


## 3.1 경제성

지침의 상한을 지키기 위해 “투명 쉘” 컨셉에 직접 기여하지 않는 기능은 과감히 제외했다. 산정 기준은 다음과 같다.

| 항목 | 줄 수 |
|---|---|
| include/myshell.h + src/*.c | 8,611 |
| 빈 줄 | 997 |
| 주석 | 337 |
| 주석·빈 줄 제외 코드 | 7,277 |
| 생성된 trace_syscalls.h | 137 |
| 테스트 코드(별도) | 1,201 |
| 생성 포함 총계 | 8,748 |

라인 에디터/스크립트 자동화, 네트워크/HMAC, dbg를 제외한 것이 아니라, 이번 최종본에서는 관찰 도구(dbg 포함)를 남기고 자동화·UX·네트워크를 제외했다. 그 결과 원 과제 1~5와 11개 명령을 유지하면서 10,000줄 이하를 만족한다.


## 3.2 안정성

모든 시스템 콜의 반환값을 검사하고, 백그라운드 작업은 waitpid(WNOHANG)로 회수한다. 재지향 실패 시 fd를 복원하고, 쉘 종료 시 termios를 복구한다. 기능·확장·추적·디버거·녹화·조사 테스트와 PTY 시그널 테스트로 회귀를 확인했다.


## 3.3 생산성

11개 명령은 외부 프로그램 없이 동작한다. explain/pipestat/trace/dbg/inspect는 “왜 이런 결과가 나왔는가”를 단계별로 보여 주어 시스템 프로그래밍 학습 시간을 줄인다. rec는 실습 과정을 HTML로 제출해 채점자가 서버 없이 재현할 수 있게 한다.


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

tests/run_tests.sh는 stdout/stderr/종료 상태를 비교하는 94개 기능 테스트를 수행한다. test_ext는 explain/&&/glob/pipestat 21개, test_file_input은 4개, test_trace는 32개, test_dbg는 44개, test_rec는 35개, test_inspect는 27개를 검사한다. test_signals.py는 pty.fork()로 Ctrl-C/Ctrl-Z 후 쉘 생존을 확인한다.


## 4.3 테스트 결과

```text
Passed: 94
File-input tests passed: 4
Extension tests passed: 21
Trace tests passed: 32
Debugger tests passed: 44
Recording tests passed: 35
Inspect tests passed: 27
SIGNAL TEST PASSED
```


---


# 5. 프로젝트 결과


## 5.1 구현 내용

| 기능 | 상태 | 검증 |
|---|---|---|
| 필수 요구사항 1~5 | 완료 | 94개 기능 + PTY |
| 11개 내장 명령 | 완료 | ls/pwd/cd/…/grep |
| explain | 완료 | 파이프/재지향 도식 |
| pipestat | 완료 | 단계별 바이트/시간/상태 |
| trace | 완료 | 시스템 호출/필터/요약 |
| dbg | 완료 | ELF/PIE, break/step/bt, SIGSEGV |
| inspect | 완료 | /proc status/fd/maps/ns |
| rec/replay | 완료 | 출력/상태/시간, HTML |


## 5.2 완성도 평가

원 과제 요구사항과 11개 명령을 모두 유지했고, 관찰 도구를 중심으로 범위를 재편해 8,748줄(생성 포함)로 줄였다. 257개 자동 테스트와 PTY 시그널 테스트가 통과했다. 라인 에디터/스크립트 자동화/네트워크/serve/watch/undo는 컨셉과 분량을 위해 제외했다.


## 5.3 일정/역할/위험

| 주차 | 류동엽 | 강동우 | 증빙 |
|---|---|---|---|
| 11주 | simplesh 분석, 요구사항/파서 설계 | 파일/텍스트 명령 분석 | 요구사항 표 |
| 12주 | 토큰화/확장/파이프/재지향 | ls/mkdir/rmdir 초안 | parser.c/execute.c |
| 13주 | 프로세스 그룹/시그널 | ln/cp/rm/mv 경계 | PTY 테스트 |
| 14주 | 11개 명령 통합, 94개 테스트 | cat/grep 옵션 | Passed: 94 |
| 15주 | explain/pipestat/trace/dbg/inspect/rec | 테스트/문서/PTY | 기능별 커밋 |

| 위험 | 대응 | 결과 |
|---|---|---|
| Ctrl-C가 쉘까지 종료 | 자식 setpgid + tcsetpgrp | PTY 통과 |
| ptrace 차단 | 자기 자식만 PTRACE_TRACEME | WSL2 통과 |
| PIE 주소 불일치 | /proc/PID/maps bias 계산 | dbg PIE 테스트 |
| 브레이크포인트 재정지 | 복원→SINGLESTEP→재설치 | dbg 통과 |
| 녹화 출력 지연 | 명령 종료 후 tmpfile 복사 | rec 통과 |
| 10,000줄 초과 | 관찰 도구 중심으로 범위 재편 | 8,748줄 |

역할은 5개(pwd, cd, ls, mkdir, rmdir)와 6개(ln, cp, rm, mv, cat, grep)로 나누었고, 관찰 도구/코어는 류동엽, 테스트·문서·PTY 검증은 강동우가 담당했다.


# 6. 소감


## 류동엽

프로세스 그룹, ptrace, ELF, /proc을 직접 다루면서 쉘이 단순한 실행기가 아니라 운영체제와 상호작용하는 프로그램임을 이해했다. 기능을 많이 넣는 것보다 주제에 맞는 도구를 남기고 10,000줄 제한을 지키는 판단이 중요했다.


## 강동우

파일/텍스트 명령의 경계 조건과 테스트를 정리하고, trace/dbg/rec/inspect 시나리오를 검증했다. 테스트가 있어 범위를 줄이는 변경도 안전하게 할 수 있었다.


## 함께

“실행하는 쉘”보다 “실행을 보이게 하는 쉘”이 시스템 프로그래밍 학습에 더 잘 맞는다는 결론을 얻었다.


---


# 부록 A. 빌드 및 실행

```text
make
./myshell
./myshell -c 'exit 7'
./myshell -c 'explain "cat a | grep b"'
./myshell -c 'pipestat "echo hi | cat | wc -c"'
./myshell -c 'trace -e write /bin/echo hi'
./myshell -c 'inspect'
./myshell -c 'rec export lab.rec out.html'
make test
```


# 부록 B. 테스트 항목

| 영역 | 내용 | 개수 |
|---|---|---|
| 기능 | exit, 인용/확장, 재지향, 파이프, 작업, 11개 명령, 오류 | 94 |
| 확장 | explain, &&/\|\|, glob, pipestat | 21 |
| 파일 입력 | 여러 줄, 마지막 줄, 백그라운드 | 4 |
| 추적 | 필터/요약/자식/문자열/오류 | 32 |
| 디버거 | ELF/PIE, break/step/finish/bt, SIGSEGV | 44 |
| 녹화 | rec/replay, 출력, HTML, -x, 오류 | 35 |
| 조사 | /proc status/fd/maps/ns/jobs/limits/오류 | 27 |
| 시그널 | PTY Ctrl-C/Ctrl-Z | 1 시나리오 |


# 부록 C. 소스 규모

| 파일 | 줄 수 |
|---|---|
| include/myshell.h | 263 |
| src/main.c | 419 |
| src/parser.c | 935 |
| src/execute.c | 469 |
| src/jobs.c | 154 |
| src/builtin.c | 625 |
| src/cmd_ls.c | 370 |
| src/cmd_fs.c | 952 |
| src/cmd_text.c | 469 |
| src/cmd_explain.c | 111 |
| src/cmd_pipestat.c | 330 |
| src/cmd_trace.c | 573 |
| src/cmd_dbg.c | 1263 |
| src/cmd_inspect.c | 274 |
| src/cmd_rec.c | 837 |
| src/util.c | 567 |
| src/trace_syscalls.h | 137 |
| 합계 | 8,748 / 직접 작성 8,611 |


# 부록 D. 참고문헌

- 교재 357쪽, simplesh.c (fgets/getargs/fork/execvp/wait)
- Linux man pages: fork(2), execvp(3), waitpid(2), pipe(2), dup2(2), signal(7), sigaction(2), termios(3), tcsetpgrp(3)
- Linux man pages: opendir(3), readdir(3), lstat(2), link(2), symlink(2), rename(2), unlink(2), rmdir(2)
- POSIX regex: regcomp(3), regexec(3), regerror(3)
- Linux man pages: ptrace(2), waitpid(2) __WALL, sys/user.h
- Linux man pages: proc(5), getrlimit(2), readlink(2), gethostname(2)
- 프로젝트 저장소: https://github.com/gong000000/lab3
