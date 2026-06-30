#include "../libc/stdio.h"
#include "../libc/syscall.h"


// Skip leading spaces, return pointer to first non-space char
static const char* SkipSpaces(const char* s)
{
    while (*s == ' ') s++;
    return s;
}

// Extract first token into out_buf, return pointer to remainder of string
static const char* ParseToken(const char* s, char* out_buf, uint64_t out_size)
{
    s = SkipSpaces(s);
    uint64_t i = 0;
    while (*s && *s != ' ' && i < out_size - 1)
    {
        out_buf[i++] = *s++;
    }
    out_buf[i] = '\0';
    return s; // points at remainder (args)
}


// ---------------------------------------------------------------------------
// Terminal state
//
// NOTE: the flat binary is loaded RX (read/execute, no write), so all mutable
// state must live on the RW user stack — globals/.bss would fault on write.
// ---------------------------------------------------------------------------
struct Terminal
{
    char input_buffer[256];
    uint32_t input_length;
    char cwd[256];
};

static const char prompt[] = "> ";

static void DispatchCommand(Terminal* term, const char* input)
{
    char cmd[64];
    const char* rest = ParseToken(input, cmd, sizeof(cmd));

    if (cmd[0] == '\0')
    {
        return;
    }
    else if (StrEq(cmd, "help"))
    {
        printf("Built-in commands:\n");
        printf("  help        - show this help\n");
        printf("  echo <text> - print text back\n");
        printf("  clear       - clear the screen\n");
        printf("  exit        - quit the terminal\n");
        printf("Any other word runs /bin/<name>.bin (e.g. 'ls', 'ls /boot').\n");
    }
    else if (StrEq(cmd, "echo"))
    {
        const char* arg = SkipSpaces(rest);
        printf(arg);
        printf("\n");
    }
    else if (StrEq(cmd, "clear"))
    {
        // VT100/ANSI: erase the whole screen, then home the cursor. The
        // console (DrawChar) interprets these escapes - same as a real terminal.
        printf("\033[2J\033[H");
    }
    else if (StrEq(cmd, "exit"))
    {
        printf("Goodbye!\n");
        sys_exit(0);
    }
    else if (StrEq(cmd, "cd"))
    {
        const char* arg = SkipSpaces(rest);

        // No argument: go home to "/"
        if (arg[0] == '\0') arg = "/";

        if (sys_chdir(arg) < 0)
        {
            printf("cd: no such directory: ");
            printf(arg);
            printf("\n");
        }
        else
        {
            sys_getcwd(term->cwd, sizeof(term->cwd));
        }
    }
    else
    {
        // Not a built-in: treat `cmd` as the name of a program in /bin and try
        // to run it. e.g. "ls /boot" -> exec "/bin/ls.bin" with argv {ls, /boot}.
        // fork() this shell, exec the program in the child, wait in the parent.
        char path[80];
        {
            const char* prefix = "/bin/";
            const char* suffix = ".bin";
            uint64_t p = 0;
            for (uint64_t i = 0; prefix[i]; i++) path[p++] = prefix[i];
            for (uint64_t i = 0; cmd[i]; i++)    path[p++] = cmd[i];
            for (uint64_t i = 0; suffix[i]; i++) path[p++] = suffix[i];
            path[p] = '\0';
        }

        // Build argv: argv[0] is the command name, the rest are the
        // whitespace-separated arguments. Tokenize a writable copy of `rest`
        // in place (the original input must not be mutated here).
        char  argbuf[256];
        char* argv[16];
        int   argc = 0;

        argv[argc++] = cmd;

        {
            const char* r = SkipSpaces(rest);
            if (*r == '\0' && StrEq(cmd, "ls"))
            {
                argv[argc++] = term->cwd;
            }
            uint64_t n = 0;
            while (r[n] && n < sizeof(argbuf) - 1)
            {
                argbuf[n] = r[n];
                n++;
            }
            argbuf[n] = '\0';

            char* s = argbuf;
            while (*s && argc < 15)
            {
                while (*s == ' ') s++;
                if (*s == '\0') break;
                argv[argc++] = s;
                while (*s && *s != ' ') s++;
                if (*s == ' ') { *s = '\0'; s++; }
            }
        }
        argv[argc] = (char*)0;

        long pid = sys_fork();
        if (pid < 0)
        {
            printf(cmd);
            printf(": fork failed\n");
        }
        else if (pid == 0)
        {
            // Child: become the requested program. exec only returns on
            // failure (e.g. no such binary in /bin) -> command not found.
            sys_exec(path, argv);
            printf("command not found: ");
            printf(cmd);
            printf("\n");
            sys_exit(127);
        }
        else
        {
            // Parent: block until the child terminates.
            sys_wait();
        }
    }
}

int main()
{
    // Stack-allocated so it lands in the RW user stack (see note above).
    Terminal term;
    term.input_length = 0;
    term.input_buffer[0] = '\0';
    term.cwd[0] = '/';
    term.cwd[1] = '\0';

    printf("\nWelcome to Pandora\n");
    printf("~%s > ", term.cwd);

    while (true)
    {
        // Poll stdin from userspace until a key is available.
        char c = 0;
        while (sys_read(0, &c, 1) == 0)
        {
            asm volatile("pause");
        }

        if (c == '\n')
        {
            printf("\n");

            term.input_buffer[term.input_length] = '\0';
            DispatchCommand(&term, term.input_buffer);

            term.input_length = 0;
            term.input_buffer[0] = '\0';

            printf("~%s > ", term.cwd);
        }
        else if (c == '\b')
        {
            if (term.input_length > 0)
            {
                term.input_length--;
                term.input_buffer[term.input_length] = '\0';

                // Console DrawChar('\b') erases the glyph; only do it while
                // there is typed input so we never eat into the prompt.
                printf("\b");
            }
        }
        else
        {
            if (term.input_length < sizeof(term.input_buffer) - 1)
            {
                term.input_buffer[term.input_length++] = c;
                term.input_buffer[term.input_length] = '\0';

                // Echo the typed character
                printf("%c", c);
            }
        }
    }

    return 0;
}
