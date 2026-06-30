/**
 * @file        keyboard.cpp
 * @brief       Implementation of the PS/2 keyboard driver mappings and handlers.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 31, 2026
 * 
 * @details     Processes PS/2 Scancode Set 1, translates inputs, tracks lock keys,
 * and maintains a synchronized circular buffer for event deliver.
 */

#include <stdint.h>
#include <stddef.h>

#include "ps2_keyboard.h"
#include "common.h"
#include "lib/stdio.h"
#include "drivers/serial_port.h"

/**
 * @brief Scancode-to-KeyCode translation lookup table.
 * 
 * @details Maps IBM Set 1 standard single-byte make codes to non-printable KeyCode symbols.
 *          Printable character entries remain KeyCode::Unknown to enforce handling via 
 *          ASCII map utilities.
 */
static const KeyCode scancode_to_keycode[128] =
{
    /*00*/ KeyCode::Unknown,   /*01*/ KeyCode::Escape,
    /*02*/ KeyCode::Unknown,   /*03*/ KeyCode::Unknown,
    /*04*/ KeyCode::Unknown,   /*05*/ KeyCode::Unknown,
    /*06*/ KeyCode::Unknown,   /*07*/ KeyCode::Unknown,
    /*08*/ KeyCode::Unknown,   /*09*/ KeyCode::Unknown,
    /*0A*/ KeyCode::Unknown,   /*0B*/ KeyCode::Unknown,
    /*0C*/ KeyCode::Unknown,   /*0D*/ KeyCode::Unknown,
    /*0E*/ KeyCode::Backspace, /*0F*/ KeyCode::Tab,
    /*10*/ KeyCode::Unknown,   /*11*/ KeyCode::Unknown,
    /*12*/ KeyCode::Unknown,   /*13*/ KeyCode::Unknown,
    /*14*/ KeyCode::Unknown,   /*15*/ KeyCode::Unknown,
    /*16*/ KeyCode::Unknown,   /*17*/ KeyCode::Unknown,
    /*18*/ KeyCode::Unknown,   /*19*/ KeyCode::Unknown,
    /*1A*/ KeyCode::Unknown,   /*1B*/ KeyCode::Unknown,
    /*1C*/ KeyCode::Enter,     /*1D*/ KeyCode::LeftCtrl,
    /*1E*/ KeyCode::Unknown,   /*1F*/ KeyCode::Unknown,
    /*20*/ KeyCode::Unknown,   /*21*/ KeyCode::Unknown,
    /*22*/ KeyCode::Unknown,   /*23*/ KeyCode::Unknown,
    /*24*/ KeyCode::Unknown,   /*25*/ KeyCode::Unknown,
    /*26*/ KeyCode::Unknown,   /*27*/ KeyCode::Unknown,
    /*28*/ KeyCode::Unknown,   /*29*/ KeyCode::Unknown,
    /*2A*/ KeyCode::LeftShift, /*2B*/ KeyCode::Unknown,
    /*2C*/ KeyCode::Unknown,   /*2D*/ KeyCode::Unknown,
    /*2E*/ KeyCode::Unknown,   /*2F*/ KeyCode::Unknown,
    /*30*/ KeyCode::Unknown,   /*31*/ KeyCode::Unknown,
    /*32*/ KeyCode::Unknown,   /*33*/ KeyCode::Unknown,
    /*34*/ KeyCode::Unknown,   /*35*/ KeyCode::Unknown,
    /*36*/ KeyCode::RightShift,/*37*/ KeyCode::Unknown,
    /*38*/ KeyCode::LeftAlt,   /*39*/ KeyCode::Space,
    /*3A*/ KeyCode::CapsLock,  /*3B*/ KeyCode::F1,
    /*3C*/ KeyCode::F2,        /*3D*/ KeyCode::F3,
    /*3E*/ KeyCode::F4,        /*3F*/ KeyCode::F5,
    /*40*/ KeyCode::F6,        /*41*/ KeyCode::F7,
    /*42*/ KeyCode::F8,        /*43*/ KeyCode::F9,
    /*44*/ KeyCode::F10,       /*45*/ KeyCode::NumLock,
    /*46*/ KeyCode::ScrollLock,/*47*/ KeyCode::Home,
    /*48*/ KeyCode::ArrowUp,   /*49*/ KeyCode::PageUp,
    /*4A*/ KeyCode::Unknown,   /*4B*/ KeyCode::ArrowLeft,
    /*4C*/ KeyCode::Unknown,   /*4D*/ KeyCode::ArrowRight,
    /*4E*/ KeyCode::Unknown,   /*4F*/ KeyCode::End,
    /*50*/ KeyCode::ArrowDown, /*51*/ KeyCode::PageDown,
    /*52*/ KeyCode::Insert,    /*53*/ KeyCode::Delete,
    /*54*/ KeyCode::Unknown,   /*55*/ KeyCode::Unknown,
    /*56*/ KeyCode::Unknown,   /*57*/ KeyCode::F11,
    /*58*/ KeyCode::F12,

    // Remaining 103-127 filled with Unknown
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown,
    KeyCode::Unknown, KeyCode::Unknown, KeyCode::Unknown
};

/**
 * @brief Maps scancode -> ASCII character when unshifted.
 *        '\0' means not a printable character.
 * 
 */
static const char scancode_ascii_lower[128] =
{
    /*00*/'\0', /*01*/'\0', /*02*/'1',  /*03*/'2',  /*04*/'3',  /*05*/'4',
    /*06*/'5',  /*07*/'6',  /*08*/'7',  /*09*/'8',  /*0A*/'9',  /*0B*/'0',
    /*0C*/'-',  /*0D*/'=',  /*0E*/'\b', /*0F*/'\t', /*10*/'q',  /*11*/'w',
    /*12*/'e',  /*13*/'r',  /*14*/'t',  /*15*/'y',  /*16*/'u',  /*17*/'i',
    /*18*/'o',  /*19*/'p',  /*1A*/'[',  /*1B*/']',  /*1C*/'\0', /*1D*/'\0',
    /*1E*/'a',  /*1F*/'s',  /*20*/'d',  /*21*/'f',  /*22*/'g',  /*23*/'h',
    /*24*/'j',  /*25*/'k',  /*26*/'l',  /*27*/';',  /*28*/'\'', /*29*/'`',
    /*2A*/'\0', /*2B*/'\\', /*2C*/'z',  /*2D*/'x',  /*2E*/'c',  /*2F*/'v',
    /*30*/'b',  /*31*/'n',  /*32*/'m',  /*33*/',',  /*34*/'.',  /*35*/'/',
    /*36*/'\0', /*37*/'*',  /*38*/'\0', /*39*/' ',  /*3A*/'\0',
    
    // F1-F12 and special keys: all non-printable
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
};

/**
 * @brief Maps scancode -> ASCII character when shifted.
 *        '\0' means not a printable character.
 * 
 */
static const char scancode_ascii_upper[128] =
{
    /*00*/'\0', /*01*/'\0', /*02*/'!',  /*03*/'@',  /*04*/'#',  /*05*/'$',
    /*06*/'%',  /*07*/'^',  /*08*/'&',  /*09*/'*',  /*0A*/'(',  /*0B*/')',
    /*0C*/'_',  /*0D*/'+',  /*0E*/'\b', /*0F*/'\t', /*10*/'Q',  /*11*/'W',
    /*12*/'E',  /*13*/'R',  /*14*/'T',  /*15*/'Y',  /*16*/'U',  /*17*/'I',
    /*18*/'O',  /*19*/'P',  /*1A*/'{',  /*1B*/'}',  /*1C*/'\n', /*1D*/'\0',
    /*1E*/'A',  /*1F*/'S',  /*20*/'D',  /*21*/'F',  /*22*/'G',  /*23*/'H',
    /*24*/'J',  /*25*/'K',  /*26*/'L',  /*27*/':',  /*28*/'"',  /*29*/'~',
    /*2A*/'\0', /*2B*/'|',  /*2C*/'Z',  /*2D*/'X',  /*2E*/'C',  /*2F*/'V',
    /*30*/'B',  /*31*/'N',  /*32*/'M',  /*33*/'<',  /*34*/'>',  /*35*/'?',
    /*36*/'\0', /*37*/'*',  /*38*/'\0', /*39*/' ',  /*3A*/'\0',
    
    // F1-F12 and special keys: all non-printable
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
    '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0',
};

#define KEYBOARD_BUFFER_SIZE 64

static bool shift_held = false;
static bool caps_lock = false;
static bool ctrl_held = false;
static bool alt_held = false;

static KeyEvent key_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buf_head = 0;
static volatile uint32_t buf_tail = 0;

static Task* keyboard_waiter = nullptr;

static void PushKeyEvent(KeyEvent event)
{
    uint32_t next = (buf_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == buf_tail) return;
    key_buffer[buf_head] = event;
    buf_head = next;

    // Wake any task blocked waiting for keyboard event
    if (keyboard_waiter)
    {
        Task* waiter = keyboard_waiter;
        keyboard_waiter = nullptr;
        scheduler.Wake(waiter);
    }
}

bool PopKeyEvent(KeyEvent* out)
{
    if (buf_head == buf_tail) return false;
    *out = key_buffer[buf_tail];
    buf_tail = (buf_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return true;
}

static void WaitInputBuffer()
{
    for (int i = 0; i < 100000; i++)
    {
        if ((InPortB(0x64) & 0x02) == 0) return;
    }
}

static void WaitOutputBuffer()
{
    for (int i = 0; i < 100000; i++)
    {
        if (InPortB(0x64) & 0x01) return;
    }
}

static void PS2WriteCommand(uint8_t cmd)
{
    WaitInputBuffer();
    OutPortB(0x64, cmd);
}

static void PS2WriteData(uint8_t data)
{
    WaitInputBuffer();
    OutPortB(0x60, data);
}

static uint8_t PS2ReadData()
{
    WaitOutputBuffer();
    return InPortB(0x60);
}

static void PS2FlushOutput()
{
    while (InPortB(0x64) & 0x01)
    {
        (void)InPortB(0x60);
    }
}

void InitKeyboard()
{
    shift_held = false;
    caps_lock = false;
    ctrl_held = false;
    alt_held = false;
    buf_head = 0;
    buf_tail = 0;

    RegisterInterruptHandler(33, KeyboardHandler);
}

static void ProcessScancode(uint8_t raw)
{
    uint8_t scancode = raw & 0x7F;
    bool pressed = !(raw & 0x80);

    KeyCode kc = scancode_to_keycode[scancode];

    switch (kc)
    {
        case KeyCode::LeftShift:
        case KeyCode::RightShift:
        {
            shift_held = pressed;
            return;
        }

        case KeyCode::LeftCtrl:
        case KeyCode::RightCtrl:
        {
            ctrl_held = pressed;
            return;
        }

        case KeyCode::LeftAlt:
        case KeyCode::RightAlt:
        {
            alt_held = pressed;
            return;
        }

        case KeyCode::CapsLock:
        {
            if (pressed) caps_lock = !caps_lock;
            return;
        }

        default:
        {
            break;
        }
    }

    bool use_upper = shift_held ^ caps_lock;
    char ascii = use_upper ? scancode_ascii_upper[scancode] : scancode_ascii_lower[scancode];

    KeyEvent event;
    event.keycode = kc;
    event.ascii = ascii;
    event.pressed = pressed;
    event.shift = shift_held;
    event.ctrl = ctrl_held;
    event.alt = alt_held;
    event.caps_lock = caps_lock;

    PushKeyEvent(event);
}

void KeyboardHandler(InterruptFrame* frame)
{
    (void)frame;

    ProcessScancode(InPortB(0x60));
    OutPortB(0x20, 0x20);
}

void KeyboardPoll()
{
    while (InPortB(0x64) & 0x01)
    {
        ProcessScancode(InPortB(0x60));
    }
}

void KeyboardSetWaiter(Task* task)
{
    keyboard_waiter = task;
}
void KeyboardClearWaiter()
{
    keyboard_waiter = nullptr;
}

bool KeyboardHasEvent()
{
    return buf_head != buf_tail;
}

char KeyboardReadChar()
{
    for (;;)
    {
        KeyboardPoll();

        KeyEvent ev;
        if (!PopKeyEvent(&ev))
            continue;

        if (!ev.pressed)
            continue;

        if (ev.ascii != '\0')
            return ev.ascii;

        if (ev.keycode == KeyCode::Enter)
            return '\n';
    }
}