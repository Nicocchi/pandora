#pragma once

constexpr uint16_t GDT_KERNEL_CODE = 0x08;
constexpr uint16_t GDT_KERNEL_DATA = 0x10;

constexpr uint16_t GDT_USER_DATA = 0x18;
constexpr uint16_t GDT_USER_CODE = 0x20;

constexpr uint16_t GDT_USER_DATA_RING3 = 0x1B;
constexpr uint16_t GDT_USER_CODE_RING3 = 0x23;
