// MIT License

// Copyright (c) 2026 TIC-80 contributors

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

// Forth through Lugforth, sandboxed.
//
// pForth (src/api/forth.c) runs a cartridge as native code: its @ and ! take
// the player's own addresses. Here the Forth system is a WebAssembly module
// (src/api/lugforth/, built in the Lugforth repository) run by wasm3, so every
// address a cartridge makes is an offset in the module's memory, an access
// past it traps, and the only ways out of the module are the imports below,
// which check the addresses they are handed. The words, their names and their
// stack effects are those of the pForth binding, so its cartridges keep
// working; the two builds are exclusive (the same script id).

#include "core/core.h"

// Avoid redefining u* and s*
#define d_m3ShortTypesDefined 1
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef double f64;
typedef float f32;

#include "wasm3.h"
#include "m3_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lugforth/tic80_api.h"

static const u8 LugforthModule[] =
    {
#include "lugforth/lugforth-tic80.wasm.h"
};

#define LUGFORTH_STACK_SIZE (64 * 1024)
// The module grows its memory as the dictionary and ALLOCATE need: 16 MiB at most.
#define LUGFORTH_MAX_PAGES 256
#define LUGFORTH_OUT_SIZE  256
#define LUGFORTH_STR_SIZE  1024

typedef struct
{
    tic_core* core;
    IM3Environment env;
    IM3Runtime runtime;

    IM3Function init, alloc, free, eval, find, call, call1, depth;

    // Execution tokens of the callbacks, looked up after each evaluation.
    s32 tic, boot, scn, bdr, menu;

    // A trap leaves the module half way through a word: nothing more runs.
    bool broken;

    char out[LUGFORTH_OUT_SIZE];
    s32 outLen;

    // The output of the command running, for its error: the line that says
    // what went wrong has usually gone to the console already.
    char recent[LUGFORTH_OUT_SIZE];
    s32 recentLen;
} LugforthVM;

static LugforthVM* getVM(tic_core* core)
{
    return (LugforthVM*)core->currentVM;
}

// =============================================================================
// Output: TYPE goes to the console, a line at a time, and is kept for errors.
// =============================================================================

static void flushOut(LugforthVM* vm)
{
    if(vm->outLen > 0 && vm->core->data && vm->core->data->trace)
    {
        vm->out[vm->outLen] = '\0';
        vm->core->data->trace(vm->core->data->data, vm->out, 15);
    }
    vm->outLen = 0;
}

static void keepRecent(LugforthVM* vm, char c)
{
    if(vm->recentLen >= LUGFORTH_OUT_SIZE - 1)
    {
        s32 half = vm->recentLen / 2;
        memmove(vm->recent, vm->recent + half, vm->recentLen - half);
        vm->recentLen -= half;
    }

    // Lines are kept as one, without the empty ones.
    if(c == '\n')
    {
        if(vm->recentLen == 0 || vm->recent[vm->recentLen - 1] == ' ')
            return;
        c = ' ';
    }

    vm->recent[vm->recentLen++] = c;
}

static void putOut(LugforthVM* vm, char c)
{
    if(c == '\r')
        return;

    keepRecent(vm, c);

    if(c == '\n')
    {
        flushOut(vm);
        return;
    }

    vm->out[vm->outLen++] = c;
    if(vm->outLen >= LUGFORTH_OUT_SIZE - 1)
        flushOut(vm);
}

static void reportError(LugforthVM* vm, const char* what, const char* detail)
{
    tic_core* core = vm->core;
    if(!core->data)
        return;

    char buf[1024];
    while(vm->recentLen && vm->recent[vm->recentLen - 1] == ' ')
        vm->recentLen--;
    vm->recent[vm->recentLen] = '\0';
    snprintf(buf, sizeof buf, "Forth: %.200s%s%.200s%s%.255s", what,
             detail ? ": " : "", detail ? detail : "",
             vm->recentLen ? "\n" : "", vm->recent);
    vm->outLen = 0;
    vm->recentLen = 0;
    core->data->error(core->data->data, buf);
}

// =============================================================================
// Imports of the module "tic80"
// =============================================================================

static const struct
{
    const char* name;
    s32 args;
    s32 results;
} ApiWords[] =
    {
#define API(id, name, op, nargs, nres) [id] = {name, nargs, nres},
        TIC80_API_LIST
#undef API
};

enum
{
#define API(id, name, op, nargs, nres) Api_##op = id,
    TIC80_API_LIST
#undef API
        ApiCount
};

// Copies a string of the module ( a n ) out, bounded: the API takes C strings.
static const char* moduleString(IM3Runtime runtime, void* mem, u32 addr, s32 len, char* buf)
{
    u32 size = m3_GetMemorySize(runtime);
    if(len < 0 || addr > size || (u32)len > size - addr)
        return NULL;
    if(len >= LUGFORTH_STR_SIZE)
        len = LUGFORTH_STR_SIZE - 1;
    memcpy(buf, (u8*)mem + addr, len);
    buf[len] = '\0';
    return buf;
}

static s32 transparent(s32 colorkey, u8* trans)
{
    if(colorkey < 0)
        return 0;
    trans[0] = (u8)colorkey;
    return 1;
}

// api ( id base n -- results ): runs API word id on the n cells at base, the
// last one the top of the Forth stack, and writes its results from base.
static m3ApiRawFunction(lugforth_api)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(uint32_t, base);
    m3ApiGetArg(int32_t, n);
    (void)_ctx;

    LugforthVM* vm = (LugforthVM*)runtime->userdata;
    tic_core* core = vm->core;
    tic_mem* tic = (tic_mem*)core;

    if(id < 0 || id >= ApiCount || n != ApiWords[id].args)
        m3ApiTrap(m3Err_trapAbort);

    s32 cells = MAX(ApiWords[id].args, ApiWords[id].results);
    u32 size = m3_GetMemorySize(runtime);
    if((base & 3) || base > size || (u32)cells * 4 > size - base)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    u8* cell = (u8*)_mem + base;
    s32 a[18] = {0};
    for(s32 i = 0; i < n; i++)
        a[i] = (s32)m3ApiReadMem32(cell + i * 4);

    s32 r[7] = {0};
    s32 results = 0;
    char str[LUGFORTH_STR_SIZE];
    u8 trans[1];

#define RESULT(v) (r[results++] = (s32)(v))

    switch(id)
    {
        case Api_CLS:
            core->api.cls(tic, a[0]);
            break;
        case Api_PRINT:
        {
            const char* text = moduleString(runtime, _mem, a[0], a[1], str);
            if(!text)
                m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
            RESULT(core->api.print(tic, text, a[2], a[3], a[4], a[5] != 0, a[6], a[7] != 0));
            break;
        }
        case Api_PIX:
            RESULT(core->api.pix(tic, a[0], a[1], 0, true));
            break;
        case Api_PIX_STORE:
            core->api.pix(tic, a[0], a[1], a[2], false);
            break;
        case Api_LINE:
            core->api.line(tic, a[0], a[1], a[2], a[3], a[4]);
            break;
        case Api_RECT:
            core->api.rect(tic, a[0], a[1], a[2], a[3], a[4]);
            break;
        case Api_RECTB:
            core->api.rectb(tic, a[0], a[1], a[2], a[3], a[4]);
            break;
        case Api_SPR:
        {
            s32 count = transparent(a[3], trans);
            core->api.spr(tic, a[0], a[1], a[2], a[7], a[8], trans, count, a[4], a[5], a[6]);
            break;
        }
        case Api_BTN:
            RESULT(core->api.btn(tic, a[0]));
            break;
        case Api_BTNP:
            RESULT(core->api.btnp(tic, a[0], a[1], a[2]));
            break;
        case Api_SFX:
            core->api.sfx(tic, a[0], a[1], a[2], a[3], a[4], a[5], a[5], a[6]);
            break;
        case Api_MAP:
        {
            s32 count = transparent(a[6], trans);
            core->api.map(tic, a[0], a[1], a[2], a[3], a[4], a[5], trans, count, a[7], NULL, NULL);
            break;
        }
        case Api_MGET:
            RESULT(core->api.mget(tic, a[0], a[1]));
            break;
        case Api_MSET:
            core->api.mset(tic, a[0], a[1], a[2]);
            break;
        case Api_PEEK:
            RESULT(core->api.peek(tic, a[0], a[1]));
            break;
        case Api_POKE:
            core->api.poke(tic, a[0], a[1], a[2]);
            break;
        case Api_PEEK1:
            RESULT(core->api.peek1(tic, a[0]));
            break;
        case Api_POKE1:
            core->api.poke1(tic, a[0], a[1]);
            break;
        case Api_PEEK2:
            RESULT(core->api.peek2(tic, a[0]));
            break;
        case Api_POKE2:
            core->api.poke2(tic, a[0], a[1]);
            break;
        case Api_PEEK4:
            RESULT(core->api.peek4(tic, a[0]));
            break;
        case Api_POKE4:
            core->api.poke4(tic, a[0], a[1]);
            break;
        case Api_MEMCPY:
            core->api.memcpy(tic, a[0], a[1], a[2]);
            break;
        case Api_MEMSET:
            core->api.memset(tic, a[0], a[1], a[2]);
            break;
        case Api_TRACE:
        {
            const char* text = moduleString(runtime, _mem, a[0], a[1], str);
            if(!text)
                m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
            core->api.trace(tic, text, a[2]);
            break;
        }
        case Api_PMEM:
            RESULT(core->api.pmem(tic, a[0], 0, false));
            break;
        case Api_PMEM_STORE:
            core->api.pmem(tic, a[1], (u32)a[0], true);
            break;
        case Api_TIME:
            RESULT(core->api.time(tic));
            break;
        case Api_TSTAMP:
            RESULT(core->api.tstamp(tic));
            break;
        case Api_EXITGAME:
            core->api.exit(tic);
            break;
        case Api_FONT:
        {
            const char* text = moduleString(runtime, _mem, a[0], a[1], str);
            if(!text)
                m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
            trans[0] = (u8)a[4];
            RESULT(core->api.font(tic, text, a[2], a[3], trans, 1, a[5], a[6], a[7] != 0, a[8], a[9] != 0));
            break;
        }
        case Api_MOUSE:
        {
            tic_point pos = core->api.mouse(tic);
            const tic80_mouse* m = &core->memory.ram->input.mouse;
            RESULT(pos.x);
            RESULT(pos.y);
            RESULT(m->left);
            RESULT(m->middle);
            RESULT(m->right);
            RESULT(m->scrollx);
            RESULT(m->scrolly);
            break;
        }
        case Api_CIRC:
            core->api.circ(tic, a[0], a[1], a[2], a[3]);
            break;
        case Api_CIRCB:
            core->api.circb(tic, a[0], a[1], a[2], a[3]);
            break;
        case Api_ELLI:
            core->api.elli(tic, a[0], a[1], a[2], a[3], a[4]);
            break;
        case Api_ELLIB:
            core->api.ellib(tic, a[0], a[1], a[2], a[3], a[4]);
            break;
        case Api_PAINT:
            core->api.paint(tic, a[0], a[1], a[2], a[3]);
            break;
        case Api_TRI:
            core->api.tri(tic, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
            break;
        case Api_TRIB:
            core->api.trib(tic, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
            break;
        case Api_TTRI:
        {
            s32 count = transparent(a[13], trans);
            core->api.ttri(tic, a[0], a[1], a[2], a[3], a[4], a[5],
                           a[6], a[7], a[8], a[9], a[10], a[11],
                           (tic_texture_src)a[12], trans, count, a[14], a[15], a[16], a[17] != 0);
            break;
        }
        case Api_CLIP:
            core->api.clip(tic, a[0], a[1], a[2], a[3]);
            break;
        case Api_CLIP0:
            core->api.clip(tic, 0, 0, TIC80_WIDTH, TIC80_HEIGHT);
            break;
        case Api_MUSIC:
            core->api.music(tic, a[0], a[1], a[2], a[3] != 0, a[4] != 0, a[5], a[6]);
            break;
        case Api_SYNC:
            core->api.sync(tic, (u32)a[0], a[1], a[2] != 0);
            break;
        case Api_VBANK:
            RESULT(core->api.vbank(tic, a[0]));
            break;
        case Api_RESET:
            // As every other binding: the cart restarts at the next tick.
            core->state.initialized = false;
            break;
        case Api_KEYPRESSED:
            RESULT(core->api.key(tic, (tic_key)a[0]));
            break;
        case Api_KEYP:
            RESULT(core->api.keyp(tic, (tic_key)a[0], a[1], a[2]));
            break;
        case Api_FGET:
            RESULT(core->api.fget(tic, a[0], a[1]));
            break;
        case Api_FSET:
            core->api.fset(tic, a[0], a[1], a[2] != 0);
            break;
        case Api_FFT:
            RESULT(core->api.fft(tic, a[0], a[1]) * 65535.0);
            break;
        case Api_FFTS:
            RESULT(core->api.ffts(tic, a[0], a[1]) * 65535.0);
            break;
        case Api_RRECT:
            core->api.rrect(tic, a[0], a[1], a[2], a[3], a[4], a[5], (float)a[6]);
            break;
        case Api_RRECTB:
            core->api.rrectb(tic, a[0], a[1], a[2], a[3], a[4], a[5], (float)a[6]);
            break;
        case Api_BTNR:
            RESULT(core->api.btnr(tic, a[0]));
            break;
        case Api_KEYR:
            RESULT(core->api.keyr(tic, (tic_key)a[0]));
            break;
    }

#undef RESULT

    // The memory may have moved if the API ran anything that grew it: it does
    // not, but the pointer is taken again to be safe.
    cell = m3_GetMemory(runtime, NULL, 0) + base;
    for(s32 i = 0; i < results; i++)
        m3ApiWriteMem32(cell + i * 4, (u32)r[i]);

    m3ApiReturn(results);
}

// type ( a n -- ): the text of TYPE.
static m3ApiRawFunction(lugforth_type)
{
    m3ApiGetArg(uint32_t, addr);
    m3ApiGetArg(int32_t, len);
    (void)_ctx;

    LugforthVM* vm = (LugforthVM*)runtime->userdata;
    u32 size = m3_GetMemorySize(runtime);
    if(len < 0 || addr > size || (u32)len > size - addr)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    const char* text = (const char*)_mem + addr;
    for(s32 i = 0; i < len; i++)
        putOut(vm, text[i]);

    m3ApiSuccess();
}

static m3ApiRawFunction(lugforth_ticks)
{
    m3ApiReturnType(int32_t);
    (void)_ctx;
    (void)_mem;

    LugforthVM* vm = (LugforthVM*)runtime->userdata;
    m3ApiReturn((int32_t)vm->core->api.time((tic_mem*)vm->core));
}

#define MATH1(name)                          \
    static m3ApiRawFunction(lugforth_##name) \
    {                                        \
        m3ApiReturnType(double);             \
        m3ApiGetArg(double, x);              \
        (void)runtime;                       \
        (void)_ctx;                          \
        (void)_mem;                          \
        m3ApiReturn(name(x));                \
    }

#define MATH2(name)                          \
    static m3ApiRawFunction(lugforth_##name) \
    {                                        \
        m3ApiReturnType(double);             \
        m3ApiGetArg(double, x);              \
        m3ApiGetArg(double, y);              \
        (void)runtime;                       \
        (void)_ctx;                          \
        (void)_mem;                          \
        m3ApiReturn(name(x, y));             \
    }

MATH1(sin)
MATH1(cos)
MATH1(exp)
MATH1(log)
MATH2(atan2)
MATH2(pow)

static M3Result linkImports(IM3Module module)
{
    M3Result result = m3Err_none;

#define LINK(name, sig, fn)                                             \
    if((result = m3_LinkRawFunction(module, "tic80", name, sig, fn)) && \
       result != m3Err_functionLookupFailed)                            \
        return result;

    LINK("api", "i(iii)", &lugforth_api);
    LINK("type", "v(ii)", &lugforth_type);
    LINK("ticks", "i()", &lugforth_ticks);
    LINK("sin", "F(F)", &lugforth_sin);
    LINK("cos", "F(F)", &lugforth_cos);
    LINK("exp", "F(F)", &lugforth_exp);
    LINK("log", "F(F)", &lugforth_log);
    LINK("atan2", "F(FF)", &lugforth_atan2);
    LINK("pow", "F(FF)", &lugforth_pow);

#undef LINK

    return m3Err_none;
}

// =============================================================================
// Calls into the module
// =============================================================================

// Runs an export that returns a cell; on a trap, says so and stops the module.
static bool callModule(LugforthVM* vm, IM3Function fn, s32* ret, const char* what, s32 argc, s32 a, s32 b)
{
    if(vm->broken)
        return false;

    M3Result result = argc == 0   ? m3_CallV(fn)
                      : argc == 1 ? m3_CallV(fn, a)
                                  : m3_CallV(fn, a, b);

    if(!result && ret)
        result = m3_GetResultsV(fn, ret);

    if(result)
    {
        vm->broken = true;
        reportError(vm, what, result);
        return false;
    }

    return true;
}

// Copies a text into the module: its address there, or 0.
static s32 toModule(LugforthVM* vm, const char* text, s32 len)
{
    s32 addr = 0;
    if(!callModule(vm, vm->alloc, &addr, "out of memory", 1, len ? len : 1, 0) || !addr)
        return 0;

    u32 size = 0;
    u8* mem = m3_GetMemory(vm->runtime, &size, 0);
    if((u32)addr > size || (u32)len > size - addr)
        return 0;

    memcpy(mem + addr, text, len);
    return addr;
}

static s32 findWord(LugforthVM* vm, const char* name)
{
    s32 len = (s32)strlen(name);
    s32 addr = toModule(vm, name, len);
    if(!addr)
        return 0;

    s32 xt = 0;
    callModule(vm, vm->find, &xt, "find", 2, addr, len);
    callModule(vm, vm->free, NULL, "free", 1, addr, 0);
    return xt;
}

static void findCallbacks(LugforthVM* vm)
{
    vm->tic = findWord(vm, TIC_FN);
    vm->boot = findWord(vm, BOOT_FN);
    vm->scn = findWord(vm, SCN_FN);
    vm->bdr = findWord(vm, BDR_FN);
    vm->menu = findWord(vm, MENU_FN);

#if defined(BUILD_RENDER_CACHE)
    vm->core->state.has_scn = vm->scn != 0;
    vm->core->state.has_bdr = vm->bdr != 0;
#endif
}

static bool evaluate(LugforthVM* vm, const char* code)
{
    s32 len = (s32)strlen(code);
    s32 addr = toModule(vm, code, len);
    if(!addr)
    {
        if(!vm->broken)
            reportError(vm, "out of memory", NULL);
        return false;
    }

    s32 thrown = 0;
    vm->recentLen = 0;
    bool ok = callModule(vm, vm->eval, &thrown, "evaluate", 2, addr, len);
    if(ok)
        callModule(vm, vm->free, NULL, "free", 1, addr, 0);

    if(ok && thrown)
    {
        char code[32];
        snprintf(code, sizeof code, "THROW %d", (int)thrown);
        reportError(vm, code, NULL);
        return false;
    }

    flushOut(vm);
    return ok;
}

// Runs a callback; a word that leaves or takes cells is an error, as in the
// pForth binding (the module empties the stack before each call anyway).
static void callWord(tic_mem* tic, s32 xt, const char* name, s32 param, bool hasParam)
{
    tic_core* core = (tic_core*)tic;
    LugforthVM* vm = getVM(core);
    if(!vm || !xt || vm->broken)
        return;

    s32 thrown = 0;
    vm->recentLen = 0;
    bool ok = hasParam
                  ? callModule(vm, vm->call1, &thrown, name, 2, xt, param)
                  : callModule(vm, vm->call, &thrown, name, 1, xt, 0);

    if(!ok)
        return;

    if(thrown)
    {
        char what[64];
        snprintf(what, sizeof what, "THROW %d in %s", (int)thrown, name);
        reportError(vm, what, NULL);
        return;
    }

    s32 depth = 0;
    if(callModule(vm, vm->depth, &depth, "depth", 0, 0, 0) && depth != 0)
    {
        char what[128];
        snprintf(what, sizeof what, "stack %s in %s (%d item%s %s)",
                 depth > 0 ? "overflow" : "underflow", name, (int)abs(depth),
                 abs(depth) == 1 ? "" : "s", depth > 0 ? "extra left" : "missing");
        reportError(vm, what, NULL);
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

static void closeLugforth(tic_mem* tic)
{
    tic_core* core = (tic_core*)tic;
    LugforthVM* vm = getVM(core);

    if(vm)
    {
        flushOut(vm);
        if(vm->runtime)
            m3_FreeRuntime(vm->runtime);
        if(vm->env)
            m3_FreeEnvironment(vm->env);
        free(vm);
        core->currentVM = NULL;
    }
}

static bool initLugforth(tic_mem* tic, const char* code)
{
    tic_core* core = (tic_core*)tic;
    closeLugforth(tic);

    LugforthVM* vm = calloc(1, sizeof(LugforthVM));
    if(!vm)
        return false;

    vm->core = core;
    core->currentVM = vm;

    const char* failed = NULL;
    M3Result result = m3Err_none;
    IM3Module module = NULL;

    if(!(vm->env = m3_NewEnvironment()))
        failed = "cannot create the wasm environment";
    else if(!(vm->runtime = m3_NewRuntime(vm->env, LUGFORTH_STACK_SIZE, vm)))
        failed = "cannot create the wasm runtime";
    else if((result = m3_ParseModule(vm->env, &module, LugforthModule, sizeof LugforthModule)))
        failed = result;
    else if((result = m3_LoadModule(vm->runtime, module)))
    {
        m3_FreeModule(module);
        failed = result;
    }
    else if((result = linkImports(module)))
        failed = result;
    else
        // The load sets the limit the module declares; ours is lower.
        vm->runtime->memory.maxPages = MIN(vm->runtime->memory.maxPages, LUGFORTH_MAX_PAGES);

    struct
    {
        IM3Function* fn;
        const char* name;
    } exports[] =
        {
            {&vm->init, "lf_init"},
            {&vm->alloc, "lf_alloc"},
            {&vm->free, "lf_free"},
            {&vm->eval, "lf_eval"},
            {&vm->find, "lf_find"},
            {&vm->call, "lf_call"},
            {&vm->call1, "lf_call1"},
            {&vm->depth, "lf_depth"},
        };

    for(s32 i = 0; !failed && i < COUNT_OF(exports); i++)
        if((result = m3_FindFunction(exports[i].fn, vm->runtime, exports[i].name)))
            failed = result;

    if(failed)
    {
        reportError(vm, "cannot load the Lugforth module", failed);
        closeLugforth(tic);
        return false;
    }

    s32 ready = -1;
    if(!callModule(vm, vm->init, &ready, "boot", 0, 0, 0) || ready != 0)
    {
        if(!vm->broken)
            reportError(vm, "the boot of Lugforth failed", NULL);
        closeLugforth(tic);
        return false;
    }

    if(!evaluate(vm, code))
    {
        closeLugforth(tic);
        return false;
    }

    findCallbacks(vm);
    return true;
}

static void callLugforthTick(tic_mem* tic)
{
    LugforthVM* vm = getVM((tic_core*)tic);
    if(!vm)
        return;

    callWord(tic, vm->tic, TIC_FN, 0, false);
    // '.' output that has no line end yet.
    flushOut(vm);
}

static void callLugforthBoot(tic_mem* tic)
{
    LugforthVM* vm = getVM((tic_core*)tic);
    if(vm)
        callWord(tic, vm->boot, BOOT_FN, 0, false);
}

static void callLugforthScanline(tic_mem* tic, s32 row, void* data)
{
    (void)data;
    LugforthVM* vm = getVM((tic_core*)tic);
    if(vm)
        callWord(tic, vm->scn, SCN_FN, row, true);
}

static void callLugforthBorder(tic_mem* tic, s32 row, void* data)
{
    (void)data;
    LugforthVM* vm = getVM((tic_core*)tic);
    if(vm)
        callWord(tic, vm->bdr, BDR_FN, row, true);
}

static void callLugforthMenu(tic_mem* tic, s32 index, void* data)
{
    (void)data;
    LugforthVM* vm = getVM((tic_core*)tic);
    if(vm)
        callWord(tic, vm->menu, MENU_FN, index, true);
}

static void evalLugforth(tic_mem* tic, const char* code)
{
    tic_core* core = (tic_core*)tic;
    if(!getVM(core) && !initLugforth(tic, ""))
        return;

    LugforthVM* vm = getVM(core);
    if(evaluate(vm, code))
        findCallbacks(vm);
}

// =============================================================================
// Editor support
// =============================================================================

// Lines that start with ': NAME' are the outline.
static const tic_outline_item* getLugforthOutline(const char* code, s32* size)
{
    static tic_outline_item items[128];
    *size = 0;

    for(const char* p = code; *p;)
    {
        if(*p == ':' && (p == code || p[-1] == '\n'))
        {
            p++;
            while(*p == ' ' || *p == '\t')
                p++;

            const char* start = p;
            while(*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                p++;

            if(p > start && *size < COUNT_OF(items))
            {
                items[*size].pos = start;
                items[*size].size = (s32)(p - start);
                (*size)++;
            }
        }

        while(*p && *p != '\n')
            p++;
        if(*p)
            p++;
    }

    return *size ? items : NULL;
}

static const char* const LugforthKeywords[] =
    {
        "IF",
        "THEN",
        "ELSE",
        "BEGIN",
        "UNTIL",
        "WHILE",
        "REPEAT",
        "AGAIN",
        "DO",
        "?DO",
        "LOOP",
        "+LOOP",
        "LEAVE",
        "UNLOOP",
        "I",
        "J",
        "EXIT",
        "RECURSE",
        "CASE",
        "OF",
        "ENDOF",
        "ENDCASE",
        ":",
        ";",
        ":NONAME",
        "VARIABLE",
        "CONSTANT",
        "VALUE",
        "TO",
        "CREATE",
        "DOES>",
        "DEFER",
        "IS",
        "IMMEDIATE",
        "POSTPONE",
        "LITERAL",
        "[",
        "]",
        "DUP",
        "DROP",
        "SWAP",
        "OVER",
        "ROT",
        "-ROT",
        "NIP",
        "TUCK",
        "PICK",
        "ROLL",
        "2DUP",
        "2DROP",
        "2SWAP",
        "2OVER",
        ">R",
        "R>",
        "R@",
        "+",
        "-",
        "*",
        "/",
        "MOD",
        "/MOD",
        "*/",
        "*/MOD",
        "MAX",
        "MIN",
        "ABS",
        "NEGATE",
        "1+",
        "1-",
        "2*",
        "2/",
        "AND",
        "OR",
        "XOR",
        "INVERT",
        "LSHIFT",
        "RSHIFT",
        "=",
        "<>",
        "<",
        ">",
        "<=",
        ">=",
        "0=",
        "0<",
        "0>",
        "@",
        "!",
        "C@",
        "C!",
        "+!",
        "MOVE",
        "FILL",
        ".",
        ".\"",
        "EMIT",
        "CR",
        "SPACE",
        "SPACES",
        "TYPE",
        "S\"",
        "C\"",
        "CHAR",
        "[CHAR]",
        "TRUE",
        "FALSE",
        "CELL",
        "CELLS",
        "HERE",
        "ALLOT",
        ",",
        "CATCH",
        "THROW",
        "{:",
        ":}",
};

static const char* LugforthApiKeywords[] =
    {
#define TIC_CALLBACK_DEF(name, ...) #name,
        TIC_CALLBACK_LIST(TIC_CALLBACK_DEF)
#undef TIC_CALLBACK_DEF
#define API(id, name, op, nargs, nres) name,
            TIC80_API_LIST
#undef API
};

static const u8 DemoRom[] =
    {
#include "../build/assets/forthdemo.tic.dat"
};

static const u8 MarkRom[] =
    {
#include "../build/assets/forthmark.tic.dat"
};

// The same id, name and extension as the pForth binding: a Forth cartridge
// runs on either build.
TIC_EXPORT const tic_script EXPORT_SCRIPT(Forth) =
    {
        .id = 22,
        .name = "forth",
        .fileExtension = ".fth",
        .projectComment = "\\",
        {
            .init = initLugforth,
            .close = closeLugforth,
            .tick = callLugforthTick,
            .boot = callLugforthBoot,
            .callback =
                {
                    .scanline = callLugforthScanline,
                    .border = callLugforthBorder,
                    .menu = callLugforthMenu,
                },
        },
        .getOutline = getLugforthOutline,
        .eval = evalLugforth,

        .blockCommentStart = NULL,
        .blockCommentEnd = NULL,
        .blockCommentStart2 = NULL,
        .blockCommentEnd2 = NULL,
        .singleComment = "\\",
        .blockStringStart = NULL,
        .blockStringEnd = NULL,
        .stdStringStartEnd = NULL,
        .blockEnd = NULL,

        .keywords = LugforthKeywords,
        .keywordsCount = COUNT_OF(LugforthKeywords),

        .api_keywords = LugforthApiKeywords,
        .api_keywordsCount = COUNT_OF(LugforthApiKeywords),

        .demo = {DemoRom, sizeof DemoRom, "forthdemo.tic"},
        .mark = {MarkRom, sizeof MarkRom, "forthmark.tic"},
};
