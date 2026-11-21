/*
 * testhook.c - TEST ONLY support for intercepting calls between modules in
 * the shared test DLL (tests/base_sources.txt).  Not part of the game build.
 *
 * Every module is linked with its real callees; a test that must replace a
 * callee (to log the call, to stop a game loop, to supply a result) patches
 * the callee's entry from Python (lale.patch(name, ...): a jump to a ctypes
 * callback or to one of the C helpers below) and restores it afterwards.
 *
 * Leaving a never-ending routine (ist_main, the exploration loop) needs a
 * non-local exit that never crosses a Python frame:
 *   th_run(fn, a, b, c, d)  calls fn(a, b, c, d); returns 0 when fn returns,
 *                           1 when th_stop()/th_tick() jumped out of it;
 *   th_stop()               longjmp back to th_run;
 *   th_tick(arg)            calls th_tick_hook(arg) (if set, e.g. a Python
 *                           logger) and leaves through th_stop() on the
 *                           th_tick_left-th call (<= 0 never stops).
 * Patch the callee to th_tick (or th_stop) directly, so that the jump
 * starts in C.
 */
#include <setjmp.h>
#include <stdint.h>

typedef intptr_t (*ThFn)(intptr_t, intptr_t, intptr_t, intptr_t);
typedef void (*ThHook)(uint32_t arg);

ThHook  th_tick_hook;
int32_t th_tick_left;
int32_t th_tick_count;

static jmp_buf th_jb;
static int th_armed;

int th_run(void *fn, intptr_t a, intptr_t b, intptr_t c, intptr_t d)
{
    th_tick_count = 0;
    if (setjmp(th_jb)) {
        th_armed = 0;
        return 1;
    }
    th_armed = 1;
    ((ThFn)fn)(a, b, c, d);
    th_armed = 0;
    return 0;
}

void th_stop(void)
{
    if (th_armed)
        longjmp(th_jb, 1);
}

void th_tick(uint32_t arg)
{
    th_tick_count++;
    if (th_tick_hook)
        th_tick_hook(arg);
    if (th_tick_left > 0 && --th_tick_left == 0)
        th_stop();
}
