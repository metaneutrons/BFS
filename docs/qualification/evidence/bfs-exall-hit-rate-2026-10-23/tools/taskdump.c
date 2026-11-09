/* SPDX-License-Identifier: MPL-2.0 */
/* taskdump
 *
 * Diagnostic: lists the ROM modules with their address ranges and every task
 * that waits or is ready, with its wait signals and the ROM modules that its
 * stack points into, then the Intuition screens and windows with their titles.
 * Used to see what a hung command is blocked on, for example a requester. */
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/tasks.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/exec.h>

static ULONG length(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }
static void emit(const char *s) { Write(Output(), (APTR)s, length(s)); }
static void hex(unsigned long v)
{
    static const char digits[] = "0123456789abcdef";
    char out[9];
    for (int i = 0; i < 8; i++) out[i] = digits[(v >> (28 - 4 * i)) & 15];
    out[8] = 0;
    emit(out);
}

static const char *module_of(struct ExecBase *sys, ULONG address)
{
    ULONG *list = (ULONG *)sys->ResModules;
    for (; *list; list++) {
        if (*list & 0x80000000UL) list = (ULONG *)(*list & 0x7fffffffUL);
        if (!*list) break;
        struct Resident *r = (struct Resident *)*list;
        if (address >= (ULONG)r && address < (ULONG)r->rt_EndSkip) return r->rt_Name;
    }
    return 0;
}

static void dump_list(struct ExecBase *sys, struct List *tasks, const char *title)
{
    emit(title);
    for (struct Node *n = tasks->lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct Task *t = (struct Task *)n;
        emit("task "); emit(n->ln_Name ? n->ln_Name : "?");
        emit(" type="); hex(n->ln_Type); emit(" state="); hex(t->tc_State);
        emit(" sigwait="); hex(t->tc_SigWait); emit("\n");
        ULONG *sp = (ULONG *)t->tc_SPReg;
        for (int i = 0; i < 48; i++) {
            if ((ULONG)(sp + i) >= (ULONG)t->tc_SPUpper) break;
            const char *m = module_of(sys, sp[i]);
            if (m) { emit("  "); hex(sp[i]); emit(" in "); emit(m); emit("\n"); }
        }
    }
}

int main(void)
{
    struct ExecBase *sys = *(struct ExecBase **)4;
    emit("-- modules\n");
    ULONG *list = (ULONG *)sys->ResModules;
    for (; *list; list++) {
        if (*list & 0x80000000UL) list = (ULONG *)(*list & 0x7fffffffUL);
        if (!*list) break;
        struct Resident *r = (struct Resident *)*list;
        hex((ULONG)r); emit(" "); hex((ULONG)r->rt_EndSkip); emit(" "); emit(r->rt_Name); emit("\n");
    }
    Forbid();
    dump_list(sys, &sys->TaskWait, "-- waiting\n");
    dump_list(sys, &sys->TaskReady, "-- ready\n");
    Permit();
    emit("-- screens and windows\n");
    struct IntuitionBase *ib = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 0);
    if (ib) {
        ULONG lock = LockIBase(0);
        for (struct Screen *sc = ib->FirstScreen; sc; sc = sc->NextScreen) {
            emit("screen "); emit(sc->Title ? (const char *)sc->Title : "(no title)");
            emit(" default="); emit(sc->DefaultTitle ? (const char *)sc->DefaultTitle : "?"); emit("\n");
            for (struct Window *w = sc->FirstWindow; w; w = w->NextWindow) {
                emit("  window "); emit(w->Title ? (const char *)w->Title : "(no title)"); emit("\n");
            }
        }
        UnlockIBase(lock);
        CloseLibrary((struct Library *)ib);
    }
    return 0;
}
