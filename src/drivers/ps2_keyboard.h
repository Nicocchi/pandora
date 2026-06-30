/**
 * @file        keyboard.h
 * @brief       Subsystem for handling PS/2 keyboard input and scancode decoding.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 31, 2026
 * 
 * @details     Provides key definitions, state tracking structure, and the interrupt
 * handler needed to process raw keyboard harware input into an event queue.
 */

#pragma once

#include "interrupts/idt.h"
#include "processes/scheduler.h"

 /**
 * @brief Hardware-independent key identifiers.
 * 
 * @details Special keys map to values above 0x100 to avoid conflicting with
 *          the standard printable ASCII range.
 */
enum class KeyCode : uint32_t
{
    Unknown = 0x000,

    // Control keys
    Escape = 0x100,
    Backspace,
    Tab,
    Enter,
    CapsLock,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,
    Space,

    // Function keys
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,

    // Navigation
    Insert, Delete,
    Home, End,
    PageUp, PageDown,
    ArrowUp, ArrowDown,
    ArrowLeft, ArrowRight,

    // Lock keys
    ScrollLock,
    NumLock
};

/**
 * @brief Represents a single resolved keyboard action.
 * 
 * @details Decouples raw hardware scancodes from logical key meanings,
 *          bundling peripheral modifier states into a single transaction package.
 */
struct KeyEvent
{
    KeyCode keycode;    /**< Hardware-independent key identifier. */
    char ascii;         /**< Decoded ASCII character if printable, otherwise 0. */
    bool pressed;       /**< Action state: true if key down, false if key up. */
    bool shift;         /**< Status of either Shift key at the moment of event. */
    bool ctrl;          /**< Status of either Ctrl key at the moment of event. */
    bool alt;           /**< Status of either Alt key at the moment of event. */
    bool caps_lock;     /**< Toggle status of Caps Lock at the moment of event. */
};

/**
 * @brief Initializes the keyboard driver hardware and driver states.
 * 
 * @details Configures the keyboard controller, clears internal input buffers,
 *          and readies modifier toggle registers.
 */
void InitKeyboard();

/**
 * @brief Top-half interrupt service routine wrapper for keyboard inputs.
 * 
 * @details Reads scancodes directly from the hardware I/O ports, processes state transitions,
 *          and posts events to the internal OS queue.
 * 
 * @param frame Pointer to the processor context at the time of the interrupt.
 */
void KeyboardHandler(InterruptFrame* frame);

/**
 * @brief Polls and pops the oldest event from the keyboard input queue.
 * 
 * @details Thread-safe context function to fetch driver events.
 * 
 * @param[out] out Pointer to a KeyEvent structure where data will be copied.
 * @return true If an event was successfully popped.
 * @return false If the queue was empty.
 */
bool PopKeyEvent(KeyEvent* out);

void KeyboardSetWaiter(Task* task);
void KeyboardClearWaiter();

bool KeyboardHasEvent();

// Drain any pending scancodes from the PS/2 data port (IRQ fallback).
void KeyboardPoll();

// Block until a key press is available (same wait model as KTerminal).
char KeyboardReadChar();