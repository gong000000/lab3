/*
 * cmd_dbg.c - x86_64 ELF/ptrace debugger.
 */
#include "myshell.h"

#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef ADDR_NO_RANDOMIZE
#define ADDR_NO_RANDOMIZE 0x0040000
#endif

#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

#define DBG_MAX_BREAKPOINTS 64
#define DBG_MAX_DEPTH 32

typedef struct {
    char *name;
    unsigned long value;
    unsigned long size;
} dbg_symbol_t;

typedef struct {
    int used;
    int inserted;
    int temporary;
    int id;
    unsigned long addr;
    unsigned long sym_value;
    int has_symbol;
    char *name;
    long orig_word;
    unsigned char orig_byte;
} dbg_bp_t;

typedef struct {
    char *path;
    char *realpath;
    unsigned char *image;
    size_t image_size;
    int is_pie;
    unsigned long bias;
    dbg_symbol_t *symbols;
    int nsymbols;
    int symbol_cap;

    pid_t pid;
    int started;
    int stopped;
    int exited;
    int exit_status;
    int stop_signal;
    dbg_bp_t breakpoints[DBG_MAX_BREAKPOINTS];
    int next_bp_id;
    dbg_bp_t *pending_bp;
} dbg_t;

/* Utilities                                                          */

static int read_binary_file(const char *path, unsigned char **data,
                            size_t *size_out)
{
    int fd;
    struct stat st;
    unsigned char *buffer;
    size_t done = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    buffer = msh_malloc((size_t)st.st_size);
    while (done < (size_t)st.st_size) {
        ssize_t n = read(fd, buffer + done, (size_t)st.st_size - done);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            free(buffer);
            close(fd);
            errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    close(fd);
    *data = buffer;
    *size_out = (size_t)st.st_size;
    return 0;
}

static long peek_word(pid_t pid, unsigned long addr)
{
    long word;

    errno = 0;
    word = ptrace(PTRACE_PEEKDATA, pid, (void *)addr, NULL);
    return errno == 0 ? word : 0;
}

static int poke_word(pid_t pid, unsigned long addr, long word)
{
    return ptrace(PTRACE_POKEDATA, pid, (void *)addr, (void *)word) == 0 ? 0
                                                                         : -1;
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGBUS: return "SIGBUS";
    case SIGINT: return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
    }
}

/* ELF                                                                */

static void dbg_add_symbol(dbg_t *d, const char *name, unsigned long value,
                           unsigned long size)
{
    if (d->nsymbols == d->symbol_cap) {
        d->symbol_cap = d->symbol_cap ? d->symbol_cap * 2 : 128;
        d->symbols = msh_realloc(d->symbols,
                                 (size_t)d->symbol_cap * sizeof(*d->symbols));
    }
    d->symbols[d->nsymbols].name = msh_strdup(name);
    d->symbols[d->nsymbols].value = value;
    d->symbols[d->nsymbols].size = size;
    ++d->nsymbols;
}

static int symbol_cmp(const void *left, const void *right)
{
    const dbg_symbol_t *a = left;
    const dbg_symbol_t *b = right;

    if (a->value < b->value)
        return -1;
    if (a->value > b->value)
        return 1;
    return strcmp(a->name, b->name);
}

static void dbg_load_symbols(dbg_t *d, Elf64_Shdr *sections, int count,
                             const char *shstr, int prefer_dynsym)
{
    int i;

    for (i = 0; i < count; ++i) {
        Elf64_Shdr *sh = &sections[i];
        int want = prefer_dynsym ? SHT_DYNSYM : SHT_SYMTAB;

        if (sh->sh_type != (uint32_t)want || sh->sh_link >= (uint32_t)count)
            continue;
        {
            Elf64_Sym *syms = (Elf64_Sym *)(d->image + sh->sh_offset);
            size_t nsyms = sh->sh_size / sizeof(Elf64_Sym);
            const char *strtab = (const char *)(d->image +
                                               sections[sh->sh_link].sh_offset);
            size_t j;

            (void)shstr;
            for (j = 0; j < nsyms; ++j) {
                unsigned type = ELF64_ST_TYPE(syms[j].st_info);
                const char *name;

                if (syms[j].st_shndx == SHN_UNDEF || syms[j].st_name == 0)
                    continue;
                if (type != STT_FUNC && type != STT_GNU_IFUNC)
                    continue;
                name = strtab + syms[j].st_name;
                if (name[0] == '\0')
                    continue;
                dbg_add_symbol(d, name, (unsigned long)syms[j].st_value,
                               (unsigned long)syms[j].st_size);
            }
        }
    }
}

static int dbg_load_elf(dbg_t *d, const char *path)
{
    Elf64_Ehdr *eh;
    Elf64_Shdr *sections;

    if (read_binary_file(path, &d->image, &d->image_size) < 0)
        return -1;
    if (d->image_size < sizeof(Elf64_Ehdr) ||
        memcmp(d->image, ELFMAG, SELFMAG) != 0) {
        errno = ENOEXEC;
        return -1;
    }
    eh = (Elf64_Ehdr *)d->image;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_machine != EM_X86_64 ||
        (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) ||
        eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr)) {
        errno = ENOEXEC;
        return -1;
    }
    if (eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf64_Shdr) >
        d->image_size) {
        errno = ENOEXEC;
        return -1;
    }

    d->is_pie = eh->e_type == ET_DYN;
    sections = (Elf64_Shdr *)(d->image + eh->e_shoff);

    dbg_load_symbols(d, sections, eh->e_shnum, NULL, 0);
    if (d->nsymbols == 0)
        dbg_load_symbols(d, sections, eh->e_shnum, NULL, 1);
    qsort(d->symbols, (size_t)d->nsymbols, sizeof(*d->symbols), symbol_cmp);
    return 0;
}

static dbg_symbol_t *dbg_find_symbol(dbg_t *d, const char *name)
{
    int i;

    for (i = 0; i < d->nsymbols; ++i) {
        if (d->symbols[i].name[0] != '\0' &&
            strcmp(d->symbols[i].name, name) == 0)
            return &d->symbols[i];
    }
    return NULL;
}

static dbg_symbol_t *dbg_symbol_at(dbg_t *d, unsigned long addr)
{
    dbg_symbol_t *best = NULL;
    int i;

    for (i = 0; i < d->nsymbols; ++i) {
        dbg_symbol_t *sym = &d->symbols[i];
        unsigned long start = sym->value + d->bias;

        if (sym->name[0] == '\0' || start > addr)
            continue;
        if (sym->size > 0) {
            if (addr >= start + sym->size)
                continue;
        } else if (addr != start) {
            continue;
        }
        if (best == NULL ||
            sym->value + d->bias > best->value + d->bias)
            best = sym;
    }
    return best;
}

static unsigned long dbg_symbol_address(dbg_t *d, const char *name)
{
    dbg_symbol_t *sym = dbg_find_symbol(d, name);

    return sym != NULL ? sym->value + d->bias : 0;
}

/* Process control                                                    */

static int dbg_get_regs(dbg_t *d, struct user_regs_struct *regs)
{
    if (d->pid <= 0)
        return -1;
    memset(regs, 0, sizeof(*regs));
    return ptrace(PTRACE_GETREGS, d->pid, NULL, regs) == 0 ? 0 : -1;
}

static int dbg_set_regs(dbg_t *d, struct user_regs_struct *regs)
{
    return ptrace(PTRACE_SETREGS, d->pid, NULL, regs) == 0 ? 0 : -1;
}

static unsigned long dbg_load_bias(dbg_t *d)
{
    char maps[128];
    char line[PATH_MAX + 128];
    FILE *fp;

    if (d->pid <= 0)
        return 0;
    snprintf(maps, sizeof(maps), "/proc/%d/maps", (int)d->pid);
    fp = fopen(maps, "r");
    if (fp == NULL)
        return 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long start;
        unsigned long end;
        unsigned long offset;
        char perms[8];
        char path[PATH_MAX];

        path[0] = '\0';
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %[^\n]", &start, &end,
                   perms, &offset, path) != 5)
            continue;
        {
            char *p = path;
            size_t len;

            while (*p == ' ' || *p == '\t')
                ++p;
            len = strlen(p);
            if (len > 10 && strcmp(p + len - 10, " (deleted)") == 0)
                p[len - 10] = '\0';
            if (strcmp(p, d->realpath) != 0 &&
                strcmp(p, d->path) != 0)
                continue;
        }
        if (d->is_pie)
            d->bias = start - offset;
        else
            d->bias = 0;
        break;
    }
    fclose(fp);
    return d->bias;
}

static void dbg_resolve_breakpoints(dbg_t *d)
{
    int i;

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        dbg_bp_t *bp = &d->breakpoints[i];

        if (!bp->used || !bp->has_symbol)
            continue;
        bp->addr = bp->sym_value + d->bias;
    }
}

static int dbg_bp_insert(dbg_t *d, dbg_bp_t *bp)
{
    long word;

    if (d->pid <= 0 || bp->inserted)
        return 0;
    errno = 0;
    word = ptrace(PTRACE_PEEKDATA, d->pid, (void *)bp->addr, NULL);
    if (errno != 0) {
        msh_error("dbg: cannot read address 0x%lx", bp->addr);
        return -1;
    }
    bp->orig_word = word;
    bp->orig_byte = (unsigned char)(word & 0xff);
    if (poke_word(d->pid, bp->addr,
                  (word & ~0xffL) | 0xccL) < 0) {
        msh_error("dbg: cannot set breakpoint at 0x%lx", bp->addr);
        return -1;
    }
    bp->inserted = 1;
    return 0;
}

static int dbg_bp_restore(dbg_t *d, dbg_bp_t *bp)
{
    if (d->pid <= 0 || !bp->inserted)
        return 0;
    if (poke_word(d->pid, bp->addr, bp->orig_word) < 0) {
        msh_error("dbg: cannot restore address 0x%lx", bp->addr);
        return -1;
    }
    bp->inserted = 0;
    return 0;
}

static int dbg_bp_insert_all(dbg_t *d)
{
    int i;

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (d->breakpoints[i].used && !d->breakpoints[i].temporary &&
            dbg_bp_insert(d, &d->breakpoints[i]) < 0)
            return -1;
    }
    return 0;
}

static void dbg_print_location(dbg_t *d, unsigned long rip)
{
    dbg_symbol_t *sym = dbg_symbol_at(d, rip);

    if (sym != NULL && sym->name[0] != '\0')
        printf("0x%lx in %s ()", rip, sym->name);
    else
        printf("0x%lx", rip);
}

static int dbg_bt(dbg_t *d)
{
    struct user_regs_struct regs;
    unsigned long rip;
    unsigned long rbp;
    unsigned long rsp;
    int depth = 0;

    if (dbg_get_regs(d, &regs) < 0) {
        msh_error("dbg: no inferior");
        return -1;
    }
    rip = (unsigned long)regs.rip;
    rbp = (unsigned long)regs.rbp;
    rsp = (unsigned long)regs.rsp;

    printf("#0  ");
    dbg_print_location(d, rip);
    printf("  (rip=0x%lx)\n", rip);

    /* At a function's first instruction the frame pointer still belongs to
     * the caller, so the immediate return address is at [rsp]. */
    {
        dbg_symbol_t *sym = dbg_symbol_at(d, rip);

        if (sym != NULL && rip == sym->value + d->bias && rbp != 0) {
            unsigned long ret = (unsigned long)peek_word(d->pid, rsp);

            if (ret >= 0x10000UL) {
                ++depth;
                printf("#%d  ", depth);
                dbg_print_location(d, ret);
                printf("  (ret=0x%lx)\n", ret);
            }
        }
    }

    while (rbp != 0 && depth < DBG_MAX_DEPTH) {
        unsigned long next = (unsigned long)peek_word(d->pid, rbp);
        unsigned long ret = (unsigned long)peek_word(d->pid, rbp + 8);

        if (ret < 0x10000UL)
            break;
        ++depth;
        printf("#%d  ", depth);
        dbg_print_location(d, ret);
        printf("  (ret=0x%lx)\n", ret);
        if (next <= rbp)
            break;
        rbp = next;
    }
    return 0;
}

static int dbg_print_regs(dbg_t *d)
{
    struct user_regs_struct r;

    if (dbg_get_regs(d, &r) < 0) {
        msh_error("dbg: no inferior");
        return -1;
    }
    printf("rip=0x%llx rsp=0x%llx rbp=0x%llx\n",
           (unsigned long long)r.rip, (unsigned long long)r.rsp,
           (unsigned long long)r.rbp);
    printf("rax=0x%llx rbx=0x%llx rcx=0x%llx rdx=0x%llx\n",
           (unsigned long long)r.rax, (unsigned long long)r.rbx,
           (unsigned long long)r.rcx, (unsigned long long)r.rdx);
    printf("rsi=0x%llx rdi=0x%llx r8=0x%llx r9=0x%llx\n",
           (unsigned long long)r.rsi, (unsigned long long)r.rdi,
           (unsigned long long)r.r8, (unsigned long long)r.r9);
    printf("r10=0x%llx r11=0x%llx r12=0x%llx r13=0x%llx\n",
           (unsigned long long)r.r10, (unsigned long long)r.r11,
           (unsigned long long)r.r12, (unsigned long long)r.r13);
    printf("r14=0x%llx r15=0x%llx eflags=0x%llx\n",
           (unsigned long long)r.r14, (unsigned long long)r.r15,
           (unsigned long long)r.eflags);
    return 0;
}

static int dbg_reg_value(dbg_t *d, const char *name, unsigned long *value)
{
    struct user_regs_struct r;
    const char *n = name;

    if (dbg_get_regs(d, &r) < 0)
        return -1;
    if (*n == '$')
        ++n;
    if (strcmp(n, "rip") == 0) *value = (unsigned long)r.rip;
    else if (strcmp(n, "rsp") == 0) *value = (unsigned long)r.rsp;
    else if (strcmp(n, "rbp") == 0) *value = (unsigned long)r.rbp;
    else if (strcmp(n, "rax") == 0) *value = (unsigned long)r.rax;
    else if (strcmp(n, "rbx") == 0) *value = (unsigned long)r.rbx;
    else if (strcmp(n, "rcx") == 0) *value = (unsigned long)r.rcx;
    else if (strcmp(n, "rdx") == 0) *value = (unsigned long)r.rdx;
    else if (strcmp(n, "rsi") == 0) *value = (unsigned long)r.rsi;
    else if (strcmp(n, "rdi") == 0) *value = (unsigned long)r.rdi;
    else if (strcmp(n, "r8") == 0) *value = (unsigned long)r.r8;
    else if (strcmp(n, "r9") == 0) *value = (unsigned long)r.r9;
    else if (strcmp(n, "r10") == 0) *value = (unsigned long)r.r10;
    else if (strcmp(n, "r11") == 0) *value = (unsigned long)r.r11;
    else if (strcmp(n, "r12") == 0) *value = (unsigned long)r.r12;
    else if (strcmp(n, "r13") == 0) *value = (unsigned long)r.r13;
    else if (strcmp(n, "r14") == 0) *value = (unsigned long)r.r14;
    else if (strcmp(n, "r15") == 0) *value = (unsigned long)r.r15;
    else return -1;
    return 0;
}

static int dbg_eval_addr(dbg_t *d, const char *expr, unsigned long *out)
{
    char *copy;
    char *p;
    char *end;
    unsigned long base = 0;

    while (*expr == ' ' || *expr == '\t')
        ++expr;
    if (*expr == '\0')
        return -1;
    if (*expr == '$')
        return dbg_reg_value(d, expr, out);
    if (isdigit((unsigned char)*expr)) {
        *out = strtoul(expr, &end, 0);
        return *end == '\0' ? 0 : -1;
    }

    copy = msh_strdup(expr);
    p = copy;
    while (*p != '\0' && *p != '+' && *p != '-' && !isspace((unsigned char)*p))
        ++p;
    {
        char saved = *p;
        unsigned long addr = 0;

        *p = '\0';
        addr = dbg_symbol_address(d, copy);
        if (addr == 0) {
            free(copy);
            return -1;
        }
        *out = addr;
        if (saved == '+' || saved == '-') {
            long offset = strtol(p + 1, &end, 0);

            *out = saved == '+' ? addr + (unsigned long)offset
                               : addr - (unsigned long)offset;
        }
        base = *out;
    }
    free(copy);
    (void)base;
    return 0;
}

static int dbg_dump_memory(dbg_t *d, const char *args)
{
    int count = 4;
    const char *p = args;
    unsigned long addr;
    int i;

    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p == '/') {
        char *end;

        ++p;
        count = (int)strtol(p, &end, 10);
        if (end == p)
            count = 4;
        p = end;
        while (*p == 'x' || *p == 'X' || *p == 'g' || *p == 'G')
            ++p;
        while (*p == ' ' || *p == '\t')
            ++p;
    }
    if (count <= 0)
        count = 4;
    if (count > 64)
        count = 64;
    if (*p == '\0' || dbg_eval_addr(d, p, &addr) < 0) {
        msh_error("dbg: x requires an address or $register");
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (i % 4 == 0) {
            if (i > 0)
                putchar('\n');
            printf("0x%lx:", addr + (unsigned long)i * 8);
        }
        printf(" 0x%016lx", (unsigned long)peek_word(d->pid,
                                                     addr + (unsigned long)i * 8));
    }
    putchar('\n');
    return 0;
}

static dbg_bp_t *dbg_find_bp_at(dbg_t *d, unsigned long addr)
{
    int i;

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (d->breakpoints[i].used && d->breakpoints[i].inserted &&
            d->breakpoints[i].addr == addr)
            return &d->breakpoints[i];
    }
    return NULL;
}

static int dbg_add_breakpoint(dbg_t *d, const char *spec)
{
    dbg_bp_t *bp = NULL;
    char *name = NULL;
    unsigned long addr = 0;
    unsigned long sym_value = 0;
    int has_symbol = 0;
    int i;

    while (*spec == ' ' || *spec == '\t')
        ++spec;
    if (*spec == '\0') {
        msh_usage("break SYMBOL | *ADDR");
        return -1;
    }
    if (*spec == '*') {
        if (dbg_eval_addr(d, spec + 1, &addr) < 0) {
            msh_error("dbg: bad address: %s", spec + 1);
            return -1;
        }
    } else {
        dbg_symbol_t *sym;

        name = msh_strdup(spec);
        {
            char *p = name;

            while (*p != '\0' && !isspace((unsigned char)*p))
                ++p;
            *p = '\0';
        }
        sym = dbg_find_symbol(d, name);
        if (sym == NULL) {
            msh_error("dbg: unknown symbol: %s", name);
            free(name);
            return -1;
        }
        sym_value = sym->value;
        addr = sym_value + d->bias;
        has_symbol = 1;
    }

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (d->breakpoints[i].used && !d->breakpoints[i].temporary &&
            d->breakpoints[i].addr == addr) {
            msh_error("dbg: breakpoint already exists at 0x%lx", addr);
            free(name);
            return -1;
        }
    }
    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (!d->breakpoints[i].used) {
            bp = &d->breakpoints[i];
            break;
        }
    }
    if (bp == NULL) {
        msh_error("dbg: too many breakpoints");
        free(name);
        return -1;
    }
    memset(bp, 0, sizeof(*bp));
    bp->used = 1;
    bp->addr = addr;
    bp->sym_value = sym_value;
    bp->has_symbol = has_symbol;
    bp->name = has_symbol ? name : NULL;
    bp->id = ++d->next_bp_id;

    if (d->pid > 0 && !d->exited && dbg_bp_insert(d, bp) < 0) {
        free(bp->name);
        memset(bp, 0, sizeof(*bp));
        return -1;
    }
    if (has_symbol)
        printf("Breakpoint %d at 0x%lx (%s)\n", bp->id, addr, name);
    else
        printf("Breakpoint %d at 0x%lx\n", bp->id, addr);
    return 0;
}

static void dbg_info_breakpoints(dbg_t *d)
{
    int i;
    int any = 0;

    printf("Num  Type        Address            What\n");
    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        dbg_bp_t *bp = &d->breakpoints[i];

        if (!bp->used)
            continue;
        any = 1;
        printf("%-4d %-11s 0x%-16lx %s%s\n", bp->id,
               bp->temporary ? "tbreak" : "breakpoint", bp->addr,
               bp->name != NULL ? bp->name : "*",
               bp->inserted ? " (inserted)" : "");
    }
    if (!any)
        printf("No breakpoints.\n");
}

static int dbg_delete_breakpoint(dbg_t *d, int id)
{
    int i;

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        dbg_bp_t *bp = &d->breakpoints[i];

        if (!bp->used || bp->id != id)
            continue;
        if (d->pending_bp == bp) {
            d->pending_bp = NULL;
        } else {
            (void)dbg_bp_restore(d, bp);
        }
        free(bp->name);
        memset(bp, 0, sizeof(*bp));
        return 0;
    }
    msh_error("dbg: no breakpoint %d", id);
    return -1;
}

static void dbg_remove_temporary(dbg_t *d)
{
    int i;

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        dbg_bp_t *bp = &d->breakpoints[i];

        if (bp->used && bp->temporary) {
            (void)dbg_bp_restore(d, bp);
            free(bp->name);
            memset(bp, 0, sizeof(*bp));
        }
    }
}

static int dbg_handle_stop(dbg_t *d, int status, int from_step)
{
    int sig;

    (void)from_step;
    if (WIFEXITED(status)) {
        d->exited = 1;
        d->stopped = 0;
        d->pid = 0;
        d->exit_status = WEXITSTATUS(status);
        printf("[Inferior exited with status %d]\n", d->exit_status);
        return 0;
    }
    if (WIFSIGNALED(status)) {
        d->exited = 1;
        d->stopped = 0;
        d->pid = 0;
        d->exit_status = 128 + WTERMSIG(status);
        printf("[Inferior terminated by signal %d (%s)]\n", WTERMSIG(status),
               signal_name(WTERMSIG(status)));
        return 0;
    }
    if (!WIFSTOPPED(status))
        return 0;

    sig = WSTOPSIG(status);
    d->stopped = 1;
    d->stop_signal = 0;

    if (sig == SIGTRAP) {
        struct user_regs_struct regs;
        unsigned long rip;

        if (dbg_get_regs(d, &regs) < 0)
            return -1;
        rip = (unsigned long)regs.rip;
        {
            dbg_bp_t *bp = dbg_find_bp_at(d, rip - 1);

            if (bp != NULL && !bp->temporary) {
                (void)dbg_bp_restore(d, bp);
                regs.rip = (unsigned long long)(rip - 1);
                (void)dbg_set_regs(d, &regs);
                d->pending_bp = bp;
                printf("Breakpoint %d, ", bp->id);
                dbg_print_location(d, rip - 1);
                putchar('\n');
                return 0;
            }
            if (bp != NULL && bp->temporary) {
                (void)dbg_bp_restore(d, bp);
                regs.rip = (unsigned long long)(rip - 1);
                (void)dbg_set_regs(d, &regs);
                printf("Breakpoint %d, ", bp->id);
                dbg_print_location(d, rip - 1);
                putchar('\n');
                dbg_remove_temporary(d);
                d->pending_bp = NULL;
                return 0;
            }
        }
        if (from_step != 2) {
            dbg_symbol_t *sym = dbg_symbol_at(d, rip);

            printf("Stopped at 0x%lx", rip);
            if (sym != NULL && sym->name[0] != '\0')
                printf(" in %s ()", sym->name);
            putchar('\n');
        }
        return 0;
    }

    d->stop_signal = sig;
    printf("Program received signal %s (%d)\n", signal_name(sig), sig);
    {
        struct user_regs_struct regs;

        if (dbg_get_regs(d, &regs) == 0) {
            printf("Stopped at ");
            dbg_print_location(d, (unsigned long)regs.rip);
            putchar('\n');
            (void)dbg_bt(d);
        }
    }
    return 0;
}

static int dbg_wait(dbg_t *d, int from_step)
{
    int status;
    pid_t waited;

    do {
        waited = waitpid(d->pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        msh_perror("dbg: waitpid");
        return -1;
    }
    return dbg_handle_stop(d, status, from_step);
}

static int dbg_continue_process(dbg_t *d)
{
    int sig = d->stop_signal;

    if (!d->started || d->exited || !d->stopped) {
        msh_error("dbg: the program is not being run");
        return -1;
    }

    /* At a breakpoint the original instruction has not executed yet.  Step
     * over it once, then put the 0xCC byte back before continuing. */
    if (d->pending_bp != NULL) {
        dbg_bp_t *bp = d->pending_bp;

        d->pending_bp = NULL;
        d->stopped = 0;
        if (ptrace(PTRACE_SINGLESTEP, d->pid, NULL, NULL) < 0) {
            msh_perror("dbg: ptrace SINGLESTEP");
            return -1;
        }
        (void)dbg_wait(d, 2);
        if (!d->exited)
            (void)dbg_bp_insert(d, bp);
        if (d->exited || d->stop_signal != 0 || d->pending_bp != NULL)
            return 0;
        sig = 0;
    }

    if (d->pending_bp != NULL) {
        (void)dbg_bp_insert(d, d->pending_bp);
        d->pending_bp = NULL;
    }
    d->stop_signal = 0;
    d->stopped = 0;
    if (ptrace(PTRACE_CONT, d->pid, NULL, (void *)(long)sig) < 0) {
        msh_perror("dbg: ptrace CONT");
        return -1;
    }
    return dbg_wait(d, 0);
}

static int dbg_step_process(dbg_t *d)
{
    dbg_bp_t *pending = d->pending_bp;

    if (!d->started || d->exited || !d->stopped) {
        msh_error("dbg: the program is not being run");
        return -1;
    }
    d->pending_bp = NULL;
    d->stopped = 0;
    if (ptrace(PTRACE_SINGLESTEP, d->pid, NULL, NULL) < 0) {
        msh_perror("dbg: ptrace SINGLESTEP");
        return -1;
    }
    (void)dbg_wait(d, 1);
    if (pending != NULL && !d->exited) {
        (void)dbg_bp_insert(d, pending);
    }
    return 0;
}

static char *dbg_trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t')
        ++text;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
        --end;
    *end = '\0';
    return text;
}

static int dbg_finish(dbg_t *d)
{
    struct user_regs_struct regs;
    unsigned long ret;
    int i;

    if (dbg_get_regs(d, &regs) < 0) {
        msh_error("dbg: no inferior");
        return -1;
    }
    {
        dbg_symbol_t *sym = dbg_symbol_at(d, (unsigned long)regs.rip);

        if (sym != NULL &&
            (unsigned long)regs.rip == sym->value + d->bias)
            ret = (unsigned long)peek_word(d->pid,
                                           (unsigned long)regs.rsp);
        else if (regs.rbp != 0 &&
                 (unsigned long)regs.rbp >= (unsigned long)regs.rsp)
            ret = (unsigned long)peek_word(d->pid,
                                           (unsigned long)regs.rbp + 8);
        else
            ret = (unsigned long)peek_word(d->pid,
                                           (unsigned long)regs.rsp);
    }
    if (ret < 0x10000UL) {
        msh_error("dbg: cannot find return address");
        return -1;
    }

    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (d->breakpoints[i].used && d->breakpoints[i].addr == ret)
            return dbg_continue_process(d);
    }
    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        if (!d->breakpoints[i].used) {
            dbg_bp_t *tmp = &d->breakpoints[i];

            memset(tmp, 0, sizeof(*tmp));
            tmp->used = 1;
            tmp->temporary = 1;
            tmp->id = ++d->next_bp_id;
            tmp->addr = ret;
            tmp->name = msh_strdup("finish");
            if (dbg_bp_insert(d, tmp) < 0)
                return -1;
            printf("Run till return at 0x%lx\n", ret);
            return dbg_continue_process(d);
        }
    }
    msh_error("dbg: too many breakpoints");
    return -1;
}

/* Startup / cleanup                                                  */

static int dbg_spawn(dbg_t *d, char **target)
{
    pid_t pid;
    int status;

    if (d->pid > 0 && !d->exited) {
        msh_error("dbg: the program is already running");
        return -1;
    }
    d->bias = 0;
    d->stop_signal = 0;
    d->stopped = 0;
    d->exited = 0;

    pid = fork();
    if (pid < 0) {
        msh_perror("dbg: fork");
        return -1;
    }
    if (pid == 0) {
        reset_child_signals();
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
            _exit(127);
        (void)personality(ADDR_NO_RANDOMIZE);
        raise(SIGSTOP);
        execvp(target[0], target);
        fprintf(stderr, "%s: %s: %s\n", MSH_NAME, target[0],
                strerror(errno));
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0 || !WIFSTOPPED(status)) {
        msh_error("dbg: the program did not stop for the debugger");
        return -1;
    }
    d->pid = pid;
    d->started = 1;
    if (ptrace(PTRACE_SETOPTIONS, pid, NULL,
               (void *)(long)PTRACE_O_EXITKILL) < 0) {
        msh_perror("dbg: ptrace SETOPTIONS");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        d->pid = 0;
        return -1;
    }
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        msh_perror("dbg: ptrace CONT");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        d->pid = 0;
        return -1;
    }
    if (waitpid(pid, &status, 0) < 0 || !WIFSTOPPED(status)) {
        msh_error("dbg: exec failed");
        d->pid = 0;
        return -1;
    }

    (void)dbg_load_bias(d);
    dbg_resolve_breakpoints(d);
    if (dbg_bp_insert_all(d) < 0)
        return -1;
    d->stopped = 1;
    return dbg_continue_process(d);
}

static void dbg_cleanup(dbg_t *d)
{
    int i;

    if (d->pid > 0 && !d->exited) {
        (void)kill(d->pid, SIGKILL);
        (void)waitpid(d->pid, NULL, 0);
    }
    d->pid = 0;
    for (i = 0; i < DBG_MAX_BREAKPOINTS; ++i) {
        free(d->breakpoints[i].name);
        memset(&d->breakpoints[i], 0, sizeof(d->breakpoints[i]));
    }
    for (i = 0; i < d->nsymbols; ++i)
        free(d->symbols[i].name);
    free(d->symbols);
    free(d->image);
    free(d->path);
    free(d->realpath);
    memset(d, 0, sizeof(*d));
}

static void dbg_print_help(void)
{
    printf("Commands:\n");
    printf("  break SYMBOL | *ADDR  set a breakpoint\n");
    printf("  run                   start the program\n");
    printf("  continue              continue execution\n");
    printf("  step                  execute one instruction\n");
    printf("  finish                run until the current frame returns\n");
    printf("  bt                    print the call stack\n");
    printf("  regs                  show registers\n");
    printf("  x/Nx ADDR             dump N 64-bit words ($rsp, symbol, 0x...)\n");
    printf("  info break            list breakpoints\n");
    printf("  delete N              delete breakpoint N\n");
    printf("  quit                  leave the debugger\n");
}

/* Command loop                                                       */

int cmd_dbg(int argc, char **argv, shell_state_t *sh)
{
    dbg_t dbg;
    char *line = NULL;
    size_t cap = 0;
    int argi = 1;
    char *target_path = NULL;
    char realbuf[PATH_MAX];

    (void)sh;
    while (argi < argc && strcmp(argv[argi], "--") == 0)
        ++argi;
    if (argi >= argc) {
        msh_usage("dbg PROGRAM [ARGS...]");
        return 2;
    }

    memset(&dbg, 0, sizeof(dbg));
    dbg.path = msh_strdup(argv[argi]);
    target_path = dbg.path;
    if (realpath(target_path, realbuf) != NULL)
        dbg.realpath = msh_strdup(realbuf);
    else
        dbg.realpath = msh_strdup(target_path);

    if (dbg_load_elf(&dbg, target_path) < 0) {
        msh_perror(target_path);
        dbg_cleanup(&dbg);
        return 1;
    }

    printf("dbg: loaded %s (%s, %d symbols)\n", target_path,
           dbg.is_pie ? "PIE" : "executable", dbg.nsymbols);
    fflush(stdout);

    for (;;) {
        char *cmd;
        char *args;
        char *sp;

        if (isatty(STDIN_FILENO)) {
            fputs("(dbg) ", stdout);
            fflush(stdout);
        }
        if (getline(&line, &cap, stdin) < 0)
            break;
        {
            size_t len = strlen(line);

            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
        }
        cmd = dbg_trim(line);
        if (*cmd == '\0')
            continue;
        sp = cmd;
        while (*sp != '\0' && !isspace((unsigned char)*sp))
            ++sp;
        if (*sp != '\0') {
            *sp = '\0';
            args = dbg_trim(sp + 1);
        } else {
            args = sp;
        }

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
            dbg_print_help();
        } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
            (void)dbg_add_breakpoint(&dbg, args);
        } else if (strcmp(cmd, "run") == 0 || strcmp(cmd, "r") == 0) {
            if (dbg.started && !dbg.exited) {
                msh_error("dbg: the program is already running");
            } else {
                if (dbg.exited) {
                    int j;

                    for (j = 0; j < DBG_MAX_BREAKPOINTS; ++j) {
                        dbg.breakpoints[j].inserted = 0;
                        dbg.breakpoints[j].addr = dbg.breakpoints[j].has_symbol
                                                       ? dbg.breakpoints[j].sym_value
                                                       : dbg.breakpoints[j].addr;
                    }
                    dbg.exited = 0;
                }
                (void)dbg_spawn(&dbg, argv + argi);
            }
        } else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "c") == 0) {
            (void)dbg_continue_process(&dbg);
        } else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
            (void)dbg_step_process(&dbg);
        } else if (strcmp(cmd, "finish") == 0) {
            (void)dbg_finish(&dbg);
        } else if (strcmp(cmd, "bt") == 0 ||
                   strcmp(cmd, "backtrace") == 0) {
            (void)dbg_bt(&dbg);
        } else if (strcmp(cmd, "regs") == 0 ||
                   strcmp(cmd, "info regs") == 0) {
            (void)dbg_print_regs(&dbg);
        } else if (strcmp(cmd, "x") == 0) {
            (void)dbg_dump_memory(&dbg, args);
        } else if (cmd[0] == 'x' && cmd[1] == '/') {
            size_t need = strlen(cmd) + strlen(args) + 2;
            char *spec = msh_malloc(need);

            snprintf(spec, need, "%s %s", cmd + 1, args);
            (void)dbg_dump_memory(&dbg, spec);
            free(spec);
        } else if (strcmp(cmd, "info") == 0) {
            if (strcmp(args, "break") == 0 ||
                strcmp(args, "breakpoints") == 0)
                dbg_info_breakpoints(&dbg);
            else
                msh_error("dbg: unknown info command: %s", args);
        } else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "d") == 0) {
            (void)dbg_delete_breakpoint(&dbg, atoi(args));
        } else {
            msh_error("dbg: undefined command: %s", cmd);
        }
        fflush(stdout);
    }

    free(line);
    dbg_cleanup(&dbg);
    return 0;
}
