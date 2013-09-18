/*  GRAPHITE2 LICENSING

    Copyright 2010, SIL International
    All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should also have received a copy of the GNU Lesser General Public
    License along with this library in the file named "LICENSE".
    If not, write to the Free Software Foundation, 51 Franklin Street,
    suite 500, Boston, MA 02110-1335, USA or visit their web page on the 
    internet at http://www.fsf.org/licenses/lgpl.html.

Alternatively, the contents of this file may be used under the terms of the
Mozilla Public License (http://mozilla.org/MPL) or the GNU General Public
License, as published by the Free Software Foundation, either version 2
of the License or (at your option) any later version.
*/
#include "inc/Main.h"
#include "inc/Slot.h"
#include "inc/Segment.h"

using namespace graphite2;

enum DirCode {  // Hungarian: dirc
        Unk        = -1,
        N          =  0,   // other neutrals (default) - ON
        L          =  1,   // left-to-right, strong - L
        R          =  2,   // right-to-left, strong - R
        AL         =  3,   // Arabic letter, right-to-left, strong, AR
        EN         =  4,   // European number, left-to-right, weak - EN
        EUS        =  5,   // European separator, left-to-right, weak - ES
        ET         =  6,   // European number terminator, left-to-right, weak - ET
        AN         =  7,   // Arabic number, left-to-right, weak - AN
        CUS        =  8,   // Common number separator, left-to-right, weak - CS
        WS         =  9,   // white space, neutral - WS
        BN         = 10,   // boundary neutral - BN

        LRO        = 11,   // LTR override
        RLO        = 12,   // RTL override
        LRE        = 13,   // LTR embedding
        RLE        = 14,   // RTL embedding
        PDF        = 15,   // pop directional format
        NSM        = 16,   // non-space mark
        LRI        = 17,   // LRI isolate
        RLI        = 18,   // RLI isolate
        FSI        = 19,   // FSI isolate
        PDI        = 20,   // pop isolate

        ON = N
};

enum DirMask {
        WSflag = (1 << 7),     // keep track of WS for eos handling
        WSMask = ~(1 << 7)
};

inline uint8    BaseClass(Slot *s)                      { return s->getBidiClass() & WSMask; }

unsigned int bidi_class_map[] = { 0, 1, 2, 5, 4, 8, 9, 3, 7, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0 };
// Algorithms based on Unicode reference standard code. Thanks Asmus Freitag.

void resolveWeak(Slot *start, int baseLevel, int sos, int eos);
void resolveNeutrals(Slot *s, int baseLevel, int sos, int eos);

inline int calc_base_level(Slot *s)
{
    int count = 0;
    for ( ; s; s = s->next())
    {
        int cls = s->getBidiClass();
        if (count)
        {
            switch(cls)
            {
            case LRI :
            case RLI :
            case FSI :
                ++count;
            case PDI :
                --count;
            }
        }
        else
        {
            switch(cls)
            {
            case L :
                return 0;
            case R :
            case AL :
                return 1;
            case LRI :
            case RLI :
            case FSI :
                ++count;
            }
        }
    }
    return 0;
}

// inline or not?
void do_resolves(Slot *start, int level, int sos, int eos, int isol, Slot *pstart, int &bmask)
{
    if (bmask & 0x1F1178)
        resolveWeak(start, level, sos, eos);
    // do bracket pairing here
    if (bmask & 0x1E0361)
        resolveNeutrals(start, level, sos, eos);
    bmask = 0;
}

enum maxs
{
    // MAX_LEVEL = 61,
    MAX_LEVEL = 125,
};

// returns where we are up to in processing
Slot *process_bidi(Slot *start, int level, int prelevel, int &nextLevel, int dirover, int isol, int &cisol, int &isolerr, int &embederr, int init)
{
    int bmask = 0;
    Slot *s = start;
    Slot *slast = start;
    Slot *scurr = 0;
    Slot *stemp;
    int lnextLevel = nextLevel;
    int newLevel;
    int empty = 1;
    for ( ; s; s = s ? s->next() : s)
    {
        int cls = s->getBidiClass();
        bmask |= (1 << cls);
        s->setBidiLevel(level);
        // we keep s->prev() pointing backwards for PDI repeating
        
        switch (cls)
        {
        case BN :
            if (slast == s) slast = s->next();
            continue;
        case LRE :
        case LRO :
        case RLE :
        case RLO :
            switch (cls)
            {
            case LRE :
            case LRO :
                newLevel = level + (level & 1 ? 1 : 2);
                break;
            case RLE :
            case RLO :
                newLevel = level + (level & 1 ? 2 : 1);
                break;
            }
            s->setBidiClass(BN);
            if (isolerr || newLevel > MAX_LEVEL || embederr)
            {
                if (!isolerr) ++embederr;
                break;
            }
            stemp = scurr;
            if (scurr)
                scurr->prev(0);         // don't include control in string
            lnextLevel = newLevel;
            scurr = s;
            s->setBidiLevel(newLevel); // to make it vanish
            s = process_bidi(s->next(), newLevel, level, lnextLevel, cls < LRE, 0, cisol, isolerr, embederr, 0);
            // s points at PDF or end of sequence
            // try to keep extending the run and not process it until we have to
            if (lnextLevel != level || !s)      // if at end of run
            {
                if (slast != scurr)             // process the run now, don't try to extend it
                {
                    // process text preceeding embedding
                    do_resolves(slast, level, (prelevel > level ? prelevel : level) & 1, lnextLevel & 1, isol, start, bmask);
                    empty = 0;
                    nextLevel = level;
                }
                else if (lnextLevel != level)   // the embedding had something
                {
                    empty = 0;                  // so we aren't empty either
                    nextLevel = lnextLevel;     // but pass back from the embedded
                }
                if (s) 
                {
                    prelevel = lnextLevel;
                    lnextLevel = level;
                }
                slast = s ? s->next() : s;
            }
            else if (stemp)
                stemp->prev(s);
            break;

        case PDF :
            s->setBidiClass(BN);
            s->prev(0);
            if (isol || isolerr || init)
                break;
            if (embederr)
            {
                --embederr;
                break;
            }
            if (slast != s)
            {
                scurr->prev(0);     // if slast, then scurr
                do_resolves(slast, level, level & 1, level & 1, isol, start, bmask);
                empty = 0;
            }
            if (empty)
            {
                nextLevel = prelevel;
                s->setBidiLevel(prelevel);
            }
            return s;

        case FSI :
        case LRI :
        case RLI :
            switch (cls)
            {
            case FSI :
                if (calc_base_level(s->next()))
                    newLevel = level + (level & 1 ? 2 : 1);
                else
                    newLevel = level + (level & 1 ? 1 : 2);
                break;
            case LRI :
                newLevel = level + (level & 1 ? 1 : 2);
                break;
            case RLI :
                newLevel = level + (level & 1 ? 2 : 1);
                break;
            }
            if (newLevel > MAX_LEVEL || isolerr)
            {
                ++isolerr;
                s->setBidiClass(ON | WSflag);
                break;
            }
            ++cisol;
            if (scurr) scurr->prev(s);
            scurr = s;  // include FSI
            lnextLevel = newLevel;
            s = process_bidi(s->next(), newLevel, newLevel, lnextLevel, 0, 1, cisol, isolerr, embederr, 0);
            // s points at PDI
            if (s)
            {
                bmask |= 1 << BaseClass(s);
                s->setBidiLevel(level);
                // prelevel = s->getBidiLevel();
            }
            lnextLevel = level;
            break;

        case PDI :
            if (isolerr)
            {
                --isolerr;
                s->setBidiClass(ON | WSflag);
                break;
            }
            if (init || !cisol)
            {
                s->setBidiClass(ON | WSflag);
                break;
            }
            embederr = 0;
            if (!isol)
            {
                if (empty)
                {
                    nextLevel = prelevel;
                    //s->setBidiLevel(prelevel);
                }
                return s->prev();       // keep working up the stack pointing at this PDI until we get to an isolate entry
            }
            else
            {
                if (slast != s)
                {
                    scurr->prev(0);
                    do_resolves(slast, level, prelevel & 1, level & 1, isol, start, bmask);
                    empty = 0;
                }
                //s->setBidiClass(ON | WSflag);    // no special treatment in final stages
                --cisol;
                //s->setBidiLevel(prelevel);
                return s;
            }

        default :
            if (dirover)
                s->setBidiClass((level & 1 ? R : L) | (WSflag * (cls == WS)));
        }
        if (s) s->prev(0);
        if (scurr)
            scurr->prev(s);
        scurr = s;
    }
    if (slast != s)
    {
        do_resolves(slast, level, (level > prelevel ? level : prelevel) & 1, lnextLevel & 1, isol, start, bmask);
        empty = 0;
    }
    if (empty || isol)
        nextLevel = prelevel;
    return s;
}

// === RESOLVE WEAK TYPES ================================================

enum bidi_state // possible states
{
        xa,             //      arabic letter
        xr,             //      right leter
        xl,             //      left letter

        ao,             //      arabic lett. foll by ON
        ro,             //      right lett. foll by ON
        lo,             //      left lett. foll by ON

        rt,             //      ET following R
        lt,             //      ET following L

        cn,             //      EN, AN following AL
        ra,             //      arabic number foll R
        re,             //      european number foll R
        la,             //      arabic number foll L
        le,             //      european number foll L

        ac,             //      CS following cn
        rc,             //      CS following ra
        rs,             //      CS,ES following re
        lc,             //      CS following la
        ls,             //      CS,ES following le

        ret,            //      ET following re
        let,            //      ET following le
} ;

enum bidi_state_mask
{
    xamask = 1,
    xrmask = 2,
    xlmask = 4,
    aomask = 8,
    romask = 0x10,
    lomask = 0x20,
    rtmask = 0x40,
    ltmask = 0x80,
    cnmask = 0x100,
    ramask = 0x200,
    remask = 0x400,
    lamask = 0x800,
    lemask = 0x1000,
    acmask = 0x2000,
    rcmask = 0x4000,
    rsmask = 0x8000,
    lcmask = 0x10000,
    lsmask = 0x20000,
    retmask = 0x40000,
    letmask = 0x80000
};

const bidi_state stateWeak[][10] =
{
        //      N,  L,  R,  AN, EN, AL,NSM, CS, ES, ET,
{ /*xa*/        ao, xl, xr, cn, cn, xa, xa, ao, ao, ao, /* arabic letter          */ },
{ /*xr*/        ro, xl, xr, ra, re, xa, xr, ro, ro, rt, /* right letter           */ },
{ /*xl*/        lo, xl, xr, la, le, xa, xl, lo, lo, lt, /* left letter            */ },

{ /*ao*/        ao, xl, xr, cn, cn, xa, ao, ao, ao, ao, /* arabic lett. foll by ON*/ },
{ /*ro*/        ro, xl, xr, ra, re, xa, ro, ro, ro, rt, /* right lett. foll by ON */ },
{ /*lo*/        lo, xl, xr, la, le, xa, lo, lo, lo, lt, /* left lett. foll by ON  */ },

{ /*rt*/        ro, xl, xr, ra, re, xa, rt, ro, ro, rt, /* ET following R         */ },
{ /*lt*/        lo, xl, xr, la, le, xa, lt, lo, lo, lt, /* ET following L         */ },

{ /*cn*/        ao, xl, xr, cn, cn, xa, cn, ac, ao, ao, /* EN, AN following AL    */ },
{ /*ra*/        ro, xl, xr, ra, re, xa, ra, rc, ro, rt, /* arabic number foll R   */ },
{ /*re*/        ro, xl, xr, ra, re, xa, re, rs, rs,ret, /* european number foll R */ },
{ /*la*/        lo, xl, xr, la, le, xa, la, lc, lo, lt, /* arabic number foll L   */ },
{ /*le*/        lo, xl, xr, la, le, xa, le, ls, ls,let, /* european number foll L */ },

{ /*ac*/        ao, xl, xr, cn, cn, xa, ao, ao, ao, ao, /* CS following cn        */ },
{ /*rc*/        ro, xl, xr, ra, re, xa, ro, ro, ro, rt, /* CS following ra        */ },
{ /*rs*/        ro, xl, xr, ra, re, xa, ro, ro, ro, rt, /* CS,ES following re     */ },
{ /*lc*/        lo, xl, xr, la, le, xa, lo, lo, lo, lt, /* CS following la        */ },
{ /*ls*/        lo, xl, xr, la, le, xa, lo, lo, lo, lt, /* CS,ES following le     */ },

{ /*ret*/       ro, xl, xr, ra, re, xa,ret, ro, ro,ret, /* ET following re        */ },
{ /*let*/       lo, xl, xr, la, le, xa,let, lo, lo,let, /* ET following le        */ },


};

enum bidi_action // possible actions
{
        // primitives
        IX = 0x100,                     // increment
        XX = 0xF,                       // no-op

        // actions
        xxx = (XX << 4) + XX,           // no-op
        xIx = IX + xxx,                         // increment run
        xxN = (XX << 4) + ON,           // set current to N
        xxE = (XX << 4) + EN,           // set current to EN
        xxA = (XX << 4) + AN,           // set current to AN
        xxR = (XX << 4) + R,            // set current to R
        xxL = (XX << 4) + L,            // set current to L
        Nxx = (ON << 4) + 0xF,          // set run to neutral
        Axx = (AN << 4) + 0xF,          // set run to AN
        ExE = (EN << 4) + EN,           // set run to EN, set current to EN
        NIx = (ON << 4) + 0xF + IX,     // set run to N, increment
        NxN = (ON << 4) + ON,           // set run to N, set current to N
        NxR = (ON << 4) + R,            // set run to N, set current to R
        NxE = (ON << 4) + EN,           // set run to N, set current to EN

        AxA = (AN << 4) + AN,           // set run to AN, set current to AN
        NxL = (ON << 4) + L,            // set run to N, set current to L
        LxL = (L << 4) + L,             // set run to L, set current to L
};


const bidi_action actionWeak[][10] =
{
    //   N,.. L,   R,   AN,  EN,  AL,  NSM, CS,..ES,  ET,
{ /*xa*/ xxx, xxx, xxx, xxx, xxA, xxR, xxR, xxN, xxN, xxN, /* arabic letter             */ },
{ /*xr*/ xxx, xxx, xxx, xxx, xxE, xxR, xxR, xxN, xxN, xIx, /* right leter               */ },
{ /*xl*/ xxx, xxx, xxx, xxx, xxL, xxR, xxL, xxN, xxN, xIx, /* left letter               */ },

{ /*ao*/ xxx, xxx, xxx, xxx, xxA, xxR, xxN, xxN, xxN, xxN, /* arabic lett. foll by ON   */ },
{ /*ro*/ xxx, xxx, xxx, xxx, xxE, xxR, xxN, xxN, xxN, xIx, /* right lett. foll by ON    */ },
{ /*lo*/ xxx, xxx, xxx, xxx, xxL, xxR, xxN, xxN, xxN, xIx, /* left lett. foll by ON     */ },

{ /*rt*/ Nxx, Nxx, Nxx, Nxx, ExE, NxR, xIx, NxN, NxN, xIx, /* ET following R            */ },
{ /*lt*/ Nxx, Nxx, Nxx, Nxx, LxL, NxR, xIx, NxN, NxN, xIx, /* ET following L            */ },

{ /*cn*/ xxx, xxx, xxx, xxx, xxA, xxR, xxA, xIx, xxN, xxN, /* EN, AN following  AL      */ },
{ /*ra*/ xxx, xxx, xxx, xxx, xxE, xxR, xxA, xIx, xxN, xIx, /* arabic number foll R      */ },
{ /*re*/ xxx, xxx, xxx, xxx, xxE, xxR, xxE, xIx, xIx, xxE, /* european number foll R    */ },
{ /*la*/ xxx, xxx, xxx, xxx, xxL, xxR, xxA, xIx, xxN, xIx, /* arabic number foll L      */ },
{ /*le*/ xxx, xxx, xxx, xxx, xxL, xxR, xxL, xIx, xIx, xxL, /* european number foll L    */ },

{ /*ac*/ Nxx, Nxx, Nxx, Axx, AxA, NxR, NxN, NxN, NxN, NxN, /* CS following cn           */ },
{ /*rc*/ Nxx, Nxx, Nxx, Axx, NxE, NxR, NxN, NxN, NxN, NIx, /* CS following ra           */ },
{ /*rs*/ Nxx, Nxx, Nxx, Nxx, ExE, NxR, NxN, NxN, NxN, NIx, /* CS,ES following re        */ },
{ /*lc*/ Nxx, Nxx, Nxx, Axx, NxL, NxR, NxN, NxN, NxN, NIx, /* CS following la           */ },
{ /*ls*/ Nxx, Nxx, Nxx, Nxx, LxL, NxR, NxN, NxN, NxN, NIx, /* CS,ES following le        */ },

{ /*ret*/xxx, xxx, xxx, xxx, xxE, xxR, xxE, xxN, xxN, xxE, /* ET following re           */ },
{ /*let*/xxx, xxx, xxx, xxx, xxL, xxR, xxL, xxN, xxN, xxL, /* ET following le           */ },
};

inline uint8    GetDeferredType(bidi_action a)          { return (a >> 4) & 0xF; }
inline uint8    GetResolvedType(bidi_action a)          { return a & 0xF; }
inline DirCode  EmbeddingDirection(int l)               { return l & 1 ? R : L; }

// Neutrals
enum neutral_action
{
        // action to resolve previous input
        nL = L,         // resolve EN to L
        En = 3 << 4,    // resolve neutrals run to embedding level direction
        Rn = R << 4,    // resolve neutrals run to strong right
        Ln = L << 4,    // resolved neutrals run to strong left
        In = (1<<8),    // increment count of deferred neutrals
        LnL = (1<<4)+L, // set run and EN to L
};

// ->prev() here means ->next()
void SetDeferredRunClass(Slot *s, Slot *sRun, int nval)
{
    if (!sRun || s == sRun) return;
    for (Slot *p = sRun; p != s; p = p->prev())
        if (p->getBidiClass() == WS) p->setBidiClass(nval | WSflag);
        else if (BaseClass(p) != BN) p->setBidiClass(nval | (p->getBidiClass() & WSflag));
}

void SetThisDeferredRunClass(Slot *s, Slot *sRun, int nval)
{
    if (!sRun) return;
    for (Slot *p = sRun, *e = s->prev(); p != e; p = p->prev())
        if (p->getBidiClass() == WS) p->setBidiClass(nval | WSflag);
        else if (BaseClass(p) != BN) p->setBidiClass(nval | (p->getBidiClass() & WSflag));
}

void resolveWeak(Slot *start, int baseLevel, int sos, int eos)
{
    int state = (sos & 1) ? xr : xl;
    int cls;
    //int level = baseLevel;
    Slot *s = start;
    Slot *sRun = NULL;
    Slot *sLast = s;

    for ( ; s; s = s->prev())
    {
        sLast = s;
        cls = BaseClass(s);
        switch (cls)
        {
        case BN :
            if (s == start) start = s->prev();  // skip initial BNs for NSM resolving
            continue;
        case LRI :
        case RLI :
        case FSI :
        case PDI :
            {
                Slot *snext = s->prev();
                if (snext && snext->getBidiClass() == NSM)
                    snext->setBidiClass(ON);
                s->setBidiClass(ON | WSflag);
            }
            break;

        case NSM :
            if (s == start)
            {
                cls = EmbeddingDirection(sos);
                s->setBidiClass(cls);
            }
            break;
        }
        
        bidi_action action = actionWeak[state][bidi_class_map[cls]];
        int clsRun = GetDeferredType(action);
        if (clsRun != XX)
        {
            SetDeferredRunClass(s, sRun, clsRun);
            sRun = NULL;
        }
        int clsNew = GetResolvedType(action);
        if (clsNew != XX)
            s->setBidiClass(clsNew);
        if (!sRun && (IX & action))
            sRun = s;
        state = stateWeak[state][bidi_class_map[cls]];
    }

    cls = EmbeddingDirection(eos);
    int clsRun = GetDeferredType(actionWeak[state][bidi_class_map[cls]]);
    if (clsRun != XX)
        SetThisDeferredRunClass(sLast, sRun, clsRun);
}

int GetDeferredNeutrals(int action, int level)
{
        action = (action >> 4) & 0xF;
        if (action == (En >> 4))
            return EmbeddingDirection(level);
        else
            return action;
}

int GetResolvedNeutrals(int action)
{
        return action & 0xF;
}

// state values
enum neutral_state
{
        // new temporary class
        r,  // R and characters resolved to R
        l,  // L and characters resolved to L
        rn, // N preceded by right
        ln, // N preceded by left
        a,  // AN preceded by left (the abbrev 'la' is used up above)
        na, // N preceeded by a
} ;

const uint8 neutral_class_map[] = { 0, 1, 2, 0, 4, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

const int actionNeutrals[][5] =
{
// cls= N,   L,  R, AN, EN,        state =
{       In,  0,  0,  0,  0, },  // r    right
{       In,  0,  0,  0,  L, },  // l    left

{       In, En, Rn, Rn, Rn, },  // rn   N preceded by right
{       In, Ln, En, En, LnL, }, // ln   N preceded by left

{       In,  0,  0,  0,  L, },  // a   AN preceded by left
{       In, En, Rn, Rn, En, },  // na   N  preceded by a
} ;

const int stateNeutrals[][5] =
{
// cls= N,  L,  R,      AN,     EN              state =
{       rn, l,  r,      r,      r, },           // r   right
{       ln, l,  r,      a,      l, },           // l   left

{       rn, l,  r,      r,      r, },           // rn  N preceded by right
{       ln, l,  r,      a,      l, },           // ln  N preceded by left

{       na, l,  r,      a,      l, },           // a  AN preceded by left
{       na, l,  r,      a,      l, },           // na  N preceded by la
} ;

void resolveNeutrals(Slot *s, int baseLevel, int sos, int eos)
{
    int state = (sos & 1) ? r : l;
    int cls;
    Slot *sRun = NULL;
    Slot *sLast = s;
    int level = baseLevel;

    for ( ; s; s = s->prev())
    {
        sLast = s;
        cls = BaseClass(s);
        switch (cls)
        {
        case BN :
            continue;
        case LRI :
        case RLI :
        case FSI :
            s->setBidiClass(BN | WSflag);
            continue;

        default :
            int action = actionNeutrals[state][neutral_class_map[cls]];
            int clsRun = GetDeferredNeutrals(action, level);
            if (clsRun != N)
            {
                SetDeferredRunClass(s, sRun, clsRun);
                sRun = NULL;
            }
            int clsNew = GetResolvedNeutrals(action);
            if (clsNew != N)
                s->setBidiClass(clsNew);
            if (!sRun && (action & In))
                sRun = s;
            state = stateNeutrals[state][neutral_class_map[cls]];
        }
    }
    cls = EmbeddingDirection(eos);
    int clsRun = GetDeferredNeutrals(actionNeutrals[state][neutral_class_map[cls]], level);
    if (clsRun != N)
        SetThisDeferredRunClass(sLast, sRun, clsRun);
}

const int addLevel[][4] =
{
        //  cls = L,    R,      AN,     EN         level =
/* even */      { 0,    1,      2,      2, },   // EVEN
/* odd  */      { 1,    0,      1,      1, },   // ODD

};

void resolveImplicit(Slot *s, Segment *seg, uint8 aMirror)
{
    bool rtl = seg->dir() & 1;
    int level = rtl;
    Slot *slast = 0;
    for ( ; s; s = s->next())
    {
        int cls = BaseClass(s);
        s->prev(slast);         // restitch the prev() side of the doubly linked list
        slast = s;
        if (cls == AN)
            cls = AL;   // use AL value as the index for AN, no property change
        if (cls < 5 && cls > 0)
        {
            level = s->getBidiLevel();
            level += addLevel[level & 1][cls - 1];
            s->setBidiLevel(level);
            if (aMirror)
            {
                int hasChar = seg->glyphAttr(s->gid(), aMirror + 1);
                if ( ((level & 1) && (!(seg->dir() & 4) || !hasChar)) 
                  || ((rtl ^ (level & 1)) && (seg->dir() & 4) && hasChar) )
                {
                    unsigned short g = seg->glyphAttr(s->gid(), aMirror);
                    if (g) s->setGlyph(seg, g);
                }
            }
        }
/*        else if (cls != BN)
        {
            s->setBidiLevel(level);
            continue;
        } */
    }
}

void resolveWhitespace(int baseLevel, Segment *seg, uint8 aBidi, Slot *s)
{
    for ( ; s; s = s->prev())
    {
        int8 cls = s->getBidiClass();
        if (cls == WS || cls & WSflag)
            s->setBidiLevel(baseLevel);
        else if (cls != BN)
            break;
    }
}


/*
Stitch two spans together to make another span (with ends tied together).
If the level is odd then swap the order of the two spans
*/
inline
Slot * join(int level, Slot * a, Slot * b)
{
    if (!a) return b;
    if (level & 1)  { Slot * const t = a; a = b; b = t; }
    Slot * const t = b->prev();
    a->prev()->next(b); b->prev(a->prev()); // splice middle
    t->next(a); a->prev(t);                 // splice ends
    return a;
}

/*
Given the first slot in a run of slots with the same bidi level, turn the run
into it's own little doubly linked list ring (a span) with the two ends joined together.
If the run is rtl then reverse its direction.
Returns the first slot after the span
*/
Slot * span(Slot * & cs, const bool rtl)
{
    Slot * r = cs, * re = cs; cs = cs->next();
    if (rtl)
    {
        Slot * t = r->next(); r->next(r->prev()); r->prev(t);
        for (int l = r->getBidiLevel(); cs && (l == cs->getBidiLevel() || cs->getBidiClass() == BN); cs = cs->prev())
        {
            re = cs;
            t = cs->next(); cs->next(cs->prev()); cs->prev(t);
        }
        r->next(re);
        re->prev(r);
        r = re;
    }
    else
    {
        for (int l = r->getBidiLevel(); cs && (l == cs->getBidiLevel() || cs->getBidiClass() == BN); cs = cs->next())
            re = cs;
        r->prev(re);
        re->next(r);
    }
    if (cs) cs->prev(0);
    return r;
}

inline int getlevel(const Slot *cs, const int level)
{
    while (cs && cs->getBidiClass() == BN)
    { cs = cs->next(); }
    if (cs)
        return cs->getBidiLevel();
    else
        return level;
}

Slot *resolveOrder(Slot * & cs, const bool reordered, const int level)
{
    Slot * r = 0;
    int ls;
    while (cs && level <= (ls = getlevel(cs, level) - reordered))
    {
        r = join(level, r, level < ls
                                ? resolveOrder(/* updates */cs, reordered, level+1) // find span of heighest level
                                : span(/* updates */cs, level & 1));
    }
    return r;
}
