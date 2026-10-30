/*
 * RamRiseBalloon (ram-rise-balloon) v0.1.0
 * RAM 使用量が増えている間だけ、画面右下に使用量のバルーンを出す常駐アプリ (Windows 10/11 x64)。
 * メモリ圧縮 (Memory Compression) の量も監視し、圧縮が増えているときもバルーンを出す。
 *
 * plain C / Win32 API。MinGW-w64 でクロスコンパイル (build.sh)。
 * Copyright (c) 2026 zmlworks. MIT License.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winternl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <psapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_NAME     L"RamRiseBalloon"
#define APP_TITLE    L"RAM 上昇バルーン"
#define APP_VER      L"0.1.0"
#define INI_NAME     L"RamRiseBalloon.ini"
#define MUTEX_NAME   L"Local\\zmlworks.RamRiseBalloon"
#define RUN_KEY      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

#define WM_TRAY      (WM_APP + 1)
#define TID_SAMPLE   1
#define TID_ANIM     2
#define ID_PEEK      101
#define ID_PAUSE     102
#define ID_INI       103
#define ID_STARTUP   104
#define ID_EXIT      105
#define ID_INFO      106

#define MB           (1024.0 * 1024.0)
#define GB           (1024.0 * 1024.0 * 1024.0)
#define HIST_MAX     64

/* ---------------------------------------------------------------- 設定 (INI) */
typedef struct {
    int sampleMs;      /* サンプリング間隔 ms */
    int windowSec;     /* 増加を判定する窓 (秒) */
    int riseMB;        /* 窓の間に使用量がこれ以上増えたら「増加中」 */
    int compRiseMB;    /* 窓の間に圧縮量がこれ以上増えたら「圧縮が増加中」 */
    int holdSec;       /* 増加が止まってから消えるまでの秒数 */
    int minPercent;    /* 使用率がこれ未満なら出さない */
    int showTopProcess;
    int clickThrough;  /* 1 = バルーンをクリック透過 (クリックで閉じられない) */
    int opacity;       /* 0..255 */
    int suppressFullscreen;
} Settings;

static Settings gS;
static wchar_t  gIni[MAX_PATH];
static FILETIME gIniTime;

static const wchar_t kDefaultIni[] =
    L"; RamRiseBalloon の設定 (保存すると数秒以内に反映されます)\r\n"
    L"; 値はすべて整数です。\r\n"
    L"[settings]\r\n"
    L"; サンプリング間隔 (ミリ秒, 250-10000)\r\n"
    L"SampleMs=1000\r\n"
    L"; 増加を判定する時間の窓 (秒, 2-30)\r\n"
    L"WindowSec=5\r\n"
    L"; 窓の間に使用中 RAM がこの MB 以上増えたら「増加中」としてバルーンを出す\r\n"
    L"RiseMB=50\r\n"
    L"; 窓の間に圧縮メモリがこの MB 以上増えたら「圧縮が増加中」としてバルーンを出す\r\n"
    L"CompRiseMB=20\r\n"
    L"; 増加が止まってから消えるまでの秒数\r\n"
    L"HoldSec=3\r\n"
    L"; 使用率 (%) がこの値未満のときは出さない (0 = 常に判定)\r\n"
    L"MinPercent=0\r\n"
    L"; いちばん増えたプロセスを表示する (1/0)\r\n"
    L"ShowTopProcess=1\r\n"
    L"; 1 = バルーンをクリック透過にする (クリックで閉じられなくなる)\r\n"
    L"ClickThrough=0\r\n"
    L"; 不透明度 (80-255)\r\n"
    L"Opacity=240\r\n"
    L"; 全画面アプリ・プレゼン中は出さない (1/0)\r\n"
    L"SuppressFullscreen=1\r\n";

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int IniInt(const wchar_t *key, int def, int lo, int hi)
{
    return Clamp((int)GetPrivateProfileIntW(L"settings", key, def, gIni), lo, hi);
}

static int FileTime(const wchar_t *path, FILETIME *ft)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 0;
    *ft = fa.ftLastWriteTime;
    return 1;
}

static void LoadSettings(void)
{
    gS.sampleMs = IniInt(L"SampleMs", 1000, 250, 10000);
    gS.windowSec = IniInt(L"WindowSec", 5, 2, 30);
    gS.riseMB = IniInt(L"RiseMB", 50, 1, 100000);
    gS.compRiseMB = IniInt(L"CompRiseMB", 20, 1, 100000);
    gS.holdSec = IniInt(L"HoldSec", 3, 0, 600);
    gS.minPercent = IniInt(L"MinPercent", 0, 0, 100);
    gS.showTopProcess = IniInt(L"ShowTopProcess", 1, 0, 1);
    gS.clickThrough = IniInt(L"ClickThrough", 0, 0, 1);
    gS.opacity = IniInt(L"Opacity", 240, 80, 255);
    gS.suppressFullscreen = IniInt(L"SuppressFullscreen", 1, 0, 1);
    if (!FileTime(gIni, &gIniTime)) memset(&gIniTime, 0, sizeof gIniTime);
}

/* 新規作成できたら 1、既にあれば 1、作れなければ 0。UTF-16LE (BOM 付き) で書く。 */
static int EnsureIni(const wchar_t *path)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr;
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_EXISTS;
    WriteFile(h, "\xFF\xFE", 2, &wr, NULL);
    WriteFile(h, kDefaultIni, (DWORD)(wcslen(kDefaultIni) * sizeof(wchar_t)), &wr, NULL);
    CloseHandle(h);
    return 1;
}

/* exe と同じフォルダー。書けなければ %APPDATA%\RamRiseBalloon\ */
static void IniPath(void)
{
    wchar_t *slash;
    DWORD n;
    GetModuleFileNameW(NULL, gIni, MAX_PATH);
    slash = wcsrchr(gIni, L'\\');
    if (slash) slash[1] = 0; else gIni[0] = 0;
    if (wcslen(gIni) + wcslen(INI_NAME) < MAX_PATH) wcscat(gIni, INI_NAME);
    if (EnsureIni(gIni)) return;
    n = GetEnvironmentVariableW(L"APPDATA", gIni, MAX_PATH);
    if (n && n + 40 < MAX_PATH) {
        wcscat(gIni, L"\\" APP_NAME);
        CreateDirectoryW(gIni, NULL);
        wcscat(gIni, L"\\" INI_NAME);
        EnsureIni(gIni);
    }
}

/* ---------------------------------------------------------------- NT API (未公開部分) */
typedef NTSTATUS (NTAPI *PFN_NtQSI)(ULONG, PVOID, ULONG, PULONG);
static PFN_NtQSI pNtQSI;

#define SysProcessInformation 5
#define SysStoreInformation   0x6D  /* SystemStoreInformation */
#define MemCompressionInfoRequest 22
#define STORE_COMPRESSION_V1  3

/* phnt (System Informer) の SYSTEM_STORE_INFORMATION / SM_STORE_COMPRESSION_INFORMATION_REQUEST に相当 */
typedef struct { ULONG Version; ULONG StoreInformationClass; PVOID Data; ULONG Length; } RR_STORE_INFO;
typedef struct {
    ULONG Version;         /* 下位 8 bit = version */
    ULONG CompressionPid;
    ULONG WorkingSetSize;
    SIZE_T TotalDataCompressed;
    SIZE_T TotalCompressedSize;
    SIZE_T TotalUniqueDataCompressed;
} RR_COMPRESSION_INFO;

/* SYSTEM_PROCESS_INFORMATION (Vista 以降の配置) */
typedef struct {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
} RR_PROC_INFO;

static BYTE *gProcBuf;
static ULONG gProcBufSize;

/* 戻り値: 先頭エントリ (失敗なら NULL) */
static RR_PROC_INFO *QueryProcesses(void)
{
    int tries;
    if (!pNtQSI) return NULL;
    for (tries = 0; tries < 6; tries++) {
        ULONG need = 0;
        NTSTATUS st;
        if (!gProcBuf) {
            gProcBufSize = gProcBufSize ? gProcBufSize : 512 * 1024;
            gProcBuf = (BYTE *)malloc(gProcBufSize);
            if (!gProcBuf) return NULL;
        }
        st = pNtQSI(SysProcessInformation, gProcBuf, gProcBufSize, &need);
        if (st >= 0) return (RR_PROC_INFO *)gProcBuf;
        if (st != (NTSTATUS)0xC0000004L /* STATUS_INFO_LENGTH_MISMATCH */) return NULL;
        free(gProcBuf); gProcBuf = NULL;
        gProcBufSize = (need > gProcBufSize ? need : gProcBufSize) + 64 * 1024;
    }
    return NULL;
}

/* 管理者として実行したときだけ持っている特権。無ければ何もしない (失敗しても問題なし) */
static void EnableProfilePrivilege(void)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return;
    if (LookupPrivilegeValueW(NULL, L"SeProfileSingleProcessPrivilege", &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(tok);
}

/* 方法 1: NtQuerySystemInformation(SystemStoreInformation, MemCompressionInfoRequest) */
static int QueryStore(RR_COMPRESSION_INFO *ci)
{
    RR_STORE_INFO si;
    if (!pNtQSI) return 0;
    memset(ci, 0, sizeof *ci);
    ci->Version = STORE_COMPRESSION_V1;
    si.Version = 1;
    si.StoreInformationClass = MemCompressionInfoRequest;
    si.Data = ci;
    si.Length = (ULONG)sizeof *ci;
    return pNtQSI(SysStoreInformation, &si, (ULONG)sizeof si, NULL) >= 0 && ci->CompressionPid != 0;
}

/* ---------------------------------------------------------------- サンプル */
typedef struct {
    double used, total, commit, commitLimit;
    double comp;        /* 圧縮ストアが使っている物理メモリ (bytes) */
    double compOrig;    /* 圧縮前のデータ量の推定 (0 = 不明) */
    int compOk;
} Sample;

typedef struct { ULONG_PTR pid; LONGLONG create; double mem; } ProcMem;
typedef struct { ProcMem *v; int n, cap; } ProcSnap;

static Sample   gHist[HIST_MAX];
static ProcSnap gSnaps[HIST_MAX];
static int      gHistCount, gHistPos;  /* gHistPos = 次に書く位置 */

enum { COMP_NONE, COMP_STORE, COMP_PROCESS };
static int gCompMethod = COMP_NONE;  /* 最後に成功した方法 (メニュー表示用) */
static ULONG_PTR gCompPid;

static int gDemo;
static int gDemoT;
static const wchar_t *gDumpPath;

static wchar_t gTopName[64];
static double  gTopDelta;

static int CmpPid(const void *a, const void *b)
{
    ULONG_PTR x = ((const ProcMem *)a)->pid, y = ((const ProcMem *)b)->pid;
    return x < y ? -1 : x > y;
}

static void SnapPush(ProcSnap *s, ULONG_PTR pid, LONGLONG create, double mem)
{
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 512;
        ProcMem *nv = (ProcMem *)realloc(s->v, (size_t)nc * sizeof *nv);
        if (!nv) return;
        s->v = nv; s->cap = nc;
    }
    s->v[s->n].pid = pid; s->v[s->n].create = create; s->v[s->n].mem = mem;
    s->n++;
}

static int IsMemCompressionName(const UNICODE_STRING *u)
{
    static const wchar_t nm[] = L"Memory Compression";
    size_t len = sizeof nm / sizeof nm[0] - 1;
    return u->Buffer && u->Length == len * sizeof(wchar_t) && !memcmp(u->Buffer, nm, len * sizeof(wchar_t));
}

/* プロセス一覧から: (a) 圧縮プロセスのワーキングセット (b) プロセスごとのメモリのスナップショット */
static void SampleProcesses(Sample *s, ProcSnap *snap, ULONG_PTR storePid)
{
    RR_PROC_INFO *p = QueryProcesses();
    snap->n = 0;
    if (!p) return;
    for (;;) {
        ULONG_PTR pid = (ULONG_PTR)p->UniqueProcessId;
        int isComp = storePid ? (pid == storePid) : (IsMemCompressionName(&p->ImageName) &&
                     (ULONG_PTR)p->InheritedFromUniqueProcessId == 4);
        if (isComp) {
            s->comp = (double)p->WorkingSetSize;
            s->compOk = 1;
            gCompPid = pid;
        } else if (pid > 4 && pid != (ULONG_PTR)GetCurrentProcessId()) {   /* 自分自身は除く */
            double m = p->WorkingSetPrivateSize.QuadPart > 0 ? (double)p->WorkingSetPrivateSize.QuadPart
                                                             : (double)p->WorkingSetSize;
            SnapPush(snap, pid, p->CreateTime.QuadPart, m);
        }
        if (!p->NextEntryOffset) break;
        p = (RR_PROC_INFO *)((BYTE *)p + p->NextEntryOffset);
    }
    qsort(snap->v, (size_t)snap->n, sizeof(ProcMem), CmpPid);
}

/* pid のプロセス名 (実行ファイル名) を取得 */
static void ProcessName(ULONG_PTR pid, wchar_t *out, int cap)
{
    RR_PROC_INFO *p = (RR_PROC_INFO *)gProcBuf;
    out[0] = 0;
    if (!p) return;
    for (;;) {
        if ((ULONG_PTR)p->UniqueProcessId == pid) {
            int n = p->ImageName.Length / (int)sizeof(wchar_t);
            if (n >= cap) n = cap - 1;
            if (p->ImageName.Buffer && n > 0) { memcpy(out, p->ImageName.Buffer, (size_t)n * sizeof(wchar_t)); out[n] = 0; }
            return;
        }
        if (!p->NextEntryOffset) return;
        p = (RR_PROC_INFO *)((BYTE *)p + p->NextEntryOffset);
    }
}

static void TakeSample(Sample *s, ProcSnap *snap)
{
    MEMORYSTATUSEX ms;
    PERFORMANCE_INFORMATION pi;
    RR_COMPRESSION_INFO ci;
    int store;

    memset(s, 0, sizeof *s);
    if (gDemo) {
        /* 40 サンプル周期: 5 秒平ら → 15 秒上昇 (約 +150 MB/s) → 20 秒平ら/少し減る */
        int t = gDemoT % 40, rise = t < 5 ? 0 : t < 20 ? t - 5 : 15, crise;
        double jitter = (double)((gDemoT * 7919) % 13 - 6) * MB;
        s->total = 32.0 * GB;
        s->used = 12.0 * GB + rise * 150.0 * MB + (t >= 20 ? -(t - 20) * 4.0 * MB : 0) + jitter;
        s->commit = 18.0 * GB + rise * 160.0 * MB;
        s->commitLimit = 40.0 * GB;
        crise = t < 10 ? 0 : t < 24 ? t - 10 : 14;
        s->comp = 0.8 * GB + crise * 35.0 * MB;
        s->compOrig = s->comp * 2.9;
        s->compOk = 1;
        gCompMethod = COMP_STORE;
        snap->n = 0;
        SnapPush(snap, 1000, 1, 1.0 * GB + rise * 105.0 * MB);
        SnapPush(snap, 2000, 1, 0.5 * GB + rise * 20.0 * MB);
        gDemoT++;
        return;
    }

    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        s->total = (double)ms.ullTotalPhys;
        s->used = (double)(ms.ullTotalPhys - ms.ullAvailPhys);
    }
    pi.cb = sizeof pi;
    if (GetPerformanceInfo(&pi, sizeof pi)) {
        s->commit = (double)pi.CommitTotal * (double)pi.PageSize;
        s->commitLimit = (double)pi.CommitLimit * (double)pi.PageSize;
    }
    store = QueryStore(&ci);
    SampleProcesses(s, snap, store ? (ULONG_PTR)ci.CompressionPid : 0);
    if (s->compOk) {
        gCompMethod = store ? COMP_STORE : COMP_PROCESS;
        if (store && ci.TotalCompressedSize > 0 && ci.TotalDataCompressed >= ci.TotalCompressedSize)
            s->compOrig = s->comp * (double)ci.TotalDataCompressed / (double)ci.TotalCompressedSize;
    } else {
        gCompMethod = COMP_NONE;
    }
}

static const Sample *HistAgo(int ago)  /* 0 = 最新 */
{
    if (ago >= gHistCount) ago = gHistCount - 1;
    return &gHist[(gHistPos - 1 - ago + HIST_MAX * 2) % HIST_MAX];
}
static const ProcSnap *SnapAgo(int ago)
{
    if (ago >= gHistCount) ago = gHistCount - 1;
    return &gSnaps[(gHistPos - 1 - ago + HIST_MAX * 2) % HIST_MAX];
}

static void FindTopGrower(int win)
{
    const ProcSnap *now = SnapAgo(0), *old = SnapAgo(win);
    double best = 0;
    ULONG_PTR bestPid = 0;
    int i;
    gTopName[0] = 0; gTopDelta = 0;
    if (gHistCount <= win) return;
    for (i = 0; i < now->n; i++) {
        const ProcMem *c = &now->v[i], *o;
        ProcMem key;
        double d;
        key.pid = c->pid;
        o = old->n ? (const ProcMem *)bsearch(&key, old->v, (size_t)old->n, sizeof(ProcMem), CmpPid) : NULL;
        d = (o && o->create == c->create) ? c->mem - o->mem : c->mem;
        if (d > best) { best = d; bestPid = c->pid; }
    }
    if (best < 1.0 * MB) return;
    gTopDelta = best;
    if (gDemo) wcscpy(gTopName, bestPid == 1000 ? L"chrome.exe" : L"Code.exe");
    else ProcessName(bestPid, gTopName, (int)(sizeof gTopName / sizeof gTopName[0]));
    if (!gTopName[0]) gTopDelta = 0;
}

/* ---------------------------------------------------------------- 状態 */
static HINSTANCE gInst;
static HWND gMain, gBal;
static UINT gMsgTaskbarCreated;
static NOTIFYICONDATAW gNid;
static int gPaused;
static int gWinSamples;     /* 窓のサンプル数 */
static int gRamRise, gCompRise;
static double gDeltaUsed, gDeltaComp, gRate;
static int gQuiet;          /* 増加していないサンプルが続いた数 */
static int gHold;           /* HoldSec をサンプル数にしたもの */
static int gDismissed;      /* クリックで閉じた → 今回の増加が終わるまで出さない */
static int gPeek;           /* 手動表示の残りサンプル数 */
static int gShown;          /* 表示中 (フェードアウト中は 0) */

/* アニメーション */
static int   gAnimDir;      /* +1 表示中, -1 消去中, 0 停止 */
static DWORD gAnimStart;
static double gAnimT;       /* 0..1 */

/* 描画キャッシュ */
static HDC     gMemDC;
static HBITMAP gBmp, gOldBmp;
static DWORD  *gBits;
static int     gBW, gBH;
static UINT    gFontDpi;
static HFONT   gFSmall, gFBig, gFHead;
static POINT   gBalPos;     /* 最終位置 (スライド前) */

/* ---------------------------------------------------------------- 書式 */
static void FmtSize(wchar_t *b, int cap, double bytes, int sign)
{
    double a = fabs(bytes);
    const wchar_t *sg = sign ? (bytes < 0 ? L"-" : L"+") : (bytes < 0 ? L"-" : L"");
    if (a >= 1000.0 * MB) _snwprintf(b, (size_t)cap, L"%ls%.2f GB", sg, a / GB);
    else _snwprintf(b, (size_t)cap, L"%ls%.0f MB", sg, a / MB);
    b[cap - 1] = 0;
}

/* ---------------------------------------------------------------- モニター / DPI */
typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)(HMONITOR, int, UINT *, UINT *);

static HMONITOR TargetMonitor(void)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    POINT o = {0, 0};
    if (tray) return MonitorFromWindow(tray, MONITOR_DEFAULTTOPRIMARY);
    return MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
}

static UINT MonitorDpi(HMONITOR mon)
{
    static PFN_GetDpiForMonitor fn;
    static int init;
    UINT x = 0, y = 0;
    if (!init) {
        HMODULE h = LoadLibraryW(L"shcore.dll");
        init = 1;
        if (h) fn = (PFN_GetDpiForMonitor)(void *)GetProcAddress(h, "GetDpiForMonitor");
    }
    if (fn && fn(mon, 0 /* MDT_EFFECTIVE_DPI */, &x, &y) == S_OK && x) return x;
    {
        HDC dc = GetDC(NULL);
        x = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return x ? x : 96;
}

static void MakeFonts(UINT dpi)
{
    if (gFontDpi == dpi && gFSmall) return;
    if (gFSmall) DeleteObject(gFSmall);
    if (gFBig) DeleteObject(gFBig);
    if (gFHead) DeleteObject(gFHead);
    gFSmall = CreateFontW(-MulDiv(13, (int)dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    gFHead = CreateFontW(-MulDiv(12, (int)dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    gFBig = CreateFontW(-MulDiv(22, (int)dpi, 96), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    gFontDpi = dpi;
}

/* ---------------------------------------------------------------- バルーン描画 */
#define C_BG      RGB(0x20, 0x22, 0x27)
#define C_BORDER  RGB(0x44, 0x4A, 0x55)
#define C_TEXT    RGB(0xEC, 0xEE, 0xF1)
#define C_DIM     RGB(0x9A, 0xA1, 0xAC)
#define C_RISE    RGB(0xFF, 0x8A, 0x3D)
#define C_COMP    RGB(0xFF, 0xC1, 0x4D)
#define C_BARBG   RGB(0x3A, 0x3F, 0x48)

static void Text(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, UINT fmt, const wchar_t *s)
{
    RECT r;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

static void Fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(c);
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static BYTE Lerp8(int a, int b, double t) { return (BYTE)(a + (b - a) * t + 0.5); }

/* 角丸・縁取り・per-pixel alpha (乗算済み) */
static void ApplyShape(int w, int h, double radius, double bw)
{
    int x, y;
    double hw = w / 2.0, hh = h / 2.0;
    int br = GetRValue(C_BORDER), bg = GetGValue(C_BORDER), bb = GetBValue(C_BORDER);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            DWORD *px = &gBits[y * w + x];
            double qx = fabs(x + 0.5 - hw) - (hw - radius), qy = fabs(y + 0.5 - hh) - (hh - radius);
            double ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            double d = sqrt(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - radius;
            double a = 0.5 - d, bf = d + bw + 0.5;
            int r = (int)((*px >> 16) & 0xFF), g = (int)((*px >> 8) & 0xFF), b = (int)(*px & 0xFF);
            if (a <= 0) { *px = 0; continue; }
            if (a > 1) a = 1;
            if (bf > 0) {
                if (bf > 1) bf = 1;
                r = Lerp8(r, br, bf); g = Lerp8(g, bg, bf); b = Lerp8(b, bb, bf);
            }
            *px = ((DWORD)(a * 255 + 0.5) << 24) | ((DWORD)(r * a + 0.5) << 16) |
                  ((DWORD)(g * a + 0.5) << 8) | (DWORD)(b * a + 0.5);
        }
    }
}

static void RenderBalloon(void)
{
    HMONITOR mon = TargetMonitor();
    MONITORINFO mi;
    UINT dpi = MonitorDpi(mon);
    const Sample *s = HistAgo(0);
    int S = (int)dpi, pad = MulDiv(14, S, 96), lh = MulDiv(20, S, 96);
    int w = MulDiv(330, S, 96), lines = 2, h, y, iw;
    int showTop = gS.showTopProcess && gTopDelta > 0 && gTopName[0];
    double pct = s->total > 0 ? s->used * 100.0 / s->total : 0;
    wchar_t b1[64], b2[64], b3[64], line[200];
    BITMAPINFO bi;
    COLORREF barC;

    if (showTop) lines++;
    lines++; /* コミット */
    h = pad + MulDiv(18, S, 96) + MulDiv(32, S, 96) + MulDiv(8, S, 96) + MulDiv(10, S, 96) + lines * lh + pad - MulDiv(4, S, 96);
    iw = w - 2 * pad;
    MakeFonts(dpi);

    if (!gMemDC) gMemDC = CreateCompatibleDC(NULL);
    if (w != gBW || h != gBH || !gBmp) {
        if (gBmp) { SelectObject(gMemDC, gOldBmp); DeleteObject(gBmp); }
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        gBmp = CreateDIBSection(gMemDC, &bi, DIB_RGB_COLORS, (void **)&gBits, NULL, 0);
        if (!gBmp) return;
        gOldBmp = (HBITMAP)SelectObject(gMemDC, gBmp);
        gBW = w; gBH = h;
    }
    Fill(gMemDC, 0, 0, w, h, C_BG);
    SetBkMode(gMemDC, TRANSPARENT);

    /* 見出し */
    y = pad - MulDiv(2, S, 96);
    if (gRamRise) Text(gMemDC, gFHead, C_RISE, pad, y, iw, MulDiv(18, S, 96), DT_LEFT | DT_VCENTER, L"▲ RAM 使用量が増加中");
    else if (gCompRise) Text(gMemDC, gFHead, C_COMP, pad, y, iw, MulDiv(18, S, 96), DT_LEFT | DT_VCENTER, L"▲ メモリ圧縮が増加中");
    else Text(gMemDC, gFHead, C_DIM, pad, y, iw, MulDiv(18, S, 96), DT_LEFT | DT_VCENTER,
              gQuiet > gHold ? L"RAM 使用量" : L"RAM 使用量（増加は止まりました）");
    if (gDemo) Text(gMemDC, gFHead, C_DIM, pad, y, iw, MulDiv(18, S, 96), DT_RIGHT | DT_VCENTER, L"デモ");
    y += MulDiv(18, S, 96);

    /* 使用量 (大) */
    _snwprintf(line, 200, L"%.1f / %.1f GB", s->used / GB, s->total / GB); line[199] = 0;
    Text(gMemDC, gFBig, C_TEXT, pad, y, iw, MulDiv(32, S, 96), DT_LEFT | DT_VCENTER, line);
    _snwprintf(b1, 64, L"%.0f%%", pct); b1[63] = 0;
    Text(gMemDC, gFBig, C_TEXT, pad, y, iw, MulDiv(32, S, 96), DT_RIGHT | DT_VCENTER, b1);
    y += MulDiv(32, S, 96);

    /* バー */
    barC = pct >= 90 ? RGB(0xF0, 0x55, 0x55) : pct >= 75 ? RGB(0xF0, 0xA0, 0x40) : RGB(0x4F, 0xA3, 0xFF);
    Fill(gMemDC, pad, y, iw, MulDiv(6, S, 96), C_BARBG);
    Fill(gMemDC, pad, y, (int)(iw * (pct > 100 ? 100 : pct) / 100.0 + 0.5), MulDiv(6, S, 96), barC);
    y += MulDiv(6, S, 96) + MulDiv(12, S, 96);

    /* 増加の速さ */
    FmtSize(b1, 64, gRate, 1);
    FmtSize(b2, 64, gDeltaUsed, 1);
    _snwprintf(line, 200, L"%ls/s　（%d 秒で %ls）", b1, gWinSamples * gS.sampleMs / 1000, b2); line[199] = 0;
    Text(gMemDC, gFSmall, gRamRise ? C_RISE : C_DIM, pad, y, iw, lh, DT_LEFT | DT_VCENTER, line);
    y += lh;

    /* 圧縮 */
    if (s->compOk) {
        FmtSize(b1, 64, s->comp, 0);
        if (s->compOrig > 0) { FmtSize(b3, 64, s->compOrig, 0); _snwprintf(b2, 64, L"（元 約 %ls）", b3); b2[63] = 0; }
        else b2[0] = 0;
        _snwprintf(line, 200, L"圧縮: %ls%ls", b1, b2); line[199] = 0;
        Text(gMemDC, gFSmall, gCompRise ? C_COMP : C_TEXT, pad, y, iw, lh, DT_LEFT | DT_VCENTER, line);
        if (fabs(gDeltaComp) >= 1.0 * MB) {
            FmtSize(b3, 64, gDeltaComp, 1);
            _snwprintf(b1, 64, L"%ls %ls", gCompRise ? L"▲" : L"", b3); b1[63] = 0;
            Text(gMemDC, gFSmall, gCompRise ? C_COMP : C_DIM, pad, y, iw, lh, DT_RIGHT | DT_VCENTER, b1);
        }
    } else {
        Text(gMemDC, gFSmall, C_DIM, pad, y, iw, lh, DT_LEFT | DT_VCENTER, L"圧縮: n/a");
    }
    y += lh;

    /* コミット */
    _snwprintf(line, 200, L"コミット: %.1f / %.1f GB", s->commit / GB, s->commitLimit / GB); line[199] = 0;
    Text(gMemDC, gFSmall, C_DIM, pad, y, iw, lh, DT_LEFT | DT_VCENTER, line);
    y += lh;

    if (showTop) {
        FmtSize(b1, 64, gTopDelta, 1);
        _snwprintf(line, 200, L"増加最大: %ls", gTopName); line[199] = 0;
        Text(gMemDC, gFSmall, C_DIM, pad, y, iw - MulDiv(80, S, 96), lh, DT_LEFT | DT_VCENTER, line);
        Text(gMemDC, gFSmall, C_DIM, pad, y, iw, lh, DT_RIGHT | DT_VCENTER, b1);
    }

    GdiFlush();
    ApplyShape(w, h, MulDiv(8, S, 96), dpi / 96.0);

    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(mon, &mi)) {
        int m = MulDiv(12, S, 96);
        gBalPos.x = mi.rcWork.right - w - m;
        gBalPos.y = mi.rcWork.bottom - h - m;
    }
}

static void PresentBalloon(void)
{
    POINT pt = gBalPos, src = {0, 0};
    SIZE sz;
    BLENDFUNCTION bf;
    double t = gAnimT, e = 1 - (1 - t) * (1 - t) * (1 - t);   /* ease-out */
    UINT dpi = gFontDpi ? gFontDpi : 96;
    if (!gBmp) return;
    sz.cx = gBW; sz.cy = gBH;
    if (gAnimDir >= 0) pt.y += (int)((1 - e) * MulDiv(24, (int)dpi, 96));
    bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0; bf.AlphaFormat = AC_SRC_ALPHA;
    bf.SourceConstantAlpha = (BYTE)(gS.opacity * (gAnimDir >= 0 ? e : t) + 0.5);
    UpdateLayeredWindow(gBal, NULL, &pt, &sz, gMemDC, &src, 0, &bf, ULW_ALPHA);
}

static void StartAnim(int dir)
{
    if (gAnimDir == dir) return;
    gAnimDir = dir;
    gAnimStart = GetTickCount();
    SetTimer(gBal, TID_ANIM, 15, NULL);
}

static void ApplyClickThrough(void)
{
    LONG_PTR ex = GetWindowLongPtrW(gBal, GWL_EXSTYLE);
    LONG_PTR nx = gS.clickThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT);
    if (nx != ex) SetWindowLongPtrW(gBal, GWL_EXSTYLE, nx);
}

static void ShowBalloon(void)
{
    RenderBalloon();
    if (!gShown) {
        gShown = 1;
        ApplyClickThrough();
        if (!IsWindowVisible(gBal)) {
            gAnimT = 0;
            PresentBalloon();
            SetWindowPos(gBal, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        StartAnim(+1);
    } else {
        /* 他の最前面ウィンドウの下に潜らないよう、Z 順だけ上げる (アクティブにはしない) */
        SetWindowPos(gBal, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!gAnimDir) PresentBalloon();
    }
}

static void HideBalloon(void)
{
    if (!gShown) return;
    gShown = 0;
    if (IsWindowVisible(gBal)) StartAnim(-1);
}

static void AnimTick(void)
{
    DWORD el = GetTickCount() - gAnimStart;
    if (gAnimDir > 0) {
        gAnimT = el / 220.0;
        if (gAnimT >= 1) { gAnimT = 1; gAnimDir = 0; KillTimer(gBal, TID_ANIM); }
        PresentBalloon();
    } else if (gAnimDir < 0) {
        gAnimT = 1 - el / 450.0;
        if (gAnimT <= 0) {
            gAnimT = 0; gAnimDir = 0; KillTimer(gBal, TID_ANIM);
            ShowWindow(gBal, SW_HIDE);
            return;
        }
        PresentBalloon();
    } else {
        KillTimer(gBal, TID_ANIM);
    }
}

/* ---------------------------------------------------------------- 判定 */
static int FullscreenBusy(void)
{
    QUERY_USER_NOTIFICATION_STATE st;
    if (!gS.suppressFullscreen || gDemo) return 0;
    if (SHQueryUserNotificationState(&st) != S_OK) return 0;
    return st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN || st == QUNS_PRESENTATION_MODE;
}

static void UpdateTray(void);

static void OnSample(void)
{
    Sample *s = &gHist[gHistPos];
    const Sample *old, *prev;
    int hold, rising;
    double pct, thr;

    TakeSample(s, &gSnaps[gHistPos]);
    gHistPos = (gHistPos + 1) % HIST_MAX;
    if (gHistCount < HIST_MAX) gHistCount++;

    gWinSamples = (gS.windowSec * 1000 + gS.sampleMs / 2) / gS.sampleMs;
    if (gWinSamples < 1) gWinSamples = 1;
    if (gWinSamples > HIST_MAX - 1) gWinSamples = HIST_MAX - 1;

    old = HistAgo(gWinSamples);
    prev = HistAgo(1);
    thr = gS.riseMB * MB;
    gDeltaUsed = s->used - old->used;
    gRate = gDeltaUsed / (gWinSamples * gS.sampleMs / 1000.0);
    gDeltaComp = (s->compOk && old->compOk) ? s->comp - old->comp : 0;
    pct = s->total > 0 ? s->used * 100.0 / s->total : 0;

    /* 窓の中で閾値以上増え、かつ直近 1 サンプルで大きく減っていない = 増加中 */
    gRamRise = gHistCount > gWinSamples && gDeltaUsed >= thr && (s->used - prev->used) > -thr / 2;
    gCompRise = gHistCount > gWinSamples && s->compOk && old->compOk && gDeltaComp >= gS.compRiseMB * MB;
    rising = (gRamRise || gCompRise) && pct >= gS.minPercent;

    hold = gHold = (gS.holdSec * 1000 + gS.sampleMs - 1) / gS.sampleMs;
    if (rising) gQuiet = 0; else gQuiet++;
    if (gQuiet > hold) gDismissed = 0;   /* 増加が終わった → 次の増加でまた出す */

    if (gPeek > 0) gPeek--;
    if ((gS.showTopProcess) && (gShown || rising || gPeek)) FindTopGrower(gWinSamples);

    if ((rising || (gShown && gQuiet <= hold)) && !gDismissed && !FullscreenBusy()) ShowBalloon();
    else if (gPeek > 0) ShowBalloon();
    else HideBalloon();

    UpdateTray();
}

/* ---------------------------------------------------------------- 通知領域 */
static HICON LoadAppIcon(int big)
{
    HICON ic = NULL;
    if (FAILED(LoadIconMetric(gInst, MAKEINTRESOURCEW(1), big ? LIM_LARGE : LIM_SMALL, &ic)) || !ic)
        ic = (HICON)LoadImageW(gInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                               GetSystemMetrics(big ? SM_CXICON : SM_CXSMICON),
                               GetSystemMetrics(big ? SM_CYICON : SM_CYSMICON), 0);
    return ic;
}

static void TrayAdd(void)
{
    memset(&gNid, 0, sizeof gNid);
    gNid.cbSize = sizeof gNid;
    gNid.hWnd = gMain;
    gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = LoadAppIcon(0);
    wcscpy(gNid.szTip, APP_TITLE);
    Shell_NotifyIconW(NIM_ADD, &gNid);
    gNid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &gNid);
}

static void UpdateTray(void)
{
    wchar_t tip[128], c[64];
    const Sample *s = gHistCount ? HistAgo(0) : NULL;
    if (gPaused) _snwprintf(tip, 128, L"%ls（一時停止中）", APP_TITLE);
    else if (!s) _snwprintf(tip, 128, L"%ls", APP_TITLE);
    else {
        if (s->compOk) FmtSize(c, 64, s->comp, 0); else wcscpy(c, L"n/a");
        _snwprintf(tip, 128, L"%ls\nRAM %.0f%%（%.1f / %.1f GB）\n圧縮 %ls", APP_TITLE,
                   s->total > 0 ? s->used * 100.0 / s->total : 0.0, s->used / GB, s->total / GB, c);
    }
    tip[127] = 0;
    if (wcscmp(tip, gNid.szTip)) {
        wcscpy(gNid.szTip, tip);
        gNid.uFlags = NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &gNid);
    }
}

/* ---------------------------------------------------------------- スタートアップ (HKCU\...\Run) */
static int StartupRegistered(void)
{
    HKEY k;
    DWORD type = 0, sz = 0;
    LONG r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return 0;
    r = RegQueryValueExW(k, APP_NAME, NULL, &type, NULL, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

static void ToggleStartup(void)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (StartupRegistered()) {
        RegDeleteValueW(k, APP_NAME);
    } else {
        wchar_t exe[MAX_PATH], cmd[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(cmd, MAX_PATH + 4, L"\"%ls\"", exe); cmd[MAX_PATH + 3] = 0;
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}

static void ShowMenu(void)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    const wchar_t *method = gDemo ? L"圧縮の取得: デモ（ダミー値）"
                          : gCompMethod == COMP_STORE ? L"圧縮の取得: ストア統計 (SystemStoreInformation)"
                          : gCompMethod == COMP_PROCESS ? L"圧縮の取得: Memory Compression プロセスの WS"
                          : L"圧縮の取得: n/a（取得できません）";
    int reg = StartupRegistered();
    AppendMenuW(m, MF_STRING | MF_GRAYED, ID_INFO, method);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (gPaused ? MF_GRAYED : 0), ID_PEEK, L"今の使用量を表示");
    AppendMenuW(m, MF_STRING, ID_PAUSE, gPaused ? L"再開" : L"一時停止");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_INI, L"設定ファイルを開く");
    AppendMenuW(m, MF_STRING | (reg ? MF_CHECKED : 0), ID_STARTUP, reg ? L"スタートアップ登録を解除" : L"スタートアップに登録");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"終了");
    GetCursorPos(&pt);
    SetForegroundWindow(gMain);
    TrackPopupMenuEx(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, gMain, NULL);
    PostMessageW(gMain, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void RestartSampling(void)
{
    KillTimer(gMain, TID_SAMPLE);
    if (!gPaused) SetTimer(gMain, TID_SAMPLE, (UINT)gS.sampleMs, NULL);
}

static void Peek(void)
{
    if (gPaused || !gHistCount) return;
    gDismissed = 0;
    gPeek = (gS.holdSec * 1000 + gS.sampleMs - 1) / gS.sampleMs + 3;
    gQuiet = gHold + 1;   /* 見出しを「RAM 使用量」に */
    ShowBalloon();
}

/* ---------------------------------------------------------------- 診断 (--dump <file>) */
/* 3 回サンプリングして、取得できた値と圧縮の取得方法をテキスト (UTF-8) に書いて終了する */
static int Dump(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    int i;
    if (!f) return 2;
    fprintf(f, "RamRiseBalloon 0.1.0 --dump\r\n");
    for (i = 0; i < 3; i++) {
        Sample s;
        ProcSnap snap = {0, 0, 0};
        RR_COMPRESSION_INFO ci;
        int store = QueryStore(&ci);
        if (i) Sleep(1000);
        TakeSample(&s, &snap);
        fprintf(f, "sample %d: used=%.1f MB total=%.1f MB commit=%.1f/%.1f MB procs=%d\r\n", i,
                s.used / MB, s.total / MB, s.commit / MB, s.commitLimit / MB, snap.n);
        fprintf(f, "  store query: %s (pid=%lu ws=%lu data=%.1f MB compressed=%.1f MB)\r\n",
                store ? "ok" : "failed", store ? ci.CompressionPid : 0UL, store ? ci.WorkingSetSize : 0UL,
                store ? (double)ci.TotalDataCompressed / MB : 0.0, store ? (double)ci.TotalCompressedSize / MB : 0.0);
        if (s.compOk)
            fprintf(f, "  compression: method=%s pid=%lu ws=%.1f MB orig_est=%.1f MB\r\n",
                    gCompMethod == COMP_STORE ? "store+process" : "process", (unsigned long)gCompPid,
                    s.comp / MB, s.compOrig / MB);
        else
            fprintf(f, "  compression: n/a\r\n");
        free(snap.v);
    }
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- ウィンドウ */
static LRESULT CALLBACK BalProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        gDismissed = 1; gPeek = 0;
        HideBalloon();
        return 0;
    case WM_TIMER:
        if (wp == TID_ANIM) AnimTick();
        return 0;
    case WM_DPICHANGED:
        if (IsWindowVisible(h)) { RenderBalloon(); PresentBalloon(); }
        return 0;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == gMsgTaskbarCreated && gMsgTaskbarCreated) { TrayAdd(); UpdateTray(); return 0; }
    switch (msg) {
    case WM_TIMER:
        if (wp == TID_SAMPLE) {
            FILETIME ft;
            if (FileTime(gIni, &ft) && CompareFileTime(&ft, &gIniTime)) {
                int old = gS.sampleMs;
                LoadSettings();
                if (old != gS.sampleMs) RestartSampling();
            }
            OnSample();
        }
        return 0;
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU: case WM_RBUTTONUP: ShowMenu(); break;
        case NIN_SELECT: case NIN_KEYSELECT: case WM_LBUTTONUP: Peek(); break;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PEEK: Peek(); break;
        case ID_PAUSE:
            gPaused = !gPaused;
            if (gPaused) { gPeek = 0; HideBalloon(); }
            else { gHistCount = 0; gHistPos = 0; gQuiet = 0; }
            RestartSampling();
            UpdateTray();
            break;
        case ID_INI:
            EnsureIni(gIni);
            ShellExecuteW(NULL, L"open", gIni, NULL, NULL, SW_SHOWNORMAL);
            break;
        case ID_STARTUP: ToggleStartup(); break;
        case ID_EXIT: DestroyWindow(h); break;
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_SETTINGCHANGE:
        if (msg != WM_SETTINGCHANGE || wp == SPI_SETWORKAREA || wp == 0) {
            gFontDpi = 0;
            if (gHistCount && IsWindowVisible(gBal)) { RenderBalloon(); if (!gAnimDir) PresentBalloon(); }
        }
        if (msg == WM_DPICHANGED) return 0;
        break;
    case WM_ENDSESSION:
        if (wp) Shell_NotifyIconW(NIM_DELETE, &gNid);
        return 0;
    case WM_DESTROY:
        KillTimer(h, TID_SAMPLE);
        Shell_NotifyIconW(NIM_DELETE, &gNid);
        if (gBal) DestroyWindow(gBal);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    MSG m;
    HANDLE mtx;
    int argc = 0, i;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    (void)prev; (void)cmd; (void)show;

    gInst = inst;
    for (i = 1; argv && i < argc; i++)
        if (!wcscmp(argv[i], L"--demo")) gDemo = 1;
        else if (!wcscmp(argv[i], L"--dump") && i + 1 < argc) gDumpPath = argv[++i];

    /* マニフェストでも PerMonitorV2 を宣言しているが、念のため API でも設定 */
    {
        typedef BOOL (WINAPI *PFN)(HANDLE);
        PFN fn = (PFN)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
        if (fn) fn((HANDLE)(LONG_PTR)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
    }

    mtx = CreateMutexW(NULL, FALSE, gDemo ? MUTEX_NAME L".demo" : MUTEX_NAME);
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"既に起動しています。\n通知領域（タスクバー右下）のアイコンから操作できます。", APP_TITLE,
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    pNtQSI = (PFN_NtQSI)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    EnableProfilePrivilege();
    IniPath();
    LoadSettings();

    if (gDumpPath) {
        int r = Dump(gDumpPath);
        if (argv) LocalFree(argv);
        return r;
    }
    if (argv) LocalFree(argv);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hIcon = LoadAppIcon(1);
    wc.hIconSm = LoadAppIcon(0);
    wc.lpszClassName = L"RamRiseBalloonMain";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = BalProc;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    wc.lpszClassName = L"RamRiseBalloonPopup";
    RegisterClassExW(&wc);

    /* 隠しトップレベル (メッセージ専用ではない: TaskbarCreated / WM_DISPLAYCHANGE などのブロードキャストを受けるため) */
    gMain = CreateWindowExW(WS_EX_TOOLWINDOW, L"RamRiseBalloonMain", APP_TITLE, WS_POPUP,
                            0, 0, 0, 0, NULL, NULL, inst, NULL);
    gBal = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                           L"RamRiseBalloonPopup", APP_TITLE, WS_POPUP, 0, 0, 1, 1, NULL, NULL, inst, NULL);
    if (!gMain || !gBal) return 1;

    gMsgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    {   /* UIPI: 昇格して動かしたときも TaskbarCreated を受け取れるように */
        typedef BOOL (WINAPI *PFN)(HWND, UINT, DWORD, void *);
        PFN fn = (PFN)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
        if (fn) fn(gMain, gMsgTaskbarCreated, 1 /* MSGFLT_ALLOW */, NULL);
    }
    TrayAdd();
    OnSample();
    RestartSampling();

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (mtx) CloseHandle(mtx);
    return 0;
}
