# myshell

`simplesh.c`를 확장한 POSIX 쉘입니다. 원 과제의 필수 기능 5개와 내장 명령 11개를
구현했고, 파서·파이프·시스템 호출·프로세스 상태를 확인하는 도구를 함께 넣었습니다.

본 프로젝트는 Lab 3의 5개 요구사항과 11개 내장 명령을 완성하고, 그 위에
“시스템 프로그래밍 학습용 투명 쉘” 컨셉을 올렸습니다. 명령을 실행하는 데서
끝나지 않고 파서·파이프·시스템 호출·ELF·프로세스 상태를 관찰하고, 세션을
기록·재생하는 도구를 같은 바이너리에 결합했습니다.

- 저장소: <https://github.com/gong000000/lab3>
- 라이선스: MIT

## 1. 기능

| 구분 | 내용 |
|---|---|
| `exit` | `exit [status]`, `$?` |
| 백그라운드 | `&`, `jobs`, `wait`, `$!` |
| 시그널 | `SIGINT`(Ctrl-C), `SIGQUIT`(Ctrl-Z) |
| 재지향/파이프 | `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, `&>`, `&>>`, 다단 `\|` |
| 필수 내장 명령 | `ls`, `pwd`, `cd`, `mkdir`, `rmdir`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep` |
| 보조 내장 명령 | `echo`, `export`, `unset`, `jobs`, `wait`, `help`, `true`, `false`, `:` |
| 조건부 실행 | `&&`, `\|\|` |
| glob | `*`, `?`, `[...]` |
| 명령 치환 | `$(...)` |
| 파서 확인 | `explain` |
| 파이프 계측 | `pipestat` |
| 시스템 호출 추적 | `trace` |
| ELF 디버거 | `dbg` |
| 프로세스 조사 | `inspect` |
| 세션 녹화/재생 | `rec`, `replay` |

## 2. 실행 환경

- Linux 또는 WSL2, x86_64
- `gcc`, `make`, `python3`
- `trace`, `dbg`는 `ptrace`가 허용된 환경에서 동작

```bash
cd /mnt/c/Users/gong0000/Desktop/lab3
make clean
make
./myshell
```

```bash
./myshell -c 'exit 7'
```

## 3. 원 과제 기능

### exit

```text
shell> echo before
before
shell> exit 7
$ echo $?
7
```

### 백그라운드 실행

```text
shell> sleep 3 &
[1] 12345
shell> jobs
[1]  Running  sleep 3 &
shell> wait
```

### Ctrl-C / Ctrl-Z

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

Ctrl-Z는 과제 명세에 맞춰 `SIGQUIT`으로 처리합니다.

### 재지향과 파이프

```text
shell> echo hello > out.txt
shell> echo world >> out.txt
shell> cat < out.txt
hello
world
shell> cat out.txt | grep -n world
2:world
shell> ls missing 2> err.txt
```

### 내장 명령

```text
shell> mkdir -p demo/sub
shell> cd demo
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

## 4. 관찰 도구

### explain

```text
shell> explain 'cat a.txt | grep hello > out.txt'
explain: 1 pipeline(s)

[pipeline 1] foreground (first)
  └─ cmd: cat a.txt
  └─ cmd: grep hello
       stdout → out.txt (truncate)
```

### pipestat

```text
shell> pipestat 'echo hello | cat | wc -c'
6

[1] echo hello: 6 bytes, 0.000s, status 0
[2] cat: 6 bytes, 0.000s, status 0
[3] wc -c: 2 bytes, 0.003s, status 0
```

### trace

```text
shell> trace -e write /bin/echo hi
hi
write(1, 0x..., 2) = 2
shell> trace -c -e openat /bin/cat /etc/hostname
   calls  syscall
       3  openat                   100.0%
       3  total
shell> trace -f -e write /bin/sh -c 'echo child'
child
[  1234] write(1, 0x..., 6) = 6
```

### dbg

```text
shell> dbg ./a.out
dbg: loaded ./a.out (executable, 10 symbols)
(dbg) break main
Breakpoint 1 at 0x401148 (main)
(dbg) run
Breakpoint 1, 0x401148 in main ()
(dbg) regs
rip=0x401148 rsp=0x7fffffffe4d8 rbp=0x7fffffffe580
(dbg) bt
#0  0x401148 in main ()
(dbg) x/4x $rsp
0x7fffffffe4d8: 0x000000000040115e ...
(dbg) step
Stopped at 0x40114c in main ()
(dbg) continue
v=42
[Inferior exited with status 0]
(dbg) quit
```

### inspect

```text
shell> inspect
shell:
  pid=... ppid=... pgid=... sid=...
  uid=... cwd=... umask=...
status (/proc/.../status):
  Name: myshell
  VmRSS: ...
fds (/proc/.../fd):
  0 -> /dev/pts/0
  1 -> /dev/pts/0
maps (/proc/.../maps):
  ...
namespaces:
  mnt ...
  pid ...
limits:
  NOFILE ...
```

```text
inspect -f      # fd
inspect -m      # memory maps
inspect -n      # namespaces
inspect -j      # jobs
inspect -t      # process tree
inspect PID
```

### rec/replay

```text
shell> rec start lab.rec
recording to lab.rec
shell> cat a.txt | grep hello > out.txt
shell> rec stop
recording stopped: lab.rec (2 commands)
shell> replay lab.rec -s 2
=== replay: lab.rec (3 commands, speed 2.00x) ===
$ cat a.txt | grep hello > out.txt
[status=0, 3ms]
$ rec stop
recording stopped: lab.rec (2 commands)
[status=0, 0ms]
shell> rec export lab.rec out.html
```

전경 명령의 출력은 실행 중에 표시되고 동시에 기록됩니다. `rec start -x`는
`explain`과 `trace -c` 요약을 HTML에 추가합니다.

## 5. 테스트

```bash
make test
```

```text
Passed: 94
File-input tests passed: 4
Extension tests passed: 21
Trace tests passed: 32
Debugger tests passed: 46
Recording tests passed: 35
Inspect tests passed: 31
SIGNAL TEST PASSED
```

## 6. 소스 규모

| 항목 | 줄 수 |
|---|---:|
| `include/myshell.h` + `src/*.c` | 8,626 |
| 빈 줄 | 1,007 |
| 주석 | 164 |
| 주석·빈 줄 제외 코드 | 7,455 |
| 생성된 `src/trace_syscalls.h` | 137 |
| 테스트 코드 | 1,214 |
| 생성 포함 총계 | 8,763 |
| 생성·테스트 포함 총계 | 9,977 |

## 7. 파일 구성

```text
.
├── Makefile
├── README.md
├── LICENSE
├── include/
│   └── myshell.h
├── src/
│   ├── main.c
│   ├── parser.c
│   ├── execute.c
│   ├── jobs.c
│   ├── builtin.c
│   ├── cmd_ls.c
│   ├── cmd_fs.c
│   ├── cmd_text.c
│   ├── cmd_explain.c
│   ├── cmd_pipestat.c
│   ├── cmd_trace.c
│   ├── cmd_dbg.c
│   ├── cmd_inspect.c
│   ├── cmd_rec.c
│   ├── trace_syscalls.h
│   └── util.c
├── tools/
│   └── gen_trace_names.py
├── tests/
│   ├── run_tests.sh
│   ├── test_file_input.sh
│   ├── test_ext.sh
│   ├── test_trace.sh
│   ├── test_dbg.sh
│   ├── test_rec.sh
│   ├── test_inspect.sh
│   └── test_signals.py
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
| `inspect` | `-f`, `-m`, `-n`, `-j`, `-s`, `-t`, `PID` |
| `rec` | `start [-x] FILE`, `stop`, `status`, `export FILE HTML` |
| `replay` | `FILE [-s SPEED]`, `-x` |

## 9. 팀 역할

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어, 파서/실행기, 시그널, explain/pipestat/trace/dbg/inspect/rec, `pwd`/`cd`/`ls`/`mkdir`/`rmdir` |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `ln`/`cp`/`rm`/`mv`/`cat`/`grep`, 테스트, 문서, PTY 검증 |

| 주차 | 류동엽 | 강동우 |
|---|---|---|
| 11주 | simplesh 분석, 요구사항/파서 설계 | 파일/텍스트 명령 분석 |
| 12주 | 토큰화/확장/파이프/재지향 | ls/mkdir/rmdir 초안 |
| 13주 | 프로세스 그룹/시그널 | ln/cp/rm/mv 경계 |
| 14주 | 11개 명령 통합, 94개 테스트 | cat/grep 옵션 |
| 15주 | explain/pipestat/trace/dbg/inspect/rec | 테스트/문서/PTY |

## 10. 보고서 재생성

```bash
python docs/make_report.py
python docs/make_pdf.py
python docs/make_hwp_text.py
```

## 11. GitHub 및 라이선스

- <https://github.com/gong000000/lab3>
- MIT License: [LICENSE](LICENSE)
