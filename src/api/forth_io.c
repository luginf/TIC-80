// MIT License

// Copyright (c) 2024 TIC-80 contributors

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// pforth I/O layer for TIC-80: routes all pforth terminal output to the
// TIC-80 trace callback and stubs out file I/O (not needed for a cartridge).

#include "api.h"
// Undefine TIC-80's MIN/MAX before pforth redefines them to avoid warnings.
#undef MIN
#undef MAX
#include "pf_all.h"
#include <stdio.h>
#include <stdint.h>

// ---- output buffering -------------------------------------------------------

// Characters from pforth's EMIT (e.g. from '.' or TYPE) accumulate here and
// are flushed to the trace callback on newline or when the buffer is full.
#define FORTH_IO_BUF 256

static char        gOutBuf[FORTH_IO_BUF];
static int         gOutLen = 0;
static tic_tick_data* gTickData = NULL;

static void flushOut(void)
{
    if (gOutLen > 0 && gTickData && gTickData->trace)
    {
        gOutBuf[gOutLen] = '\0';
        gTickData->trace(gTickData->data, gOutBuf, 15);
        gOutLen = 0;
    }
}

void forthInitIO(tic_tick_data* tickData)
{
    gTickData = tickData;
    gOutLen   = 0;
}

void forthTermIO(void)
{
    flushOut();
    gTickData = NULL;
}

// Returns the accumulated output buffer so the caller can use it as an error
// message when pfInterpretText() returns a non-zero throw code.
const char* forthGetOutputBuffer(void)
{
    gOutBuf[gOutLen] = '\0';
    return gOutBuf;
}

void forthClearOutputBuffer(void)
{
    gOutLen = 0;
}

void forthFlushOutput(void)
{
    flushOut();
}

// ---- pforth terminal I/O callbacks ------------------------------------------

int sdTerminalOut(char c)
{
    if (gOutLen < FORTH_IO_BUF - 1)
        gOutBuf[gOutLen++] = c;

    if (c == '\n' || gOutLen >= FORTH_IO_BUF - 1)
        flushOut();

    return 0;
}

int sdTerminalEcho(char c)
{
    return sdTerminalOut(c);
}

int sdTerminalIn(void)
{
    // No interactive input during game execution.
    return -1;
}

int sdTerminalFlush(void)
{
    flushOut();
    return 0;
}

int sdQueryTerminal(void)
{
    return 0;
}

void sdTerminalInit(void)  {}
void sdTerminalTerm(void)  {}

cell_t sdSleepMillis(cell_t msec)
{
    (void)msec;
    return 0;
}

// File I/O stubs are provided by pf_io.c when PF_NO_FILEIO is defined.
// This file only needs to provide the terminal I/O functions.
