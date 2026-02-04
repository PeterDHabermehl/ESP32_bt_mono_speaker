 // version.h
#pragma once

#define SERIAL_FILENAME "/serial_number.txt"
#define SERIAL_LENGTH   24

#define FW_COMPANY     "Dr\u00F8mpelbert L\u00E4rmverk AB"
#define FW_NAME        "DL Acoustics Speaker"
#define FW_DEVICE      "DL-BB8"
#define FW_DEVICESTR   "Buller Boll 8in."

#define FW_CATCHPHRASE "Fasten your seat belts!"

#define FW_VER_MAJOR    0
#define FW_VER_MINOR    9
#define FW_VER_PATCH    2

#define STR_HELPER(x)   #x
#define STR(x)          STR_HELPER(x)

#define FW_VERSION      "v" STR(FW_VER_MAJOR) "." STR(FW_VER_MINOR) "." STR(FW_VER_PATCH)

#define FW_BUILD_DATE  __DATE__
#define FW_BUILD_TIME  __TIME__


void printVersion();
void serial_read(char *buffer);
void serial_write(const char* buffer);
