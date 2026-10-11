// test_movement_evidence.cpp
// EVIDENCE-GATHERING headless harness for the Neko98 movement code.
//
// WHY THIS FILE HAS A DIFFERENT NAME
// ----------------------------------
// The requested paths build_neko\test_movement.cpp / build_test_movement.bat were
// already written by another agent working in this directory at the same time (a
// pass/fail regression suite, not an evidence harness). Overwriting them would
// have destroyed that work, so this harness lives beside them. It modifies NO
// production file: it only links the real objects and drives them.
//
// WHAT IT DOES
// ------------
// Links the REAL production objects (Neko.o, NekoSettings.o, TransparentPet.o,
// Pet.o, Neko95.o) and calls CNeko::Update() directly - no timer, no message
// loop - so every printed number is a measurement of the shipping movement code.
// Console subsystem, so printf() reaches cmd.
//
// Usage: test_movement_evidence.exe [ticks] [footprints]
//        ticks      default 30000 (per Action, per mode)
//        footprints default 1 (the user's real setting)
//
// Measurement groups printed:
//   (1) per Action {0..4}: distinct targets + unreachable count under BOTH the
//       corner interpretation (tx > bw-cx) and the centre one (tx > bw-cx/2)
//   (2) PIN events: >=20 consecutive Update() calls with the position unchanged
//       AND the state inside U_MOVE..DR_MOVE (6..13)
//   (3) the worst pin per Action, full diagnostic + explicit PASS/FAIL on
//       "the target is unreachable only because of the 48px sprite"
//   (4) pins explained by "target outside the reachable box" vs unexplained
//   (5) seam: crossings=N reversals_within_5_ticks=M
//   (6) ticks within 2px of any edge, counts and percentages
//   (7) the same statistics with the run restricted to the 150% bottom monitor
//       (forced through the production CNeko::PlaceAndAim API)
// Plus, at startup: the virtual-desktop metrics, every monitor rect and its
// effective DPI, and a histogram of GetSize().cx proving the 32/48 flip.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <map>
#include <set>
#include <vector>

#include "TransparentPet.h"
#include "Neko.h"
#include "NekoCommon.h"

//normally defined in Neko98.cpp, which this test deliberately does not link
HINSTANCE g_hInstance = NULL;

static int g_nFail = 0;
static void CHECK(const char* what, BOOL ok, const char* detail)
{
    printf("[%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "");
    if (!ok) g_nFail++;
}

//---------------------------------------------------------------------------
// registry save / restore (plain Win32, same approach as test_autostart.cpp)
//---------------------------------------------------------------------------
static const char* g_szKey = "Software\\DHSoft\\Neko95";

struct RegEnt
{
    char  name[128];
    BOOL  had;
    DWORD type;
    DWORD cb;
    BYTE  data[1024];
};

static RegEnt g_reg[32];
static int    g_nReg = 0;

static void RegSave(const char* name)
{
    RegEnt* e = &g_reg[g_nReg++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->cb = (DWORD)sizeof(e->data);

    HKEY hk = NULL;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, g_szKey, 0, KEY_READ, &hk) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hk, name, NULL, &e->type, e->data, &e->cb) == ERROR_SUCCESS)
            e->had = TRUE;
        RegCloseKey(hk);
    }
}

static void RegWriteDword(const char* name, DWORD v)
{
    HKEY hk = NULL;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, g_szKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                       KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
    {
        printf("WARNING: could not open HKCU\\%s for writing (access denied?)\n", g_szKey);
        return;
    }
    RegSetValueEx(hk, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(hk);
}

static void RegRestoreAll()
{
    for (int i = 0; i < g_nReg; i++)
    {
        RegEnt* e = &g_reg[i];
        HKEY hk = NULL;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, g_szKey, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
            continue;
        if (e->had)
            RegSetValueEx(hk, e->name, 0, e->type, e->data, e->cb);
        else
            RegDeleteValue(hk, e->name);
        RegCloseKey(hk);
    }
}

//---------------------------------------------------------------------------
// subclass exposing the protected movement state (test-only, no production edit)
//---------------------------------------------------------------------------
class CTestNeko : public CNeko
{
public:
    CTestNeko() : CNeko((char*)"") {}

    int   ToX()       { return m_nToX; }
    int   ToY()       { return m_nToY; }
    int   DX()        { return m_nDX; }
    int   DY()        { return m_nDY; }
    int   St()        { return (int)m_State; }
    int   Act()       { return m_Action; }
    int   RevRun()    { return m_nRevRun; }
    DWORD Speed()     { return m_dwSpeed; }
    DWORD IdleSpace() { return m_dwIdleSpace; }
    void  SetAction(int a) { m_Action = a; }
};

//---------------------------------------------------------------------------
// geometry
//---------------------------------------------------------------------------
#define SEAM_Y   1440
#define PIN_MIN  20
#define MAXPKG   30

static int g_ticks = 30000;

static const char* StateName(int s)
{
    static const char* n[18] = {
        "STOP", "WASH", "SCRATCH", "YAWN", "SLEEP", "AWAKE",
        "U_MOVE", "D_MOVE", "L_MOVE", "R_MOVE",
        "UL_MOVE", "UR_MOVE", "DL_MOVE", "DR_MOVE",
        "U_CLAW", "D_CLAW", "L_CLAW", "R_CLAW"
    };
    if (s < 0 || s > 17) return "?";
    return n[s];
}

static const char* ActionName(int a)
{
    switch (a)
    {
        case 0: return "CHASE_MOUSE";
        case 1: return "RUN_AWAY_FROM_MOUSE";
        case 2: return "RUN_AROUND_RANDOMLY";
        case 3: return "PACE_AROUND_SCREEN";
        case 4: return "RUN_AROUND";
    }
    return "?";
}

// interpretation 1: corner / top-left box.  unreachable if t > maxPos - size
static BOOL UnreachCorner(int t, int maxPos, int size) { return (t < 0) || (t > maxPos - size); }
// interpretation 2: centre-based.            unreachable if t > maxPos - size/2
static BOOL UnreachCentre(int t, int maxPos, int size) { return (t < 0) || (t > maxPos - size / 2); }

//---------------------------------------------------------------------------
// results
//---------------------------------------------------------------------------
struct Pin
{
    long start, len;
    int  x, y, cx, cy;
    int  tx, ty, dx, dy, state;
};

struct Osc
{
    long start, len;
    int  reversals, x, y, cx, cy, dx, dy, state;
    const char* edge;
};

struct RunResult
{
    int  action, mode;
    long ticks;
    long moveTicks, stopTicks, sleepTicks, topTicks, bottomTicks;
    long nEvents, nUnique;
    long xUn1, xUn2, yUn1, yUn2;
    long xUnAt48, xUnAt32;
    long decisive48;
    long aimVsClamp;
    long pinCount;
    std::vector<Pin> pins;
    // Every tick with the position frozen while in a MOVE state, and the longest
    // consecutive run of such ticks. PIN=0 needs this to be explained: it shows
    // whether frozen-MOVE ticks never happen at all, or happen but never reach
    // the 20-tick threshold.
    long frozenMoveTicks, maxFrozenMoveRun;
    long stalls20, worstStallLen, worstStallTick;
    int  worstStallX, worstStallY, worstStallState;
    long crossings, reversals;
    long atLeft, atRight, atTop, atBottom, atAny;
    long rePlacements;
    long maxRevRun;
    // edge oscillation episodes: >=20 consecutive ticks within 8px of an edge
    // during which the direction of travel reverses at least 3 times. This is
    // the "stuck at the edge, running back and forth" signature the user saw.
    long oscEpisodes, oscTicks, oscReversals;
    long worstOscLen;
    std::vector<Osc> oscs;
    std::map<int, long> sizeHist;
};

//---------------------------------------------------------------------------
// one simulation run
//---------------------------------------------------------------------------
static void RunAction(CTestNeko& n, int action, int mode, int bw, int bh, RunResult& R)
{
    R = RunResult();   // value-initialise (RunResult holds std::vector/std::map)
    R.action = action;
    R.mode   = mode;

    n.SetAction(action);
    srand(12345);

    // Reset through the production placement API so both modes start identically.
    if (mode == 1)
    {
        int nx = 200 + rand() % (bw - 400);
        int ny = SEAM_Y + 200 + rand() % (bh - SEAM_Y - 400);
        n.PlaceAndAim(nx, ny, rand() % bw, SEAM_Y + rand() % (bh - SEAM_Y));
    }
    else
    {
        int nx = 200 + rand() % (bw - 400);
        int ny = 200 + rand() % (bh - 400);
        n.PlaceAndAim(nx, ny, rand() % bw, rand() % bh);
    }

    int  prevToX = n.ToX(), prevToY = n.ToY();
    POINT p0 = n.GetPosition();
    SIZE  s0 = n.GetPetSize();
    int  prevPx = p0.x, prevPy = p0.y;
    int  prevCentreY = p0.y + s0.cy / 2;

    long curLen = 0;
    Pin  curPin;
    memset(&curPin, 0, sizeof(curPin));

    long stallStart = -1, stallLen = 0;

    // edge oscillation episode state
    long oscLen = 0, oscRev = 0, oscStart = -1;
    int  oscDx = 0, oscDy = 0;
    int  oscX = 0, oscY = 0, oscCx = 0, oscCy = 0, oscSt = 0, oscTx = 0, oscTy = 0;
    int  oscDdx = 0, oscDdy = 0;

    std::set<long long> uniq;
    std::vector<int> dirSeq;
    std::vector<long> crossTick;
    std::vector<int>  crossDir;
    dirSeq.reserve(g_ticks + 8);

    for (long tick = 0; tick < g_ticks; tick++)
    {
        POINT pBefore = n.GetPosition();
        SIZE  szBefore = n.GetPetSize();

        n.Update();
        R.ticks++;

        POINT p   = n.GetPosition();
        SIZE  sz  = n.GetPetSize();
        SIZE  aim = n.GetPetAimSize();
        int   st  = n.St();
        int   toX = n.ToX();
        int   toY = n.ToY();
        int   dx  = n.DX();
        int   dy  = n.DY();
        int   rev = n.RevRun();

        R.sizeHist[sz.cx]++;

        BOOL onBottom = (p.y + sz.cy / 2 >= SEAM_Y);
        if (onBottom) R.bottomTicks++; else R.topTicks++;

        // ---- (1) target events ------------------------------------------
        if (toX != prevToX || toY != prevToY)
        {
            R.nEvents++;
            uniq.insert(((long long)toX << 32) ^ (unsigned int)toY);

            int cx = szBefore.cx, cy = szBefore.cy;
            if (UnreachCorner(toX, bw, cx)) R.xUn1++;
            if (UnreachCentre(toX, bw, cx)) R.xUn2++;
            if (UnreachCorner(toY, bh, cy)) R.yUn1++;
            if (UnreachCentre(toY, bh, cy)) R.yUn2++;
            if (UnreachCorner(toX, bw, 48)) R.xUnAt48++;
            if (UnreachCorner(toX, bw, 32)) R.xUnAt32++;
            // the decisive band: reachable at 32, unreachable at 48
            if (cx == 48 && toX > bw - 48 && toX <= bw - 32) R.decisive48++;

            prevToX = toX; prevToY = toY;
        }

        // ---- supplementary: aim arrival vs drawn-size clamp box ----------
        {
            int arrivalX = toX - aim.cx / 2;   // x at which RunTowards reports arrival
            int clampMax = bw - sz.cx;         // largest x the MOVE clamp allows
            if (arrivalX > clampMax) R.aimVsClamp++;
        }

        // ---- (2) PIN detection -------------------------------------------
        BOOL same   = (p.x == prevPx && p.y == prevPy);
        BOOL inMove = (st >= 6 && st <= 13);

        if (same && inMove)
        {
            R.frozenMoveTicks++;
            if (curLen + 1 > R.maxFrozenMoveRun) R.maxFrozenMoveRun = curLen + 1;
            if (curLen == 0)
            {
                memset(&curPin, 0, sizeof(curPin));
                curPin.start = tick;
                curPin.x = p.x;  curPin.y = p.y;
                curPin.cx = sz.cx; curPin.cy = sz.cy;
                curPin.tx = toX; curPin.ty = toY;
                curPin.dx = dx;  curPin.dy = dy;
                curPin.state = st;
            }
            curLen++;
        }
        else
        {
            if (curLen >= PIN_MIN)
            {
                curPin.len = curLen;
                R.pins.push_back(curPin);
                R.pinCount++;
            }
            curLen = 0;
        }

        // ---- supplementary STALL detection (any state) --------------------
        if (same)
        {
            if (stallLen == 0) stallStart = tick;
            stallLen++;
            if (stallLen > R.worstStallLen)
            {
                R.worstStallLen   = stallLen;
                R.worstStallTick  = stallStart;
                R.worstStallX     = p.x;
                R.worstStallY     = p.y;
                R.worstStallState = st;
            }
        }
        else
        {
            if (stallLen >= 20) R.stalls20++;
            stallLen = 0;
        }

        // ---- (5) seam crossings (by the cat's centre) ---------------------
        int centreY = p.y + sz.cy / 2;
        int dirY = (p.y > prevPy) ? 1 : ((p.y < prevPy) ? -1 : 0);
        dirSeq.push_back(dirY);

        if ((prevCentreY < SEAM_Y && centreY >= SEAM_Y) ||
            (prevCentreY > SEAM_Y && centreY <= SEAM_Y))
        {
            R.crossings++;
            crossTick.push_back(tick);
            crossDir.push_back(dirY);
        }
        prevCentreY = centreY;

        // ---- (6) within 2px of an edge ------------------------------------
        int nWidth  = bw - sz.cx;
        int nHeight = bh - sz.cy;
        BOOL L = (p.x <= 2), Rr = (p.x >= nWidth - 2);
        BOOL T = (p.y <= 2), B = (p.y >= nHeight - 2);
        if (L)  R.atLeft++;
        if (Rr) R.atRight++;
        if (T)  R.atTop++;
        if (B)  R.atBottom++;
        if (L || Rr || T || B) R.atAny++;

        // ---- census -------------------------------------------------------
        if (inMove)  R.moveTicks++;
        if (st == 0) R.stopTicks++;
        if (st == 4) R.sleepTicks++;
        if (rev > R.maxRevRun) R.maxRevRun = rev;

        // ---- edge oscillation episodes ------------------------------------
        // "stuck at the edge running back and forth" = a stretch of ticks spent
        // hugging an edge during which the heading keeps flipping.
        BOOL nearEdge = (p.x <= 8 || p.x >= nWidth - 8 || p.y <= 8 || p.y >= nHeight - 8);
        int  mdx = (p.x > prevPx) ? 1 : ((p.x < prevPx) ? -1 : 0);
        int  mdy = (p.y > prevPy) ? 1 : ((p.y < prevPy) ? -1 : 0);
        if (nearEdge)
        {
            if (oscLen == 0)
            {
                oscStart = tick; oscRev = 0;
                oscDx = mdx; oscDy = mdy;
                oscX = p.x; oscY = p.y; oscCx = sz.cx; oscCy = sz.cy;
                oscSt = st; oscTx = toX; oscTy = toY;
                oscDdx = dx; oscDdy = dy;
            }
            else
            {
                if ((mdx != 0 && oscDx != 0 && mdx != oscDx) ||
                    (mdy != 0 && oscDy != 0 && mdy != oscDy)) oscRev++;
                if (mdx != 0) oscDx = mdx;
                if (mdy != 0) oscDy = mdy;
            }
            oscLen++;
            R.oscTicks++;
            if (oscLen >= 20 && oscRev >= 3)
            {
                R.oscReversals = R.oscReversals;   // counted below per episode
                if (oscLen > R.worstOscLen) R.worstOscLen = oscLen;
            }
        }
        else
        {
            if (oscLen >= 20 && oscRev >= 3)
            {
                Osc o;
                o.start = oscStart; o.len = oscLen; o.reversals = oscRev;
                o.x = oscX; o.y = oscY; o.cx = oscCx; o.cy = oscCy;
                o.dx = oscDdx; o.dy = oscDdy; o.state = oscSt;
                o.edge = "";
                R.oscs.push_back(o);
                R.oscEpisodes++;
                R.oscReversals += oscRev;
            }
            oscLen = 0; oscRev = 0;
        }

        prevPx = p.x; prevPy = p.y;

        // ---- (7) mode 1: keep the cat on the 150% monitor -----------------
        if (mode == 1 && !onBottom)
        {
            int nx = 200 + rand() % (bw - 400);
            int ny = SEAM_Y + 200 + rand() % (bh - SEAM_Y - 400);
            n.PlaceAndAim(nx, ny, rand() % bw, SEAM_Y + rand() % (bh - SEAM_Y));
            R.rePlacements++;

            if (curLen >= PIN_MIN) { curPin.len = curLen; R.pins.push_back(curPin); R.pinCount++; }
            curLen = 0;
            if (stallLen >= 20) R.stalls20++;
            stallLen = 0;
            if (oscLen >= 20 && oscRev >= 3)
            {
                Osc o;
                o.start = oscStart; o.len = oscLen; o.reversals = oscRev;
                o.x = oscX; o.y = oscY; o.cx = oscCx; o.cy = oscCy;
                o.dx = oscDdx; o.dy = oscDdy; o.state = oscSt; o.edge = "";
                R.oscs.push_back(o);
                R.oscEpisodes++;
                R.oscReversals += oscRev;
                R.oscTicks = R.oscTicks;   // oscTicks already accumulated
            }
            oscLen = 0; oscRev = 0;

            prevPx = nx; prevPy = ny;
            prevToX = n.ToX(); prevToY = n.ToY();
            SIZE s2 = n.GetPetSize();
            prevCentreY = ny + s2.cy / 2;
        }
    }

    if (curLen >= PIN_MIN) { curPin.len = curLen; R.pins.push_back(curPin); R.pinCount++; }
    if (stallLen >= 20) R.stalls20++;
    if (oscLen >= 20 && oscRev >= 3)
    {
        Osc o;
        o.start = oscStart; o.len = oscLen; o.reversals = oscRev;
        o.x = oscX; o.y = oscY; o.cx = oscCx; o.cy = oscCy;
        o.dx = oscDdx; o.dy = oscDdy; o.state = oscSt; o.edge = "";
        R.oscs.push_back(o);
        R.oscEpisodes++;
        R.oscReversals += oscRev;
    }

    R.nUnique = (long)uniq.size();

    // ---- (5) reversals within 5 ticks of a crossing ------------------------
    for (size_t i = 0; i < crossTick.size(); i++)
    {
        long t = crossTick[i];
        int  d = crossDir[i];
        if (d == 0) { R.reversals++; continue; }
        for (long k = 1; k <= 5; k++)
        {
            size_t idx = (size_t)(t + k);
            if (idx >= dirSeq.size()) break;
            if (dirSeq[idx] != 0 && dirSeq[idx] == -d) { R.reversals++; break; }
        }
    }
}

//---------------------------------------------------------------------------
// printing
//---------------------------------------------------------------------------
static void PrintTargets(const RunResult& R, int bw, int bh)
{
    printf("  (1) targets: events=%ld distinct=%ld\n", R.nEvents, R.nUnique);
    printf("      unreachable X interp1 corner (tx > %d-cx)     = %ld\n", bw, R.xUn1);
    printf("      unreachable X interp2 centre (tx > %d-cx/2)   = %ld\n", bw, R.xUn2);
    printf("      unreachable Y interp1 corner (ty > %d-cy)     = %ld\n", bh, R.yUn1);
    printf("      unreachable Y interp2 centre (ty > %d-cy/2)   = %ld\n", bh, R.yUn2);
    printf("      corner X unreachable at a fixed 48px box=%ld   at a fixed 32px box=%ld\n",
           R.xUnAt48, R.xUnAt32);
    printf("      DECISIVE band: drawn 48px && %d-48 < tx <= %d-32 (reachable at 32, not at 48) = %ld\n",
           bw, bw, R.decisive48);
    printf("      ticks whose aim-arrival x exceeds the drawn-size clamp box = %ld\n", R.aimVsClamp);
}

static const char* EdgeName(const Pin& p, int bw, int bh)
{
    static char buf[64];
    buf[0] = 0;
    if (p.x <= 1)                 strcat(buf, "LEFT ");
    else if (p.x >= bw - p.cx - 1) strcat(buf, "RIGHT ");
    if (p.y <= 1)                 strcat(buf, "TOP ");
    else if (p.y >= bh - p.cy - 1) strcat(buf, "BOTTOM ");
    if (!buf[0]) strcat(buf, "none");
    return buf;
}

static void PrintPins(const RunResult& R, int bw, int bh)
{
    printf("  (2) PIN events (>=%d consecutive frozen MOVE ticks) = %ld\n", PIN_MIN, R.pinCount);
    printf("      frozen-while-in-a-MOVE-state ticks = %ld   longest consecutive frozen MOVE run = %ld (threshold %d)\n",
           R.frozenMoveTicks, R.maxFrozenMoveRun, PIN_MIN);
    size_t lim = R.pins.size() < MAXPKG ? R.pins.size() : MAXPKG;
    for (size_t i = 0; i < lim; i++)
    {
        const Pin& p = R.pins[i];
        printf("      pin#%d start_tick=%ld len=%ld pos=(%d,%d) size=%dx%d nWidth=%d nHeight=%d state=%s(%d) edge=%smonitor=%s\n",
               (int)i, p.start, p.len, p.x, p.y, p.cx, p.cy,
               bw - p.cx, bh - p.cy, StateName(p.state), p.state,
               EdgeName(p, bw, bh),
               (p.y + p.cy / 2 < SEAM_Y) ? "TOP" : "BOTTOM");
    }
    if (R.pins.size() > lim)
        printf("      ... %d further pins not printed (cap %d)\n", (int)(R.pins.size() - lim), MAXPKG);
}

static void PrintWorstPin(const RunResult& R, int bw, int bh)
{
    printf("  (3) worst pin:");
    if (R.pins.empty())
    {
        printf(" NONE (no run of >=%d frozen MOVE ticks)\n", PIN_MIN);
        printf("      HYPOTHESIS(target unreachable only because of the 48px sprite): NOT TESTABLE - no pin\n");
        return;
    }
    const Pin* best = &R.pins[0];
    for (size_t i = 1; i < R.pins.size(); i++)
        if (R.pins[i].len > best->len) best = &R.pins[i];

    int  xMax = bw - best->cx;
    int  yMax = bh - best->cy;
    BOOL onBottom = (best->y + best->cy / 2 >= SEAM_Y);
    BOOL unNow   = (best->tx > xMax) || (best->tx < 0);
    BOOL unAt32  = (best->tx > bw - 32) || (best->tx < 0);
    BOOL unAt48  = (best->tx > bw - 48) || (best->tx < 0);
    BOOL unYNow  = (best->ty > yMax) || (best->ty < 0);
    BOOL unYAt32 = (best->ty > bh - 32) || (best->ty < 0);

    printf(" start_tick=%ld len=%ld\n", best->start, best->len);
    printf("      position=(%d,%d) target=(%d,%d) m_nDX=%d m_nDY=%d state=%s(%d)\n",
           best->x, best->y, best->tx, best->ty, best->dx, best->dy,
           StateName(best->state), best->state);
    printf("      size in effect=%dx%d  reachable X max (bw-cx)=%d  Y max (bh-cy)=%d  monitor=%s\n",
           best->cx, best->cy, xMax, yMax, onBottom ? "BOTTOM(150%)" : "TOP(100%)");
    printf("      target_x > bw-cx  (unreachable at the size in effect)      = %s\n", unNow ? "YES" : "NO");
    printf("      target_x <= bw-32 (reachable if the sprite were 32 wide)    = %s\n", unAt32 ? "NO" : "YES");
    printf("      target_x > bw-48  (unreachable even at 48 wide)             = %s\n", unAt48 ? "YES" : "NO");
    printf("      target_y > bh-cy / > bh-32                                   = %s / %s\n",
           unYNow ? "YES" : "NO", unYAt32 ? "YES" : "NO");

    if (unNow && !unAt32)
        printf("      HYPOTHESIS(target unreachable ONLY because of the 48px size): PASS\n");
    else if (unNow && unAt32)
        printf("      HYPOTHESIS(target unreachable ONLY because of the 48px size): FAIL - unreachable even at 32px\n");
    else
        printf("      HYPOTHESIS(target unreachable ONLY because of the 48px size): FAIL - target inside the reachable box\n");
}

static void PrintTally(const RunResult& R, int bw, int bh)
{
    long explained = 0, unexplained = 0;
    for (size_t i = 0; i < R.pins.size(); i++)
    {
        const Pin& p = R.pins[i];
        if (UnreachCorner(p.tx, bw, p.cx) || UnreachCorner(p.ty, bh, p.cy)) explained++;
        else unexplained++;
    }
    printf("  (4) pin tally: explained_by_target_outside_reachable_box=%ld unexplained=%ld\n",
           explained, unexplained);
}

static void PrintSeam(const RunResult& R)
{
    printf("  (5) seam: crossings=%ld reversals_within_5_ticks=%ld\n", R.crossings, R.reversals);
}

static void PrintEdges(const RunResult& R)
{
    double t = (double)(R.ticks ? R.ticks : 1);
    printf("  (6) edge proximity (<=2px): atLeft=%ld(%.3f%%) atRight=%ld(%.3f%%) atTop=%ld(%.3f%%) atBottom=%ld(%.3f%%) any=%ld(%.3f%%)\n",
           R.atLeft, 100.0 * R.atLeft / t,
           R.atRight, 100.0 * R.atRight / t,
           R.atTop, 100.0 * R.atTop / t,
           R.atBottom, 100.0 * R.atBottom / t,
           R.atAny, 100.0 * R.atAny / t);
}

static void PrintCensus(const RunResult& R)
{
    printf("      census: ticks=%ld move=%ld stop=%ld sleep=%ld top_mon=%ld bottom_mon=%ld maxRevRun=%ld stalls20=%ld worstStall=%ld@tick%ld(state=%s)",
           R.ticks, R.moveTicks, R.stopTicks, R.sleepTicks, R.topTicks, R.bottomTicks,
           R.maxRevRun, R.stalls20, R.worstStallLen, R.worstStallTick,
           StateName(R.worstStallState));
    if (R.mode == 1) printf(" re-placements=%ld", R.rePlacements);
    printf("\n");
}

static void PrintSizeHist(const RunResult& R)
{
    printf("      GetSize().cx histogram:");
    for (std::map<int, long>::const_iterator it = R.sizeHist.begin(); it != R.sizeHist.end(); ++it)
        printf(" cx=%d:%ld", it->first, it->second);
    printf("\n");
}

//---------------------------------------------------------------------------
// (2b) edge oscillation episodes - the user-visible "stuck at the edge,
// running back and forth" signature. A literal PIN (>=20 frozen ticks in a MOVE
// state) is impossible in the MOVE block by construction: the state either moves
// or transitions to STOP. So the observable form of the bug is a long dwell near
// an edge with repeated heading reversals, which is what is counted here.
//---------------------------------------------------------------------------
static void PrintOsc(const RunResult& R, int bw, int bh)
{
    printf("  (2b) edge oscillation episodes (>=20 ticks within 8px of an edge AND >=3 heading reversals) = %ld\n",
           R.oscEpisodes);
    printf("       ticks spent near an edge=%ld (%.3f%%)   reversal events inside episodes=%ld   longest episode=%ld ticks\n",
           R.oscTicks, R.ticks ? 100.0 * R.oscTicks / R.ticks : 0.0,
           R.oscReversals, R.worstOscLen);

    size_t lim = R.oscs.size() < MAXPKG ? R.oscs.size() : MAXPKG;
    for (size_t i = 0; i < lim; i++)
    {
        const Osc& o = R.oscs[i];
        Pin tmp; memset(&tmp, 0, sizeof(tmp));
        tmp.x = o.x; tmp.y = o.y; tmp.cx = o.cx; tmp.cy = o.cy;
        printf("       osc#%d start_tick=%ld len=%ld reversals=%d pos=(%d,%d) size=%dx%d nWidth=%d nHeight=%d state=%s(%d) edge=%smonitor=%s\n",
               (int)i, o.start, o.len, o.reversals, o.x, o.y, o.cx, o.cy,
               bw - o.cx, bh - o.cy, StateName(o.state), o.state,
               EdgeName(tmp, bw, bh),
               (o.y + o.cy / 2 < SEAM_Y) ? "TOP" : "BOTTOM");
    }
    if (R.oscs.size() > lim)
        printf("       ... %d further episodes not printed\n", (int)(R.oscs.size() - lim));
}

//---------------------------------------------------------------------------
// DECISIVE-BAND PROBE (mode: probe)
//
// Isolates the exact hazard class the hypothesis names: a target that IS
// reachable at the 32px aim size but is NOT a legal resting place under the
// 48px drawn-size clamp, i.e. tx in (bw-48, bw-32]. For each such target the cat
// is placed on the 150% monitor to the LEFT of it with ty chosen so that the
// vertical delta is exactly zero (pure horizontal run), and we watch what the
// production movement code actually does: does it arrive, where does it rest,
// does it oscillate, does the oscillation breaker fire?
//---------------------------------------------------------------------------
struct ProbeRow
{
    int  tx;
    int  restX;
    int  settledTicks;
    int  maxRevRun;
    int  tails;        // ticks spent >=bw-cx-1 (hard against the clamp edge)
    int  atClamp;      // reached x == bw-48
    int  atAimRest;    // reached x == tx-16 (the aim rest point)
};

static void ProbeDecisiveBand(CTestNeko& n, int bw, int bh)
{
    n.SetAction(2);           // RUN_AROUND_RANDOMLY keeps re-aiming at m_nToX/m_nToY
    srand(999);

    n.PlaceAndAim(1200, SEAM_Y + 500, 1200, SEAM_Y + 500);

    SIZE szBot = n.GetPetSize();
    SIZE aim   = n.GetPetAimSize();
    printf("\n\n############ DECISIVE-BAND PROBE (bottom/150%% monitor) ############\n");
    printf("drawn size on this monitor=%dx%d   aim size=%dx%d\n", szBot.cx, szBot.cy, aim.cx, aim.cy);
    printf("reachable X max for the clamp (bw-cx) = %d ; aim rest point for a target tx is tx-%d\n",
           bw - szBot.cx, aim.cx / 2);
    printf("band under test: bw-48 < tx <= bw-32  =>  %d < tx <= %d\n", bw - 48, bw - 32);

    std::vector<ProbeRow> rows;

    for (int tx = bw - 70; tx <= bw + 10; tx++)
    {
        ProbeRow r;
        memset(&r, 0, sizeof(r));
        r.tx = tx;

        int py = SEAM_Y + 500;
        int px = tx - 300;
        if (px < 0) px = 0;
        // ty chosen so dLargeY == 0: pure horizontal run
        int ty = py + aim.cy - 1;
        n.PlaceAndAim(px, py, tx, ty);

        int prevX = px, stable = 0, restX = px, maxRev = 0;
        int settled = -1;

        for (int t = 0; t < 600; t++)
        {
            n.Update();
            POINT q = n.GetPosition();
            SIZE  s = n.GetPetSize();
            if (n.RevRun() > maxRev) maxRev = n.RevRun();
            if (q.x >= bw - s.cx - 1) r.tails++;
            if (q.x == bw - 48) r.atClamp = 1;

            if (q.x == prevX) { stable++; if (stable == 1) restX = q.x; }
            else { stable = 0; }
            prevX = q.x;

            if (stable >= 6) { settled = t; restX = q.x; break; }
        }

        r.restX = restX;
        r.settledTicks = settled;
        r.maxRevRun = maxRev;
        if (restX == tx - aim.cx / 2) r.atAimRest = 1;
        if (settled < 0) r.restX = n.GetPosition().x;

        rows.push_back(r);
    }

    printf("\n  tx   restX  settled  atAimRest(tx-16)  atClamp(bw-48)  ticksAtRightEdge  maxRevRun\n");
    for (size_t i = 0; i < rows.size(); i++)
    {
        const ProbeRow& r = rows[i];
        BOOL inBand = (r.tx > bw - 48 && r.tx <= bw - 32);
        printf("  %4d  %5d  %7d  %-18s %-14s %16d  %9d%s\n",
               r.tx, r.restX, r.settledTicks,
               r.atAimRest ? "YES" : "no",
               r.atClamp ? "YES" : "no",
               r.tails, r.maxRevRun,
               inBand ? "   <-- decisive band (reachable at 32, not at 48)" : "");
    }

    int nSettled = 0, nAimRest = 0, nBand = 0, nBandSettled = 0, nBandAimRest = 0, nEverFroze = 0;
    for (size_t i = 0; i < rows.size(); i++)
    {
        const ProbeRow& r = rows[i];
        BOOL inBand = (r.tx > bw - 48 && r.tx <= bw - 32);
        if (r.settledTicks >= 0) nSettled++;
        if (r.atAimRest) nAimRest++;
        if (r.maxRevRun > 0) nEverFroze++;
        if (inBand) { nBand++; if (r.settledTicks >= 0) nBandSettled++; if (r.atAimRest) nBandAimRest++; }
    }
    printf("\n  summary: %d targets probed, settled=%d, rested exactly on the aim point=%d, reversals observed=%d\n",
           (int)rows.size(), nSettled, nAimRest, nEverFroze);
    printf("  decisive band: %d targets, settled=%d, rested on the aim point=%d\n",
           nBand, nBandSettled, nBandAimRest);
    printf("  => every target in the decisive band %s\n",
           (nBandSettled == nBand && nBandAimRest == nBand)
             ? "SETTLED at the DPI-independent aim point: the 48px clamp does NOT strand it"
             : "did NOT settle cleanly (see rows above)");
}

//---------------------------------------------------------------------------
// monitors / DPI
//---------------------------------------------------------------------------
typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
static PFN_GetDpiForMonitor g_pGetDpiForMonitor = NULL;

struct MonInfo { RECT rc; BOOL primary; UINT dpiX, dpiY; };

static BOOL CALLBACK MonEnumProc(HMONITOR hMon, HDC hdc, LPRECT lprc, LPARAM lp)
{
    (void)hdc; (void)lprc;
    std::vector<MonInfo>* v = (std::vector<MonInfo>*)lp;
    MonInfo mi;
    memset(&mi, 0, sizeof(mi));
    MONITORINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetMonitorInfo(hMon, &info))
    {
        mi.rc = info.rcMonitor;
        mi.primary = (info.dwFlags & MONITORINFOF_PRIMARY) ? TRUE : FALSE;
    }
    UINT dx = 0, dy = 0;
    if (g_pGetDpiForMonitor && SUCCEEDED(g_pGetDpiForMonitor(hMon, 0, &dx, &dy)))
    {
        mi.dpiX = dx; mi.dpiY = dy;
    }
    else
    {
        HDC h = GetDC(NULL);
        mi.dpiX = (UINT)GetDeviceCaps(h, LOGPIXELSX);
        mi.dpiY = (UINT)GetDeviceCaps(h, LOGPIXELSY);
        ReleaseDC(NULL, h);
    }
    v->push_back(mi);
    return TRUE;
}

//---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    BOOL probeOnly = FALSE;
    int argi = 1;
    if (argc > 1 && _stricmp(argv[1], "probe") == 0) { probeOnly = TRUE; argi = 2; }
    if (argc > argi) g_ticks = atoi(argv[argi]);
    if (g_ticks <= 0) g_ticks = 30000;
    int footprints = (argc > argi + 1) ? atoi(argv[argi + 1]) : 1;

    printf("=== Neko98 movement / mixed-DPI edge EVIDENCE harness ===\n");
    printf("ticks per Action per mode = %d   footprint rendering = %d   probe_only=%d\n",
           g_ticks, footprints, (int)probeOnly);

    g_hInstance = GetModuleHandle(NULL);

    // PerMonitorV2 - the shipping app is PerMonitorV2 aware, so the pet world is
    // the whole virtual desktop and this test sees what the real pet sees.
    {
        HMODULE hUser = LoadLibraryA("user32.dll");
        typedef BOOL (WINAPI *PFN_SetCtx)(void*);
        PFN_SetCtx pSet = hUser ? (PFN_SetCtx)GetProcAddress(hUser, "SetProcessDpiAwarenessContext") : NULL;
        if (pSet) pSet((void*)(LONG_PTR)-4);
    }
    {
        HMODULE hShcore = LoadLibraryA("shcore.dll");
        if (hShcore)
            g_pGetDpiForMonitor = (PFN_GetDpiForMonitor)GetProcAddress(hShcore, "GetDpiForMonitor");
    }

    printf("\n--- environment (as this process sees it) ---\n");
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    printf("SM_XVIRTUALSCREEN=%d SM_YVIRTUALSCREEN=%d SM_CXVIRTUALSCREEN=%d SM_CYVIRTUALSCREEN=%d\n",
           vx, vy, vw, vh);
    printf("SM_CXSCREEN=%d SM_CYSCREEN=%d\n",
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    std::vector<MonInfo> mons;
    EnumDisplayMonitors(NULL, NULL, MonEnumProc, (LPARAM)&mons);
    printf("monitors=%d\n", (int)mons.size());
    for (size_t i = 0; i < mons.size(); i++)
    {
        const MonInfo& m = mons[i];
        printf("  monitor[%d] world_rect=(%d,%d,%d,%d) %dx%d primary=%d dpi=%ux%u scale=%.3f\n",
               (int)i, m.rc.left - vx, m.rc.top - vy, m.rc.right - vx, m.rc.bottom - vy,
               m.rc.right - m.rc.left, m.rc.bottom - m.rc.top, m.primary,
               m.dpiX, m.dpiY, m.dpiX / 96.0);
    }

    // ---- registry: save live values, seed the user's real settings --------
    printf("\n--- registry seed (HKCU\\%s) ---\n", g_szKey);
    RegSave("Action");
    RegSave("Speed");
    RegSave("Sensitivity");
    RegSave("Size");
    RegSave("Footprints");
    RegSave("Sound Frequency");
    RegSave("On Top");

    RegWriteDword("Speed", 37);
    RegWriteDword("Sensitivity", 12);
    RegWriteDword("Size", 100);
    RegWriteDword("Footprints", (DWORD)footprints);
    RegWriteDword("Sound Frequency", 0);
    RegWriteDword("On Top", 1);
    RegWriteDword("Action", 2);
    printf("seeded Speed=37 Sensitivity=12 Size=100 Footprints=%d Sound Frequency=0 On Top=1\n", footprints);

    // ---- construct --------------------------------------------------------
    CTestNeko n;

    POINT pStart = n.GetPosition();
    SIZE  sStart = n.GetPetSize();
    SIZE  aStart = n.GetPetAimSize();
    printf("\n--- configuration CNeko actually loaded ---\n");
    printf("Action=%d Speed=%ld Sensitivity=%ld drawn size=%dx%d aim size=%dx%d\n",
           n.Act(), (long)n.Speed(), (long)n.IdleSpace(), sStart.cx, sStart.cy, aStart.cx, aStart.cy);
    printf("initial position=(%d,%d)\n", pStart.x, pStart.y);

    // CNeko has no bounds accessor; the pet derives its bounds from the virtual
    // desktop, so the harness cross-checks them against the system metrics.
    printf("pet-world bounds from SM_*VIRTUALSCREEN = (%d,%d,%d,%d)  assumed seam y=%d\n",
           vx, vy, vx + vw, vy + vh, SEAM_Y);

    {
        char d[256];
        sprintf(d, "virtual desktop %dx%d (user's layout is 2560x2880)", vw, vh);
        CHECK("environment matches the user's layout", (vw == 2560 && vh == 2880), d);
        sprintf(d, "drawn sprite %dx%d", sStart.cx, sStart.cy);
        CHECK("icons loaded, sprite size sane (32 or 48)", (sStart.cx == 32 || sStart.cx == 48), d);
    }
    if (sStart.cx != 32 && sStart.cx != 48)
    {
        printf("ABORT: the pet is not drawn at 32 or 48 px - the icon library did not load.\n");
        RegRestoreAll();
        return 3;
    }

    n.SetVisible(FALSE);   // keep the user's desktop clean; does not affect the maths

    // ---- decisive-band probe (cheap; always runs) -------------------------
    ProbeDecisiveBand(n, vw, vh);

    if (probeOnly)
    {
        n.SetVisible(TRUE);
        printf("\n--- registry restore ---\n");
        RegRestoreAll();
        printf("\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail);
        fflush(stdout);
        return g_nFail ? 2 : 0;
    }

    DWORD t0 = GetTickCount();

    RunResult free_[5];
    RunResult restr[5];

    for (int a = 0; a < 5; a++)
    {
        printf("\n=================== Action %d (%s) - free run ===================\n", a, ActionName(a));
        fflush(stdout);
        RunAction(n, a, 0, vw, vh, free_[a]);
        PrintTargets(free_[a], vw, vh);
        PrintPins(free_[a], vw, vh);
        PrintOsc(free_[a], vw, vh);
        PrintWorstPin(free_[a], vw, vh);
        PrintTally(free_[a], vw, vh);
        PrintSeam(free_[a]);
        PrintEdges(free_[a]);
        PrintCensus(free_[a]);
        PrintSizeHist(free_[a]);
        fflush(stdout);
    }

    printf("\n\n############ RESTRICTED TO THE 150%% BOTTOM MONITOR (y >= %d) ############\n", SEAM_Y);
    for (int a = 0; a < 5; a++)
    {
        printf("\n============ Action %d (%s) - bottom-monitor restricted ============\n", a, ActionName(a));
        fflush(stdout);
        RunAction(n, a, 1, vw, vh, restr[a]);
        PrintTargets(restr[a], vw, vh);
        PrintPins(restr[a], vw, vh);
        PrintOsc(restr[a], vw, vh);
        PrintWorstPin(restr[a], vw, vh);
        PrintTally(restr[a], vw, vh);
        PrintSeam(restr[a]);
        PrintEdges(restr[a]);
        PrintCensus(restr[a]);
        PrintSizeHist(restr[a]);
        fflush(stdout);
    }

    DWORD t1 = GetTickCount();
    printf("\n--- total simulation wall time: %lu ms for %d ticks ---\n",
           (unsigned long)(t1 - t0), g_ticks * 10);

    // ---- aggregates -------------------------------------------------------
    printf("\n--- aggregate: merged GetSize().cx histogram (all 10 runs) ---\n");
    {
        std::map<int, long> all;
        for (int a = 0; a < 5; a++)
        {
            for (std::map<int, long>::iterator it = free_[a].sizeHist.begin(); it != free_[a].sizeHist.end(); ++it)
                all[it->first] += it->second;
            for (std::map<int, long>::iterator it = restr[a].sizeHist.begin(); it != restr[a].sizeHist.end(); ++it)
                all[it->first] += it->second;
        }
        long tot = 0;
        for (std::map<int, long>::iterator it = all.begin(); it != all.end(); ++it) tot += it->second;
        for (std::map<int, long>::iterator it = all.begin(); it != all.end(); ++it)
            printf("  GetSize().cx=%d ticks=%ld (%.3f%%)\n", it->first, it->second, tot ? 100.0 * it->second / tot : 0.0);
    }

    printf("\n--- aggregate: PIN totals (group 2 free / group 7 restricted) ---\n");
    long totFree = 0, totRe = 0;
    for (int a = 0; a < 5; a++)
    {
        printf("  Action %d (%s): free pins=%ld   bottom-restricted pins=%ld\n",
               a, ActionName(a), free_[a].pinCount, restr[a].pinCount);
        totFree += free_[a].pinCount;
        totRe   += restr[a].pinCount;
    }
    printf("  TOTAL: free pins=%ld   bottom-restricted pins=%ld\n", totFree, totRe);

    printf("\n--- aggregate: edge oscillation episodes (2b) per action ---\n");
    for (int a = 0; a < 5; a++)
        printf("  Action %d (%s): free episodes=%ld (near-edge ticks=%.3f%% worst=%ld)   bottom-restricted episodes=%ld (near-edge ticks=%.3f%% worst=%ld)\n",
               a, ActionName(a),
               free_[a].oscEpisodes, free_[a].ticks ? 100.0 * free_[a].oscTicks / free_[a].ticks : 0.0, free_[a].worstOscLen,
               restr[a].oscEpisodes, restr[a].ticks ? 100.0 * restr[a].oscTicks / restr[a].ticks : 0.0, restr[a].worstOscLen);

    printf("\n--- aggregate: seam (crossings / reversals_within_5_ticks) ---\n");    for (int a = 0; a < 5; a++)
        printf("  Action %d (%s): free %ld/%ld   bottom-restricted %ld/%ld\n",
               a, ActionName(a), free_[a].crossings, free_[a].reversals,
               restr[a].crossings, restr[a].reversals);

    printf("\n--- aggregate: targets + unreachable (corner interp), free run ---\n");
    for (int a = 0; a < 5; a++)
        printf("  Action %d (%s): events=%ld distinct=%ld unX=%ld unY=%ld decisive48only=%ld\n",
               a, ActionName(a), free_[a].nEvents, free_[a].nUnique,
               free_[a].xUn1, free_[a].yUn1, free_[a].decisive48);

    printf("\n--- aggregate: pins explained vs unexplained (group 4), free run ---\n");
    for (int a = 0; a < 5; a++)
    {
        long ex = 0, un = 0;
        for (size_t i = 0; i < free_[a].pins.size(); i++)
        {
            const Pin& p = free_[a].pins[i];
            if (UnreachCorner(p.tx, vw, p.cx) || UnreachCorner(p.ty, vh, p.cy)) ex++; else un++;
        }
        printf("  Action %d (%s): explained=%ld unexplained=%ld\n", a, ActionName(a), ex, un);
    }

    printf("\n--- aggregate: edge proximity any-edge percentage per action ---\n");
    for (int a = 0; a < 5; a++)
        printf("  Action %d (%s): free any=%ld/%ld (%.3f%%)   bottom-restricted any=%ld/%ld (%.3f%%)\n",
               a, ActionName(a), free_[a].atAny, free_[a].ticks,
               free_[a].ticks ? 100.0 * free_[a].atAny / free_[a].ticks : 0.0,
               restr[a].atAny, restr[a].ticks,
               restr[a].ticks ? 100.0 * restr[a].atAny / restr[a].ticks : 0.0);

    // ---- restore ----------------------------------------------------------
    n.SetVisible(TRUE);
    printf("\n--- registry restore ---\n");
    RegRestoreAll();
    {
        HKEY hk = NULL;
        DWORD v = 0, cb = sizeof(v), type = 0;
        BOOL ok = FALSE;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, g_szKey, 0, KEY_READ, &hk) == ERROR_SUCCESS)
        {
            if (RegQueryValueEx(hk, "Speed", NULL, &type, (LPBYTE)&v, &cb) == ERROR_SUCCESS && v == 37)
                ok = TRUE;
            RegCloseKey(hk);
        }
        CHECK("registry restored (Speed back to the user's 37)", ok, "");
    }

    printf("\n=== %s (%d failures) ===\n", g_nFail ? "FAILED" : "ALL PASS", g_nFail);
    fflush(stdout);
    return g_nFail ? 2 : 0;
}
