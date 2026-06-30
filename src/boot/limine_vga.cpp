#include "limine_vga.h"
#include <lib/string.h>

KRenderer kRenderer = {0};

static int console_lock_depth = 0;
static uint64_t console_lock_flags = 0;

void ConsoleLock()
{
    if (console_lock_depth++ == 0)
    {
        asm volatile("pushfq; pop %0; cli" : "=r"(console_lock_flags) : : "memory");
    }
}

void ConsoleUnlock()
{
    if (console_lock_depth > 0 && --console_lock_depth == 0)
    {
        FlushConsole();
        asm volatile("push %0; popfq" :: "r"(console_lock_flags) : "memory", "cc");
    }
}

static void ConsoleComputeDimensions()
{
    uint64_t fw = 8;
    uint64_t fh = kRenderer.font.psf1_Header->characterSize;

    int cols = (int)(kRenderer.framebuffer.width / fw);
    int rows = (int)(kRenderer.framebuffer.height / fh);

    if (cols > CONSOLE_MAX_COLS) cols = CONSOLE_MAX_COLS;
    if (rows > CONSOLE_MAX_ROWS) rows = CONSOLE_MAX_ROWS;

    kRenderer.cols = cols;
    kRenderer.rows = rows;
}

static void ScrollCells()
{
    int cols = kRenderer.cols;
    int rows = kRenderer.rows;

    memmove(&kRenderer.cells[0][0],
            &kRenderer.cells[1][0],
            sizeof(ConsoleCell) * CONSOLE_MAX_COLS * (rows - 1));
    
    ConsoleCell blank = { 0, kRenderer.color };
    for (int c = 0; c < cols; c++)
    {
        kRenderer.cells[rows - 1][c] = blank;
    }

    kRenderer.dirty = true;
}

void DrawChar(char c)
{
    if (kRenderer.cols == 0 || kRenderer.rows == 0)
    {
        ConsoleComputeDimensions();
    }

    int cols = kRenderer.cols;
    int rows = kRenderer.rows;

    // ----- ANSI/VT100 escape-sequence parsing -------------------------------
    // Handles CSI sequences delivered one byte at a time (e.g. "\033[2J\033[H").
    if (kRenderer.esc_state == 1)
    {
        // Just saw ESC: a CSI must follow with '['. Anything else aborts.
        if (c == '[')
        {
            kRenderer.esc_state = 2;
            kRenderer.esc_nparams = 0;
            kRenderer.esc_params[0] = 0;
            kRenderer.esc_params[1] = 0;
        }
        else
        {
            kRenderer.esc_state = 0;
        }
        return;
    }

    if (kRenderer.esc_state == 2)
    {
        if (c >= '0' && c <= '9')
        {
            if (kRenderer.esc_nparams == 0) kRenderer.esc_nparams = 1;
            int i = kRenderer.esc_nparams - 1;
            kRenderer.esc_params[i] = kRenderer.esc_params[i] * 10 + (c - '0');
            return;
        }
        if (c == ';')
        {
            if (kRenderer.esc_nparams == 0) kRenderer.esc_nparams = 1;
            if (kRenderer.esc_nparams < 2) kRenderer.esc_nparams++;
            return;
        }

        // Final byte: dispatch the command.
        kRenderer.esc_state = 0;

        if (c == 'J')
        {
            // Erase in display. We support the common "clear whole screen"
            // (ESC[2J); other variants also clear fully for simplicity.
            ClearScreen(kRenderer.bg_color, false);
        }
        else if (c == 'H' || c == 'f')
        {
            // Cursor position (1-based row;col). No params => home.
            int row = (kRenderer.esc_nparams >= 1 && kRenderer.esc_params[0] > 0)
                          ? kRenderer.esc_params[0] - 1 : 0;
            int col = (kRenderer.esc_nparams >= 2 && kRenderer.esc_params[1] > 0)
                          ? kRenderer.esc_params[1] - 1 : 0;
            if (row >= rows) row = rows - 1;
            if (col >= cols) col = cols - 1;
            kRenderer.cursor_row = row;
            kRenderer.cursor_col = col;
        }
        // Unknown final bytes are ignored.
        return;
    }

    if (c == 0x1B) // ESC: begin an escape sequence
    {
        kRenderer.esc_state = 1;
        return;
    }

    if (c == '\b')
    {
        if (kRenderer.cursor_col > 0)
        {
            --kRenderer.cursor_col;
        }
        else if (kRenderer.cursor_row > 0)
        {
            --kRenderer.cursor_row;
            kRenderer.cursor_col = cols - 1;
        }
        kRenderer.cells[kRenderer.cursor_row][kRenderer.cursor_col] = { 0, kRenderer.color };
        kRenderer.dirty = true;
        return;
    }

    if (c == '\r')
    {
        kRenderer.cursor_col = 0;
        return;
    }

    if (c == '\t')
    {
        kRenderer.cursor_col = (kRenderer.cursor_col + 4) & ~3;
        if (kRenderer.cursor_col >= cols)
        {
            kRenderer.cursor_col = 0;
            ++kRenderer.cursor_row;
            if (kRenderer.cursor_row >= rows)
            {
                ScrollCells();
                kRenderer.cursor_row = rows - 1;
            }
        }
        return;
    }

    if (c == '\n')
    {
        kRenderer.cursor_col = 0;
        ++kRenderer.cursor_row;
        if (kRenderer.cursor_row >= rows)
        {
            ScrollCells();
            kRenderer.cursor_row = rows - 1;
        }
        return;
    }

    if (kRenderer.cursor_col >= cols)
    {
        kRenderer.cursor_col = 0;
        ++kRenderer.cursor_row;
        if (kRenderer.cursor_row >= rows)
        {
            ScrollCells();
            kRenderer.cursor_row = rows - 1;
        }
    }

    kRenderer.cells[kRenderer.cursor_row][kRenderer.cursor_col] = { c, kRenderer.color };
    ++kRenderer.cursor_col;
    kRenderer.dirty = true;
}

void FlushConsole()
{
    if (!kRenderer.dirty) return;
    kRenderer.dirty = false;

    uint64_t fw = 8;
    uint64_t fh = kRenderer.font.psf1_Header->characterSize;
    int cols = kRenderer.cols;
    int rows = kRenderer.rows;
    uint32_t bg = kRenderer.bg_color;

    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            ConsoleCell &cell = kRenderer.cells[row][col];
            
            uint8_t *glyph = cell.c ? 
                (uint8_t*)kRenderer.font.glyphBuffer + ((uint8_t)cell.c * fh) : nullptr;
            for (uint64_t py = 0; py < fh; py++)
            {
                uint32_t *dst = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address
                + (row * fh + py) * kRenderer.framebuffer.pitch)
                + col * fw;
                
                uint8_t row_bits = glyph ? glyph[py] : 0;
                for (uint64_t px = 0; px < fw; px++)
                {
                    dst[px] = (row_bits & (0x80 >> px)) ? cell.color : bg;
                }
            }
        }
    }
}

void ClearScreen(uint32_t color, bool reset)
{
    int cols = kRenderer.cols ? kRenderer.cols : CONSOLE_MAX_COLS;
    int rows = kRenderer.rows ? kRenderer.rows : CONSOLE_MAX_ROWS;

    ConsoleCell blank = { 0, kRenderer.color };
    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            kRenderer.cells[r][c] = blank;
        }
    }

    uint64_t ppl = kRenderer.framebuffer.pitch / sizeof(uint32_t);
    for (uint64_t y = 0; y < kRenderer.framebuffer.height; y++)
    {
        uint32_t *row_ptr = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address
                            + y * kRenderer.framebuffer.pitch);

        for (uint64_t x = 0; x < ppl; x++) row_ptr[x] = color;
    }

    kRenderer.bg_color = color;

    if (reset)
    {
        kRenderer.cursor_col = 0;
        kRenderer.cursor_row = 0;
    }

    kRenderer.dirty = false;
}