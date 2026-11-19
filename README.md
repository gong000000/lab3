# myshell - Lab 3 팀 프로젝트

교재 357쪽의 `simplesh.c`를 확장한 POSIX 쉘입니다. Lab 3의 5개 요구사항과
11개 내장 명령을 모두 유지하고, 그 위에 **시스템 프로그래밍 학습용 투명 쉘**
컨셉의 기능을 추가했습니다.

- 저장소: <https://github.com/gong000000/lab3>
- 라이선스: MIT

## 1. 구현 기능

| 구분 | 내용 |
|---|---|
| `exit` | `exit [status]`, `$?` |
| 백그라운드 | `&`, `jobs`, `wait`, `$!` |
| 시그널 | `SIGINT`(Ctrl-C), `SIGQUIT`(Ctrl-Z), 전경 작업만 종료 |
| 재지향/파이프 | `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, `&>`, `&>>`, 다단 `\|` |
| 필수 내장 명령 | `ls`, `pwd`, `cd`, `mkdir`, `rmdir`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep` |
| 보조 내장 명령 | `echo`, `export`, `unset`, `jobs`, `wait`, `help`, `true`, `false`, `:`, `test`, `[`, `read`, `shift`, `source`, `.`, `trace`, `history`, `rec`, `replay` |
| 조건부 실행 | `&&`, `\|\|` |
| glob | `*`, `?`, `[...]` (인용되지 않은 경우) |
| 파서 시각화 | `explain '명령줄'` |
| 라인 에디터 | 방향키, Home/End, Backspace/Delete, Ctrl-U/K/W, UTF-8/한글 폭 보정 |
| 히스토리 | `~/.myshell_history`, `history [-c]`, `!!`, `!N`, `!-N`, Ctrl-R 역검색 |
| 자동 완성 | Tab으로 내장 명령·`PATH` 실행 파일·파일/디렉터리 완성 |
| 명령 치환 | `$(...)`, 중첩 치환, 따옴표 안 치환 |
| 스크립트 | `if/elif/else/fi`, `for`, `while`/`until`, `break`, `continue`, 함수, `return` |
| 위치 매개변수 | `$1`~`$9`, `$#`, `$@`, `$*`, `${...}`, `shift` |
| 스크립트 실행 | `myshell SCRIPT [ARGS...]`, `source FILE [ARGS...]`, `.` |
| 시스템 호출 추적 | `trace [-c] [-f] [-e NAME,...] COMMAND` (x86_64 ptrace, 인자·반환값) |
| 세션 녹화/재생 | `rec start [-x] FILE`, `rec stop`, `rec export FILE OUT.html`, `replay FILE [-s SPEED]` |
| 프롬프트 확장 | `prompt on/off` (종료 코드·실행 시간·작업 수) |

확장 기능은 단순한 기능 모음이 아니라 **“시스템 프로그래밍 학습용 투명 쉘”**로
묶었습니다. `explain`은 파서를, `trace`는 시스템 호출을, `rec`/`replay`는 세션
전체를 들여다보게 합니다. 즉 “명령을 실행하는 쉘”이 아니라 “실행 과정을
관찰·기록·재현하는 쉘”입니다.

## 2. 실행 방법

### 2.1 준비

Linux 또는 WSL(Ubuntu)에서 실행합니다.

```bash
cd /mnt/c/Users/gong0000/Desktop/lab3
sudo apt update
sudo apt install -y build-essential python3
```

### 2.2 빌드

```bash
make clean
make
```

### 2.3 실행

```bash
./myshell
```

```text
$ ./myshell
shell> pwd
/home/gong0000/lab3
shell> exit
```

```bash
./myshell -c 'exit 7'
echo $?
./myshell demo.sh alice bob
./myshell -c 'for i in 1 2 3; do echo $i; done'
```

### 2.4 전체 테스트

```bash
make test
```

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

## 3. 요구사항별 시연

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

## 4. 확장 기능 시연

### 4.1 라인 에디터와 히스토리

```text
shell> echo 한글
한글
shell> ech<Tab>
shell> echo 자동완성
자동완성
shell> pwd
/home/gong0000/lab3
shell> <↑>
shell> pwd
shell> <Ctrl-R>
(reverse-i-search)`pwd': pwd
shell> history
    1  echo 한글
    2  echo 자동완성
    3  pwd
```

### 4.2 쉘 스크립트

```text
shell> for i in 1 2 3; do echo "i=$i"; done
i=1
i=2
i=3
shell> if [ -f README.md ]; then echo file; else echo missing; fi
file
shell> greet() { echo "hello $1 ($# args)"; }; greet myshell
hello myshell (1 args)
shell> echo "pwd=$(pwd)"
pwd=/home/gong0000/lab3
```

```bash
cat > demo.sh <<'EOS'
for name in "$1" "$2"; do
    echo "hello $name"
done
echo "count=$#"
EOS
./myshell demo.sh alice bob
```

### 4.3 시스템 호출 추적

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

### 4.4 세션 녹화·재생

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
```

```text
shell> rec export lab.rec out.html
```

`rec export`는 서버 없이 열리는 단독 HTML 타임라인을 만듭니다. `rec start -x`를
사용하면 `explain` 도식과 `trace -c` 요약도 HTML에 포함됩니다. `-x`는 진단을
위해 명령을 한 번 더 실행하므로 읽기 전용 명령에만 사용합니다. 녹화 중 출력은
명령이 끝난 뒤 표시되며, `dbg` 같은 화면 제어형 프로그램은 녹화하지 않는 것을
권장합니다.

## 5. 소스 규모와 경제성

지침의 경제성 항목은 “1,000줄 이상, 10,000줄 이하를 원칙”으로 합니다.
현재 규모는 다음과 같습니다.

| 항목 | 줄 수 |
|---|---:|
| `include/myshell.h` + `src/*.c` | 9,672 |
| 빈 줄 | 1,120 |
| 주석 | 363 |
| 주석·빈 줄 제외 코드 | 8,189 |
| 생성된 `src/trace_syscalls.h` | 137 |
| 테스트 코드(별도) | 1,314 |

- 직접 작성 소스는 **9,672줄**, 생성 테이블을 포함해도 **9,809줄**로 10,000줄 이하입니다.
- 원 과제 1~5와 11개 내장 명령은 그대로 유지했습니다.
- 라인 에디터·스크립트·trace·explain·rec를 남기고, 부가 확장(네트워크/HMAC,
  `dbg`, `pipestat`, `serve`, `watch`, `undo`)은 범위에서 제외해 분량을 줄였습니다.
- 외부 라이브러리 없이 libc/POSIX만 사용합니다.

## 6. 프로젝트 구조

```text
.
├── Makefile
├── README.md
├── LICENSE
├── include/
│   └── myshell.h          공용 구조체와 함수 선언
├── src/
│   ├── main.c             REPL, 명령 분리, 시그널, 터미널 제어
│   ├── parser.c           토큰화, 따옴표/변수/$(...) 확장, 문법 분석
│   ├── execute.c          fork/exec, 파이프, 재지향, 명령 치환 캡처
│   ├── script.c           if/for/while, 함수, 스크립트 실행
│   ├── lineedit.c         raw 모드 라인 에디터, 히스토리, Tab, Ctrl-R
│   ├── cmd_trace.c        ptrace 시스템 호출 추적
│   ├── cmd_rec.c          세션 녹화/재생/HTML 타임라인
│   ├── cmd_explain.c      파서 결과 시각화
│   ├── trace_syscalls.h   x86_64 시스템 호출 이름 테이블 (생성 파일)
│   ├── jobs.c             백그라운드 작업 테이블
│   ├── builtin.c          내장 명령 디스패치
│   ├── cmd_ls.c           ls
│   ├── cmd_fs.c           mkdir, rmdir, ln, cp, rm, mv
│   ├── cmd_text.c         cat, grep
│   └── util.c             공용 유틸
├── tools/
│   └── gen_trace_names.py 시스템 호출 이름 테이블 생성기
├── tests/
│   ├── run_tests.sh       94개 기능 테스트
│   ├── test_file_input.sh 5개 파일 입력 테스트
│   ├── test_ext.sh        16개 explain/&&/glob 테스트
│   ├── test_script.sh     56개 스크립트 테스트
│   ├── test_trace.sh      32개 ptrace 추적 테스트
│   ├── test_rec.sh        35개 녹화/재생 테스트
│   ├── test_signals.py    PTY 시그널 테스트
│   └── test_lineedit.py   PTY 라인 에디터 테스트
└── docs/
    ├── REPORT.md / REPORT.docx / REPORT.hwp / REPORT.pdf
    ├── make_report.py
    ├── make_pdf.py
    └── make_hwp_text.py
```

## 7. 내장 명령 옵션

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
| `prompt` | `on`, `off` |
| `history` | `history [-c]` |
| `test` | `test EXPR`, `[ EXPR ]` |
| `read` | `read [-r] VAR...` |
| `shift` | `shift [N]` |
| `source` / `.` | `source FILE [ARGS...]` |
| `trace` | `-c`, `-f`, `-e NAME[,NAME...]`, `-e trace=NAME` |
| `rec` | `start [-x] FILE`, `stop`, `status`, `export FILE HTML` |
| `replay` | `replay FILE [-s SPEED]`, `-x` |

## 8. 제약

- 산술 확장 `$((...))`, `case`/`select`는 지원하지 않습니다.
- `break`/`continue`의 단계 인자(`break 2`)는 한 단계만 빠져나옵니다.
- 루프/`if` 전체를 파이프로 연결하는 `done | cmd`, `fi | cmd`는 지원하지 않습니다.
- `trace`는 x86_64 Linux 전용이며, 컨테이너 등에서 `ptrace`가 차단되면 동작하지 않습니다.
- `rec`는 전경 명령의 출력을 명령 종료 후 기록합니다. 화면 제어형 프로그램은
  녹화에 적합하지 않으며, `-x` 진단은 명령을 한 번 더 실행합니다.

## 9. 팀 역할과 주차별 기록

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어, 파서/실행기, 시그널, 라인 에디터, 스크립트, trace, rec, explain, `pwd`/`cd`/`ls`/`mkdir`/`rmdir` |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `ln`/`cp`/`rm`/`mv`/`cat`/`grep`, 테스트, 문서, PTY/시나리오 검증 |

| 주차 | 류동엽 | 강동우 | 증빙 |
|---|---|---|---|
| 11주 | simplesh 분석, 요구사항/파서·실행기 설계 | 파일/텍스트 명령 요구사항, 테스트 항목 | 요구사항 표, Makefile |
| 12주 | 토큰화·확장·파이프·재지향 | ls/mkdir/rmdir 초안, 테스트 | parser.c, execute.c |
| 13주 | 프로세스 그룹, tcsetpgrp, SIGINT/SIGQUIT | ln/cp/rm/mv 경계 조건 | PTY 시그널 테스트 |
| 14주 | 11개 필수 명령 통합, 94개 테스트 | cat/grep 옵션, 확장 테스트 | `Passed: 94` |
| 15주 | explain, 라인 에디터, 스크립트, trace, rec | 테스트/문서, PTY 시나리오 | 스크립트 56, 추적 32, 녹화 35 |

## 10. 보고서 재생성

```bash
python docs/make_report.py     # REPORT.md, REPORT.docx, REPORT.html
python docs/make_pdf.py        # REPORT.pdf (Chrome/Edge headless, 서식본)
python docs/make_hwp_text.py   # REPORT.hwp (Hancom, 편집 가능한 텍스트본)
```

## 11. GitHub 및 라이선스

- <https://github.com/gong000000/lab3>
- Collaborator: `pizza0420` (강동우), write 권한
- MIT License: [LICENSE](LICENSE)
