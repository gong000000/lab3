# myshell - 시스템 프로그래밍 학습용 투명 쉘

교재 357쪽의 `simplesh.c`를 확장한 POSIX 쉘입니다. Lab 3의 5개 요구사항과
11개 내장 명령을 모두 유지하고, **“실행 과정을 관찰·추적·기록하는 투명 쉘”**
컨셉의 시스템 프로그래밍 학습 도구를 추가했습니다.

- 저장소: <https://github.com/gong000000/lab3>
- 라이선스: MIT

## 1. 컨셉

이 프로젝트는 명령을 단순히 실행하는 쉘이 아니라, 쉘과 프로그램이 **어떻게
동작하는지 보이게 하는 쉘**입니다.

| 도구 | 들여다보는 대상 |
|---|---|
| `explain` | 파서가 명령을 해석한 구조 |
| `pipestat` | 파이프 각 단계의 바이트·시간·종료 코드 |
| `trace` | 프로세스의 시스템 호출과 인자/반환값 |
| `dbg` | ELF 심볼, 레지스터, 메모리, 호출 스택, 브레이크포인트 |
| `inspect` | `/proc` 기반 프로세스 상태, fd, 메모리 맵, 네임스페이스 |
| `rec`/`replay` | 세션의 명령·출력·상태·시간과 HTML 타임라인 |

즉 “실행하는 쉘” + “실행을 관찰하는 도구”를 한 바이너리에 결합했습니다.
원 과제 요구사항 1~5와 11개 명령은 그대로 유지합니다.

## 2. 구현 기능

| 구분 | 내용 |
|---|---|
| `exit` | `exit [status]`, `$?` |
| 백그라운드 | `&`, `jobs`, `wait`, `$!` |
| 시그널 | `SIGINT`(Ctrl-C), `SIGQUIT`(Ctrl-Z), 전경 작업만 종료 |
| 재지향/파이프 | `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, `&>`, `&>>`, 다단 `\|` |
| 필수 내장 명령 | `ls`, `pwd`, `cd`, `mkdir`, `rmdir`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep` |
| 보조 내장 명령 | `echo`, `export`, `unset`, `jobs`, `wait`, `help`, `true`, `false`, `:`, `test`, `[` |
| 조건부 실행 | `&&`, `\|\|` |
| glob | `*`, `?`, `[...]` |
| 명령 치환 | `$(...)` |
| 관찰 도구 | `explain`, `pipestat`, `trace`, `dbg`, `inspect` |
| 기록/재생 | `rec start/stop/export`, `replay -s` |

## 3. 실행 방법

### 3.1 빌드

```bash
cd /mnt/c/Users/gong0000/Desktop/lab3
make clean
make
```

### 3.2 실행

```bash
./myshell
./myshell -c 'exit 7'
```

### 3.3 전체 테스트

```bash
make test
```

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

## 4. 원 과제 요구사항 시연

### (1) exit

```text
shell> echo before
before
shell> exit 7
$ echo $?
7
```

### (2) 백그라운드 `&`

```text
shell> sleep 3 &
[1] 12345
shell> jobs
[1]  Running  sleep 3 &
shell> wait
shell> echo done
done
```

### (3) Ctrl-C / Ctrl-Z

```text
shell> sleep 30
^C
shell> echo $?
130
shell> sleep 30
^Z
Quit
shell> echo $?
131
```

Ctrl-Z는 과제 명세에 맞춰 `SIGQUIT`에 매핑했습니다. 일반 리눅스의
`SIGTSTP`와 다르며, 쉘 종료 시 터미널 설정을 복구합니다.

### (4) 재지향과 파이프

```text
shell> echo hello > out.txt
shell> echo world >> out.txt
shell> cat < out.txt
hello
world
shell> cat out.txt | grep -n world
2:world
shell> ls missing 2> err.txt
shell> cat err.txt
```

### (5) 11개 내장 명령

```text
shell> mkdir -p demo/sub
shell> cd demo
shell> pwd
/home/gong0000/lab3/demo
shell> touch a.txt
shell> ln -s a.txt link.txt
shell> cp a.txt b.txt
shell> mv b.txt sub/
shell> ls -l
shell> cat a.txt
shell> grep hello a.txt
shell> rm -r sub
shell> cd ..
shell> rmdir demo
```

## 5. 투명 쉘 도구 시연

### 5.1 explain

```text
shell> explain 'cat a.txt | grep hello > out.txt'
explain: 1 pipeline(s)

[pipeline 1] foreground (first)
  └─ cmd: cat a.txt
  └─ cmd: grep hello
       stdout → out.txt (truncate)
```

### 5.2 pipestat

```text
shell> pipestat 'echo hello | cat | wc -c'
6

[1] echo hello: 6 bytes, 0.000s, status 0
[2] cat: 6 bytes, 0.000s, status 0
[3] wc -c: 2 bytes, 0.003s, status 0
```

### 5.3 trace

```text
shell> trace -e write /bin/echo hi
hi
write(1, 0x..., 2) = 2
shell> trace -c -e openat /bin/cat /etc/hostname
...
   calls  syscall
       3  openat                   100.0%
       3  total
shell> trace -f -e write /bin/sh -c 'echo child'
child
[  1234] write(1, 0x..., 6) = 6
```

### 5.4 dbg

```text
shell> dbg ./a.out
dbg: loaded ./a.out (executable, 10 symbols)
(dbg) break main
Breakpoint 1 at 0x401148 (main)
(dbg) run
Breakpoint 1, 0x401148 in main ()
(dbg) regs
rip=0x401148 rsp=0x7fffffffe4d8 rbp=0x7fffffffe580
...
(dbg) bt
#0  0x401148 in main ()
...
(dbg) x/4x $rsp
0x7fffffffe4d8: 0x000000000040115e ...
(dbg) step
Stopped at 0x40114c in main ()
(dbg) continue
v=42
[Inferior exited with status 0]
(dbg) quit
```

### 5.5 inspect

```text
shell> inspect
shell:
  pid=1234 ppid=1200 pgid=1234 sid=1234
  uid=1000 euid=1000 gid=1000 egid=1000
  cwd=/home/gong0000/lab3
  umask=0022
status (/proc/1234/status):
  Name:   myshell
  State:  R (running)
  VmRSS:  ...
fds (/proc/1234/fd):
  0   -> /dev/pts/0
  1   -> /dev/pts/0
  2   -> /dev/pts/0
maps (/proc/1234/maps):
  ...
namespaces:
  mnt     mnt:[4026531840]
  pid     pid:[4026531836]
  ...
limits:
  NOFILE  soft=1024 hard=1048576
```

옵션: `inspect -f`(fd), `-m`(maps), `-n`(namespaces), `-j`(jobs), `-s`(status),
`inspect PID`로 다른 프로세스도 조사합니다.

### 5.6 rec/replay

```text
shell> rec start lab.rec
recording to lab.rec
shell> cat a.txt | grep hello > out.txt
shell> echo done
done
shell> rec stop
recording stopped: lab.rec (4 commands)
shell> replay lab.rec -s 2
=== replay: lab.rec (5 commands, speed 2.00x) ===
$ cat a.txt | grep hello > out.txt
[status=0, 3ms]
$ echo done
done
[status=0, 0ms]
$ rec stop
recording stopped: lab.rec (4 commands)
[status=0, 0ms]
shell> rec export lab.rec out.html
```

`rec export`는 서버 없이 열리는 단독 HTML 타임라인을 만듭니다. `rec start -x`는
`explain` 도식과 `trace -c` 요약도 HTML에 포함합니다. `-x`는 진단을 위해 명령을
한 번 더 실행하므로 읽기 전용 명령에만 사용합니다.

## 6. 소스 규모와 경제성

| 항목 | 줄 수 |
|---|---:|
| `include/myshell.h` + `src/*.c` | 8,580 |
| 빈 줄 | 994 |
| 주석 | 335 |
| 주석·빈 줄 제외 코드 | 7,251 |
| 생성된 `src/trace_syscalls.h` | 137 |
| 테스트 코드(별도) | 1,201 |
| 생성 포함 총계 | 8,717 |
| 생성·테스트 포함 총계 | 9,918 |

- 직접 작성 소스 8,580줄, 생성 테이블 포함 8,717줄로 **10,000줄 이하**입니다.
- 원 과제 1~5와 11개 내장 명령은 그대로 유지했습니다.
- 라인 에디터/스크립트 자동화, 네트워크/HMAC, `serve`, `watch`, `undo`는
  “투명 쉘” 컨셉과 분량을 위해 제외했습니다.
- 외부 라이브러리 없이 libc/POSIX만 사용합니다.

## 7. 프로젝트 구조

```text
.
├── Makefile
├── README.md
├── LICENSE
├── include/
│   └── myshell.h          공용 구조체와 함수 선언
├── src/
│   ├── main.c             REPL, 명령 분리, 시그널, 터미널
│   ├── parser.c           토큰화, 확장, 문법 분석
│   ├── execute.c          fork/exec, 파이프, 재지향, 캡처
│   ├── jobs.c             백그라운드 작업
│   ├── builtin.c          내장 명령 디스패치
│   ├── cmd_ls.c           ls
│   ├── cmd_fs.c           mkdir, rmdir, ln, cp, rm, mv
│   ├── cmd_text.c         cat, grep
│   ├── cmd_explain.c      파서 시각화
│   ├── cmd_pipestat.c     파이프 계측
│   ├── cmd_trace.c        ptrace 시스템 호출 추적
│   ├── cmd_dbg.c          ELF/ptrace 미니 디버거
│   ├── cmd_inspect.c      /proc 기반 프로세스 조사
│   ├── cmd_rec.c          세션 녹화·재생·HTML
│   ├── trace_syscalls.h   시스템 호출 이름 테이블 (생성)
│   └── util.c             공용 유틸
├── tools/
│   └── gen_trace_names.py
├── tests/
│   ├── run_tests.sh       94개 기능 테스트
│   ├── test_file_input.sh 4개 파일 입력 테스트
│   ├── test_ext.sh        21개 explain/&&/glob/pipestat
│   ├── test_trace.sh      32개 추적 테스트
│   ├── test_dbg.sh        44개 디버거 테스트
│   ├── test_rec.sh        35개 녹화 테스트
│   ├── test_inspect.sh    27개 /proc 조사 테스트
│   └── test_signals.py    PTY 시그널 테스트
└── docs/
    ├── REPORT.md / REPORT.docx / REPORT.hwp / REPORT.pdf
    ├── make_report.py
    ├── make_pdf.py
    └── make_hwp_text.py
```

## 8. 내장 명령 옵션

| 명령 | 옵션 |
|---|---|
| `ls` | `-a`, `-l`, `-R`, `-1`, `-h` |
| `pwd` | `-L`, `-P` |
| `cd` | `cd`, `cd DIR`, `cd -` |
| `mkdir` | `-p`, `-m MODE`, `-v` |
| `rmdir` | `-p`, `-v` |
| `ln` | `-s`, `-f`, `-v` |
| `cp` | `-r`/`-R`, `-f`, `-i`, `-p`, `-v` |
| `rm` | `-r`/`-R`, `-f`, `-i`, `-v` |
| `mv` | `-f`, `-i`, `-n`, `-v` |
| `cat` | `-n`, `-b`, `-E`, `-T`, `-A` |
| `grep` | `-i`, `-v`, `-n`, `-c`, `-l`, `-r`/`-R`, `-E`, `-F`, `-q`, `-s`, `-h`, `-H`, `-e` |
| `explain` | `explain '명령줄'` |
| `pipestat` | `pipestat 'cmd1 \| cmd2'` |
| `trace` | `-c`, `-f`, `-e NAME[,NAME...]` |
| `dbg` | `PROGRAM [ARGS...]`; `break`, `run`, `continue`, `step`, `finish`, `bt`, `regs`, `x/Nx`, `info break`, `delete`, `quit` |
| `inspect` | `-f`, `-m`, `-n`, `-j`, `-s`, `PID` |
| `rec` | `start [-x] FILE`, `stop`, `status`, `export FILE HTML` |
| `replay` | `FILE [-s SPEED]`, `-x` |

## 9. 제약

- 쉘 스크립트 자동화(`if/for/while`, 함수)와 라인 에디터는 최종 범위에서
  제외했습니다. 원 과제 요구사항에는 포함되지 않습니다.
- `$(...)` 명령 치환은 지원합니다.
- `trace`/`dbg`는 x86_64 Linux 전용이며 `ptrace`가 필요합니다.
- `rec`는 전경 명령의 출력을 명령 종료 후 기록합니다. `-x` 진단은 명령을
  한 번 더 실행합니다.
- `dbg`의 `bt`는 프레임 포인터 기반이라 최적화 빌드에서는 부정확할 수 있습니다.

## 10. 팀 역할과 주차별 기록

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어, 파서/실행기, 시그널, explain/pipestat/trace/dbg/inspect/rec, `pwd`/`cd`/`ls`/`mkdir`/`rmdir` |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `ln`/`cp`/`rm`/`mv`/`cat`/`grep`, 테스트, 문서, PTY 검증 |

| 주차 | 류동엽 | 강동우 | 증빙 |
|---|---|---|---|
| 11주 | simplesh 분석, 요구사항/파서 설계 | 파일/텍스트 명령 분석 | 요구사항 표 |
| 12주 | 토큰화/확장/파이프/재지향 | ls/mkdir/rmdir 초안 | parser.c, execute.c |
| 13주 | 프로세스 그룹/시그널 | ln/cp/rm/mv 경계 | PTY 테스트 |
| 14주 | 11개 명령 통합, 94개 테스트 | cat/grep 옵션 | `Passed: 94` |
| 15주 | explain/pipestat/trace/dbg/inspect/rec | 테스트/문서/PTY | 기능별 커밋 |

## 11. 보고서 재생성

```bash
python docs/make_report.py
python docs/make_pdf.py
python docs/make_hwp_text.py
```

## 12. GitHub 및 라이선스

- <https://github.com/gong000000/lab3>
- Collaborator: `pizza0420` (강동우), write 권한
- MIT License: [LICENSE](LICENSE)
