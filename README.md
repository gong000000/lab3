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

Ctrl-Z는 과제 명세에 맞춰 `SIGQUIT`에 매핑했습니다. 일반 리눅스의 `SIGTSTP`와
다르며, 쉘 종료 시 터미널 설정을 복구합니다.

## 2. 빌드 및 실행

```bash
make
./myshell
```

```text
shell> pwd
/home/student/lab3
shell> echo hello > out.txt
shell> cat out.txt | grep -n hello
1:hello
shell> sleep 3 &
[1] 12345
shell> jobs
[1]  Running  sleep 3 &
shell> exit
```

한 줄 실행:

```bash
./myshell -c 'echo hello > out.txt; cat out.txt | grep hello'
```

### 원격 쉘

서버를 백그라운드로 실행하고 클라이언트에서 명령을 보냅니다.

```bash
shell> netserv -1 -p 9090 &
[1] 12345
shell> netcli -p 9090 127.0.0.1 'cat out.txt | grep hello'
hello
```

대화형 클라이언트:

```bash
shell> netcli -i -p 9090 127.0.0.1
netcli> pwd
/home/student/lab3
netcli> exit
```

서버 옵션:

| 옵션 | 설명 |
|---|---|
| `-p PORT` | 포트 (기본 9090) |
| `-a ADDR` | 바인딩 주소 (기본 127.0.0.1) |
| `-s TOKEN` | 공유 토큰. 비루프백 주소는 토큰 필수 |
| `-t SEC` | 명령 타임아웃 (기본 10초) |
| `-n COUNT` | 접속 수 제한 후 종료 |
| `-1` | 한 접속만 처리하고 종료 |

클라이언트 옵션은 `-p`, `-s`, `-t`, `-i`를 지원합니다. `netcli HOST PORT COMMAND`처럼
포트를 두 번째 인자로 줄 수도 있습니다.

## 3. 테스트

```bash
make test
```

- `tests/run_tests.sh`: 94개 기능 테스트
- `tests/test_net.sh`: 23개 네트워크 테스트 (인증, 원격 파이프/재지향, 동시 접속, 타임아웃)
- `tests/test_file_input.sh`: 4개 파일 입력 테스트 (마지막 줄 중복 방지)
- `tests/test_signals.py`: PTY에서 Ctrl-C/Ctrl-Z 6개 시나리오

## 4. 프로젝트 구조

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
│   └── util.c             공용 유틸
├── tests/
│   ├── run_tests.sh
│   ├── test_net.sh
│   ├── test_file_input.sh
│   ├── test_signals.py
│   └── manual_test.sh
└── docs/
    ├── REPORT.md / REPORT.docx / REPORT.hwp / REPORT.pdf
    ├── REPORT.html
    ├── make_report.py
    ├── convert_hwp.py
    └── make_pdf.py
```

## 5. 내장 명령 옵션

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

## 6. 제약

- glob(`*`, `?`), 명령 치환(`$(...)`), `&&`/`||`, `1>&2`, `fg`/`bg`는 지원하지 않습니다.
- 변수 확장 결과의 단어 분할은 하지 않습니다. `$VAR`는 한 인자로 유지됩니다.
- 원격 명령의 백그라운드 실행(`&`)은 거부합니다.
- 원격 명령은 명령마다 자식 프로세스에서 실행되므로 `cd`가 다음 명령에 유지되지 않습니다.
- 서버는 기본적으로 127.0.0.1에만 바인딩합니다. 외부 주소를 열려면 `-s TOKEN`이 필요합니다.
- `grep`은 POSIX 기본/확장 정규표현식과 고정 문자열(`-F`)을 지원합니다.

## 7. 팀 역할

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어(파서/실행기/시그널/작업 제어/재지향/파이프), `netserv`, `pwd`, `cd`, `ls`, `mkdir`, `rmdir` |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `netcli`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep`, 네트워크/기능 테스트, 문서 |

## 8. 보고서 재생성

```bash
python docs/make_report.py   # REPORT.md, REPORT.docx, REPORT.html
python docs/convert_hwp.py   # REPORT.hwp (Windows + Hancom Office)
python docs/make_pdf.py      # REPORT.pdf (Chrome/Edge headless)
```

## 9. GitHub 및 라이선스

- <https://github.com/gong000000/lab3>
- Collaborator: `pizza0420` (강동우), write 권한 초대 완료 · 수락 대기
- MIT License: [LICENSE](LICENSE)
