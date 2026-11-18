# myshell - Lab 3 팀 프로젝트

교재 357페이지의 `simplesh.c`를 확장한 POSIX 쉘입니다. Lab 3의 5개 요구사항과
11개 내장 명령을 구현했고, TCP 기반 원격 쉘(`netserv`, `netcli`)을 추가했습니다.

- 저장소: <https://github.com/gong000000/lab3>
- 라이선스: MIT

## 1. 구현 기능

| 구분 | 내용 |
|---|---|
| `exit` | `exit [status]`, `$?` |
| 백그라운드 | `&`, `jobs`, `wait`, `$!` |
| 시그널 | `SIGINT`(Ctrl-C), `SIGQUIT`(Ctrl-Z), 전경 작업만 종료 |
| 재지향/파이프 | `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, `&>`, `&>>`, 다단 `\|` |
| 내장 명령 | `ls`, `pwd`, `cd`, `mkdir`, `rmdir`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep` |
| 보조 내장 명령 | `echo`, `export`, `unset`, `jobs`, `wait`, `help`, `true`, `false`, `:` |
| 네트워크 확장 | `netserv`(TCP 서버), `netcli`(클라이언트), 토큰 인증, 동시 접속, 타임아웃 |
| 조건부 실행 | `&&`, `||` |
| glob | `*`, `?`, `[...]` (인용되지 않은 경우) |
| 파서 시각화 | `explain '명령줄'` |
| 휴지통/undo | `rm`, `mv` 덮어쓰기, `cp -f` 덮어쓰기를 `~/.myshell_trash`로 이동, `undo`로 복원 |
| HTTP 서버 | `serve -p 8080 -d DIR` |
| 주기 실행 | `watch -n 2 -c 3 '명령'` |
| 프롬프트 확장 | `prompt on/off` (종료 코드·실행 시간·작업 수) |

Ctrl-Z는 과제 명세에 맞춰 `SIGQUIT`에 매핑했습니다. 일반 리눅스의 `SIGTSTP`와
다르며, 쉘 종료 시 터미널 설정을 복구합니다.

## 2. 실행 방법

### 2.1 준비

Linux 또는 WSL(Ubuntu)에서 실행합니다. Windows에서는 PowerShell에서 `wsl`을
실행한 뒤 프로젝트 폴더로 이동합니다.

```bash
cd /mnt/c/Users/gong0000/Desktop/lab3
```

필요한 도구가 없으면 설치합니다.

```bash
sudo apt update
sudo apt install -y build-essential python3
```

`gcc`, `make`, `python3`가 설치되어 있는지 확인합니다.

```bash
gcc --version
make --version
python3 --version
```

### 2.2 빌드

```bash
make clean
make
```

빌드가 끝나면 현재 폴더에 `myshell` 실행 파일이 생깁니다.

```bash
ls -l myshell
```

### 2.3 실행

```bash
./myshell
```

프롬프트가 `shell> `로 바뀝니다. 종료할 때는 `exit`를 입력합니다.

```text
$ ./myshell
shell> pwd
/home/gong0000/lab3
shell> exit
```

`exit` 뒤의 숫자는 종료 상태입니다.

```bash
./myshell -c 'exit 7'
echo $?
```

### 2.4 시연 준비

스크린샷을 찍을 때는 먼저 깨끗한 작업 폴더를 만들고 코어 덤프를 끕니다.

```bash
mkdir -p ~/myshell_demo
cd ~/myshell_demo
ulimit -c 0
/mnt/c/Users/gong0000/Desktop/lab3/myshell
```

프로젝트 위치가 다르면 위 `myshell` 경로를 실제 경로로 바꿉니다.
시연 중에는 프롬프트가 `shell> `인지 확인합니다.

### 2.5 요구사항별 시연

아래 명령은 모두 `shell> ` 프롬프트에서 실행합니다. 스크린샷마다 `clear`로
화면을 지우고 한 가지 주제만 보이게 하면 읽기 쉽습니다.

#### (1) exit

```text
shell> echo before
before
shell> exit 7
$ echo $?
7
```

`exit` 이후의 명령은 실행되지 않습니다. 위 예시는 쉘을 빠져나온 뒤 bash에서
종료 상태를 확인하는 화면입니다.

#### (2) 백그라운드 실행 `&`

```text
shell> sleep 3 &
[1] 12345
shell> jobs
[1]  Running  sleep 3 &
shell> wait
shell> echo done
done
```

백그라운드 작업이 끝난 뒤 `jobs`를 실행하면 `Done`으로 표시됩니다.

```text
shell> sleep 0.2 &
[1] 12346
shell> sleep 0.5
shell> jobs
[1]  Done     sleep 0.2 &
```

#### (3) 인터럽트 키: Ctrl-C, Ctrl-Z

`sleep 30`을 실행한 뒤 실제 키를 누릅니다.

```text
shell> sleep 30
^C
shell> echo $?
130
```

```text
shell> sleep 30
^Z
Quit
shell> echo $?
131
```

과제 명세에 맞춰 이 쉘은 Ctrl-Z를 `SIGQUIT`에 매핑합니다. `ulimit -c 0`을
실행해 두면 core 파일이 생기지 않습니다. 두 경우 모두 쉘 자체는 종료되지 않고
프롬프트가 다시 나타나야 합니다.

#### (4) 재지향과 파이프

```text
shell> printf 'alpha\nbeta\ngamma\n' > lines.txt
shell> cat < lines.txt
alpha
beta
gamma
shell> echo delta >> lines.txt
shell> cat lines.txt
alpha
beta
gamma
delta
shell> cat lines.txt | grep -n beta
2:beta
shell> cat lines.txt | grep -v beta | wc -l
3
```

표준 에러 분리와 병합:

```text
shell> cat missing.txt 2> err.txt
shell> cat err.txt
myshell: missing.txt: No such file or directory
shell> ls missing.txt > all.txt 2>&1
shell> cat all.txt
myshell: missing.txt: No such file or directory
```

#### (5) 내장 명령

`pwd`, `cd`, `ls`:

```text
shell> pwd
/home/gong0000/myshell_demo
shell> ls -la
shell> mkdir -p demo/sub
shell> cd demo
shell> pwd
/home/gong0000/myshell_demo/demo
shell> cd ..
shell> rmdir demo/sub
shell> rmdir demo
```

`ln`:

```text
shell> echo "link test" > target.txt
shell> ln target.txt hard.txt
shell> ln -s target.txt sym.txt
shell> ls -l target.txt hard.txt sym.txt
-rw-r--r-- 2 gong0000 gong0000 10 ... hard.txt
lrwxrwxrwx 1 gong0000 gong0000  9 ... sym.txt -> target.txt
-rw-r--r-- 2 gong0000 gong0000 10 ... target.txt
shell> cat hard.txt
link test
shell> cat sym.txt
link test
```

`cp`, `mv`, `rm`:

```text
shell> mkdir -p copy_src/sub
shell> echo data > copy_src/sub/file.txt
shell> cp -r copy_src copy_dst
shell> cat copy_dst/sub/file.txt
data
shell> mv copy_dst moved_dir
shell> ls moved_dir
sub
shell> rm -r moved_dir copy_src
shell> ls
```

`cat`, `grep`:

```text
shell> printf 'apple\nbanana\napple pie\n' > fruits.txt
shell> cat fruits.txt
apple
banana
apple pie
shell> cat -n fruits.txt
     1	apple
     2	banana
     3	apple pie
shell> grep -n apple fruits.txt
1:apple
3:apple pie
shell> grep -iv apple fruits.txt
banana
shell> grep -c apple fruits.txt
2
shell> grep -r apple .
./fruits.txt:apple
./fruits.txt:apple pie
```

#### (6) 원격 쉘: netserv / netcli

한 번 접속만 받는 서버를 백그라운드로 실행하고, 클라이언트가 명령을 보냅니다.

```text
shell> netserv -1 -p 9090 &
[1] 12345
netserv: listening on 127.0.0.1:9090 (once)
shell> sleep 0.3
shell> netcli -p 9090 127.0.0.1 'cat lines.txt | grep -n beta'
2:beta
shell> wait
```

토큰 인증:

```text
shell> netserv -1 -p 9091 -s secret &
[1] 12346
netserv: listening on 127.0.0.1:9091 (auth) (once)
shell> sleep 0.3
shell> netcli -p 9091 -s secret 127.0.0.1 'pwd'
/home/gong0000/myshell_demo
shell> wait
```

토큰이 틀리면 서버가 인증 실패를 반환하고 연결을 닫습니다.

```text
shell> netserv -1 -p 9092 -s secret &
shell> sleep 0.3
shell> netcli -p 9092 -s wrong 127.0.0.1 'echo no'
netserv: authentication failed
shell> echo $?
147
shell> wait
```

대화형 클라이언트:

```text
shell> netserv -1 -p 9093 &
shell> sleep 0.3
shell> netcli -i -p 9093 127.0.0.1
netcli> pwd
/home/gong0000/myshell_demo
netcli> echo remote
remote
netcli> exit
shell> wait
```

동시 접속 확인:

```text
shell> netserv -n 2 -p 9094 &
shell> sleep 0.3
shell> netcli -p 9094 127.0.0.1 'echo client-A' &
shell> netcli -p 9094 127.0.0.1 'echo client-B' &
shell> wait
client-A
client-B
```

포트가 이미 사용 중이면 `-p` 뒤의 번호를 다른 값으로 바꿉니다.
위 예시의 `lines.txt`는 2.5절 (4)에서 만든 파일입니다. 새 셸에서 시작했다면
`printf 'alpha\nbeta\ngamma\n' > lines.txt`를 먼저 실행합니다.

#### (7) explain, `&&`/`||`, glob

```text
shell> explain 'cat missing | grep x > out 2>&1 &'
explain: 1 pipeline(s)

[pipeline 1] background (first)
  ├─ cmd: cat missing
  └─ cmd: grep x
       ├─ stdout → out (truncate)
       └─ stderr → stdout
shell> false && echo no || echo fallback
fallback
shell> printf 'a\n' > g1.txt
shell> printf 'b\n' > g2.txt
shell> echo g*.txt
g1.txt g2.txt
shell> echo "g*.txt"
g*.txt
```

#### (8) undo, serve, watch, prompt

```text
shell> echo keep > undo.txt
shell> rm undo.txt
shell> undo
undo: restored '/home/gong0000/myshell_demo/undo.txt'
shell> cat undo.txt
keep
```

```text
shell> serve -1 -p 8080 -d . &
[1] 12345
serve: http://127.0.0.1:8080/ (root: /home/gong0000/myshell_demo) (once)
shell> sleep 0.3
shell> curl -s http://127.0.0.1:8080/undo.txt
keep
shell> wait
```

```text
shell> watch -n 1 -c 2 echo hello
Every 1s: echo hello    (...)
hello
...
```

```text
shell> prompt on
prompt: on
shell [0|0.00s|0]> echo hi
hi
shell [0|0.00s|0]> prompt off
prompt: off
shell>
```

#### (9) 테스트 실행

```bash
make test
```

출력 마지막 부분에서 다음 항목을 확인할 수 있습니다.

```text
Passed: 94
Failed: 0
...
Network tests passed: 23
Network tests failed: 0
...
File-input tests passed: 4
File-input tests failed: 0
...
Extension tests passed: 32
Extension tests failed: 0
...
SIGNAL TEST PASSED
```

테스트 스크립트:

- `tests/run_tests.sh`: 94개 기능 테스트
- `tests/test_net.sh`: 23개 네트워크 테스트
- `tests/test_file_input.sh`: 4개 파일 입력 테스트
- `tests/test_ext.sh`: 32개 확장 테스트
- `tests/test_signals.py`: PTY 시그널/프롬프트 테스트

### 2.6 스크린샷 체크리스트

| 파일 이름 예시 | 장면 | 명령 |
|---|---|---|
| `01_exit.png` | exit와 종료 상태 | `echo before; exit 7; echo after` |
| `02_background.png` | 백그라운드와 jobs/wait | `sleep 3 &`, `jobs`, `wait` |
| `03_sigint.png` | Ctrl-C | `sleep 30` + Ctrl-C + `echo $?` |
| `04_sigquit.png` | Ctrl-Z | `sleep 30` + Ctrl-Z + `echo $?` |
| `05_redirection.png` | `>`, `>>`, `<`, `2>`, `2>&1` | 2.5절 (4) |
| `06_pipe.png` | 다단 파이프 | `cat lines.txt \| grep -n beta` |
| `07_pwd_cd_ls.png` | pwd/cd/ls | 2.5절 (5) 첫 블록 |
| `08_fileops.png` | ln/cp/mv/rm | 2.5절 (5) 중간 블록 |
| `09_cat_grep.png` | cat/grep | 2.5절 (5) 마지막 블록 |
| `10_netcli.png` | 원격 명령 | 2.5절 (6) 첫 블록 |
| `11_net_auth.png` | 토큰 인증 | 2.5절 (6) 두 번째 블록 |
| `12_net_interactive.png` | 대화형 netcli | 2.5절 (6) 대화형 블록 |
| `13_make_test.png` | 전체 테스트 결과 | `make test` |
| `14_explain.png` | 파서 시각화 | `explain 'cat a \| grep x > o 2>&1 &'` |
| `15_and_or_glob.png` | `&&`/`||`, glob | `false && echo no \|\| echo fallback` |
| `16_undo.png` | rm 후 undo | `echo keep > u.txt; rm u.txt; undo; cat u.txt` |
| `17_serve.png` | HTTP 서버 | `serve -1 -p 8080 -d . &` + `curl` |
| `18_watch_prompt.png` | watch와 확장 프롬프트 | `watch -n 1 -c 2 echo hi`, `prompt on` |

캡처 팁:

- 터미널 크기는 120×35 정도, 글꼴은 13~14pt로 맞춥니다.
- 각 장면 전에 `clear`를 실행하고, 프롬프트와 결과가 한 화면에 들어오게 합니다.
- Ctrl-C/Ctrl-Z는 터미널에서 실제 키를 눌러 `^C`, `^Z`가 보이게 합니다.
- `make test`는 출력이 길므로 마지막 요약 부분이 화면에 남도록 합니다.
- 캡처 파일은 `docs/screenshots/`에 번호순으로 저장하면 보고서에 넣기 쉽습니다.

### 2.7 자주 발생하는 문제

| 증상 | 해결 |
|---|---|
| `cc: command not found` | `sudo apt install build-essential` |
| `./myshell: Permission denied` | `chmod +x myshell` |
| `netserv: a token (-s) is required` | 외부 주소 대신 `127.0.0.1`을 쓰거나 `-s TOKEN` 추가 |
| `netcli: cannot connect` | 서버가 실행 중인지, 포트 번호가 같은지 확인 |
| Ctrl-Z가 동작하지 않음 | 실제 WSL 터미널에서 실행 중인지 확인하고 `ulimit -c 0` 설정 |
| `make test`가 중간에 멈춤 | 이전에 띄운 `netserv`가 포트를 잡고 있는지 확인 |

## 3. 프로젝트 구조

```text
.
├── Makefile
├── README.md
├── LICENSE
├── include/
│   └── myshell.h          공용 구조체와 함수 선언
├── src/
│   ├── main.c             REPL, 명령 목록 분리, 시그널, 터미널 제어
│   ├── parser.c           토큰화, 따옴표/변수 확장, 문법 분석
│   ├── execute.c          fork/exec, 파이프, 재지향, 대기
│   ├── jobs.c             백그라운드 작업 테이블
│   ├── builtin.c          내장 명령 디스패치, cd/pwd/echo/exit/jobs/wait
│   ├── cmd_ls.c           ls
│   ├── cmd_fs.c           mkdir, rmdir, ln, cp, rm, mv
│   ├── cmd_text.c         cat, grep
│   ├── cmd_net.c          netserv, netcli
│   ├── cmd_explain.c      파서 결과 시각화
│   ├── cmd_trash.c        휴지통과 undo
│   ├── cmd_serve.c        HTTP 파일 서버
│   ├── cmd_watch.c        주기 실행
│   └── util.c             공용 유틸
├── tests/
│   ├── run_tests.sh
│   ├── test_net.sh
│   ├── test_file_input.sh
│   ├── test_ext.sh
│   ├── test_signals.py
│   └── manual_test.sh
└── docs/
    ├── REPORT.md / REPORT.docx / REPORT.hwp / REPORT.pdf
    ├── REPORT.html
    ├── make_report.py
    ├── convert_hwp.py
    └── make_pdf.py
```

## 4. 내장 명령 옵션

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
| `undo` | 마지막 rm/mv/cp -f 작업 복원 |
| `serve` | `-p PORT`, `-d DIR`, `-a ADDR`, `-1` |
| `watch` | `-n SEC`, `-c COUNT` |
| `prompt` | `on`, `off` |

## 5. 제약

- 명령 치환(`$(...)`), `1>&2`, `fg`/`bg`는 지원하지 않습니다.
- 변수 확장 결과의 단어 분할은 하지 않습니다. `$VAR`는 한 인자로 유지됩니다.
- 원격 명령의 백그라운드 실행(`&`)은 거부합니다.
- `undo`는 마지막 한 건만 복원합니다. 휴지통은 `~/.myshell_trash`에 쌓입니다.
- 원격 명령은 명령마다 자식 프로세스에서 실행되므로 `cd`가 다음 명령에 유지되지 않습니다.
- netserv는 기본적으로 127.0.0.1에만 바인딩합니다. 외부 주소를 열려면 `-s TOKEN`이 필요합니다.
- serve는 기본적으로 127.0.0.1에 바인딩하며 별도 인증은 없습니다. 외부 공개는 피합니다.
- `grep`은 POSIX 기본/확장 정규표현식과 고정 문자열(`-F`)을 지원합니다.

## 6. 팀 역할

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어(파서/실행기/시그널/작업 제어/재지향/파이프), `explain`, `netserv`, `serve`, `pwd`, `cd`, `ls`, `mkdir`, `rmdir` |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `netcli`, `undo`/휴지통, `watch`, `prompt`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep`, 테스트/문서 |

## 7. 보고서 재생성

```bash
python docs/make_report.py   # REPORT.md, REPORT.docx, REPORT.html
python docs/convert_hwp.py   # REPORT.hwp (Windows + Hancom Office)
python docs/make_pdf.py      # REPORT.pdf (Chrome/Edge headless)
```

## 8. GitHub 및 라이선스

- <https://github.com/gong000000/lab3>
- Collaborator: `pizza0420` (강동우), write 권한 등록 완료
- MIT License: [LICENSE](LICENSE)
