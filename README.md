# myshell - Lab 3 팀 프로젝트

오픈소스프로젝트(전공설계) Lab 3 과제로 만든 간단한 POSIX 쉘입니다.
교재 357페이지의 `simplesh.c`(fork/exec/wait 기반 미니 쉘)를 출발점으로 하여,
과제에서 요구한 기능을 모두 추가했습니다.

- 저장소: <https://github.com/gong000000/lab3>
- 라이선스: MIT (자유 소프트웨어)

## 1. 구현 기능

| 요구사항 | 구현 내용 |
|---|---|
| 1. `exit` | `exit [status]`로 종료, 종료 상태 저장, `$?`로 확인 가능 |
| 2. 백그라운드 실행 | 명령 끝의 `&`, 파이프라인 전체를 백그라운드로 실행, `jobs`, `wait`, `$!` 지원 |
| 3. 인터럽트 키 | `SIGINT`(Ctrl-C), `SIGQUIT`(Ctrl-Z) 처리, 전경 작업만 종료되고 쉘은 계속 동작 |
| 4. 재지향/파이프 | `<`, `>`, `>>`, `2>`, `2>>`, `&>`, `&>>`, 다단 파이프(`|`) 지원 |
| 5. 내장 명령 | `ls`, `pwd`, `cd`, `mkdir`, `rmdir`, `ln`, `cp`, `rm`, `mv`, `cat`, `grep`을 C로 직접 구현 |

추가로 `echo`, `export`, `unset`, `jobs`, `wait`, `help`, `true`, `false`, `:`를
내장 명령으로 제공하며, 내장 명령이 아닌 명령은 `execvp()`로 외부 프로그램을 실행합니다.

### Ctrl-Z에 대한 안내

과제 명세에 `SIGQUIT: Ctrl-Z`라고 되어 있어, 터미널의 `VQUIT` 문자를 Ctrl-Z(0x1A)로
바꾸고 `VSUSP`를 비활성화했습니다. 따라서 이 쉘에서는 Ctrl-Z가 전경 작업에
`SIGQUIT`를 보냅니다. 일반적인 리눅스 쉘의 `SIGTSTP`(작업 일시정지)와는 다릅니다.
쉘 종료 시 원래 터미널 설정으로 복구합니다.

## 2. 빌드 및 실행

Linux 또는 WSL(Ubuntu)에서 실행합니다.

```bash
make
./myshell
```

프롬프트는 교재와 동일하게 `shell> `입니다.

```text
$ ./myshell
shell> pwd
/home/student/lab3
shell> ls -l
...
shell> echo hello | grep -n hello
1:hello
shell> sleep 3 &
[1] 12345
shell> jobs
[1]  Running  sleep 3 &
shell> exit
```

### 한 줄 실행

```bash
./myshell -c 'echo hello > out.txt; cat out.txt | grep hello'
```

### 테스트

```bash
make test
python3 tests/test_signals.py
```

- `tests/run_tests.sh`: 92개의 기능 테스트 (종료, 확장, 재지향, 파이프, 백그라운드,
  내장 명령, 오류 처리)
- `tests/test_signals.py`: PTY에서 Ctrl-C/Ctrl-Z를 보내 쉘이 살아남는지 확인하는 테스트

## 3. 프로젝트 구조

```text
.
├── Makefile
├── README.md
├── LICENSE
├── include/
│   └── myshell.h          공용 구조체와 함수 선언
├── src/
│   ├── main.c             REPL, 시그널, 터미널/작업 제어 초기화
│   ├── parser.c           토큰화, 따옴표/변수/틸드 확장, 문법 분석
│   ├── execute.c          fork/exec, 파이프, 재지향, 대기
│   ├── jobs.c             백그라운드 작업 테이블
│   ├── builtin.c          내장 명령 디스패치, cd/pwd/echo/exit/jobs/wait 등
│   ├── cmd_ls.c           ls
│   ├── cmd_fs.c           mkdir, rmdir, ln, cp, rm, mv
│   ├── cmd_text.c         cat, grep
│   └── util.c             메모리, 경로, 파일 복사/삭제, 모드 출력 유틸
├── tests/
│   ├── run_tests.sh
│   ├── test_signals.py
│   └── manual_test.sh
└── docs/
    ├── REPORT.md / REPORT.docx / REPORT.hwp / REPORT.pdf
    ├── REPORT.html          PDF/HWP 변환용 중간 문서
    ├── make_report.py       MD/DOCX/HTML 보고서 생성
    ├── convert_hwp.py       Hancom Office로 HWP 생성
    └── make_pdf.py          Chrome/Edge로 PDF 생성
```

## 4. 내장 명령과 옵션

| 명령 | 지원 옵션 |
|---|---|
| `ls` | `-a`, `-l`, `-R`, `-1`, `-h` |
| `pwd` | `-L`, `-P` (동작은 동일) |
| `cd` | `cd`, `cd DIR`, `cd -` |
| `mkdir` | `-p`, `-m MODE`, `-v` |
| `rmdir` | `-p`, `-v` |
| `ln` | `-s`, `-f`, `-v` |
| `cp` | `-r`/`-R`, `-f`, `-i`, `-p`, `-v` |
| `rm` | `-r`/`-R`, `-f`, `-i`, `-v` |
| `mv` | `-f`, `-i`, `-n`, `-v` |
| `cat` | `-n`, `-b`, `-E`, `-T`, `-A` |
| `grep` | `-i`, `-v`, `-n`, `-c`, `-l`, `-r`/`-R`, `-E`, `-F`, `-q`, `-s`, `-h`, `-H`, `-e` |

## 5. 설계상의 제약

- POSIX/Linux 시스템 콜과 C 라이브러리만 사용합니다.
- glob(`*`, `?`) 확장, 명령 치환(`$(...)`), `&&`/`||`, `2>&1`, `fg`/`bg`는
  구현 범위에서 제외했습니다. (과제 필수 기능은 모두 지원)
- 변수 확장 결과의 단어 분할(word splitting)은 하지 않습니다. `$VAR`는 하나의
  인자로 유지됩니다.
- `grep`은 POSIX 기본/확장 정규표현식과 고정 문자열(`-F`)을 지원합니다.
- `ls`는 한 줄에 하나씩 출력하며, 컬러 출력은 지원하지 않습니다.

## 6. 팀 역할

| 팀원 | 학번 | GitHub | 담당 |
|---|---|---|---|
| 류동엽 | 20223154 | [@gong000000](https://github.com/gong000000) | 쉘 코어(파서, 실행기, 시그널/작업 제어, 재지향/파이프), `pwd`, `cd`, `ls`, `mkdir`, `rmdir`, 통합 |
| 강동우 | 20223080 | [@pizza0420](https://github.com/pizza0420) | `ln`, `cp`, `rm`, `mv`, `cat`, `grep`, 테스트/문서, 교차 파일시스템 `mv` 검증 |

- 명령 구현은 5개(`pwd`, `cd`, `ls`, `mkdir`, `rmdir`)와 6개(`ln`, `cp`, `rm`, `mv`, `cat`, `grep`)로 나누었습니다.
- 류동엽은 쉘 코어와 통합을, 강동우는 파일/텍스트 명령과 자동화 테스트를 담당해 전체 작업량을 균형 있게 분담했습니다.
- 기능별 파일 분리와 자동 테스트로 변경 사항을 관리했습니다.

## 7. 보고서 재생성

```bash
python docs/make_report.py   # REPORT.md, REPORT.docx, REPORT.html
python docs/convert_hwp.py   # REPORT.hwp (Windows + Hancom Office)
python docs/make_pdf.py      # REPORT.pdf (Chrome/Edge headless)
```

## 8. GitHub 저장소 및 라이선스

- 저장소: <https://github.com/gong000000/lab3>
- 협업자: `pizza0420` (강동우) — Collaborator(write) 초대 완료, 수락 대기
- 라이선스: MIT License. 자세한 내용은 [LICENSE](LICENSE)를 참고하세요.
