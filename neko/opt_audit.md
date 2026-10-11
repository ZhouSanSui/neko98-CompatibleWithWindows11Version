# Neko98 — Optimization / Robustness / UX Audit

Read-only audit of the Neko98 desktop-pet sources in this folder.
Every claim below was verified by reading the file; line numbers refer to the
tree as of the audit and may drift while the sources are being edited.

Already-known work deliberately **not** re-reported here: the unified
transparent layered window, AND-mask-derived premultiplied alpha, Chinese
localization of the dialogs and tray menu, PerMonitorV2 DPI awareness,
footprint culling by lifetime/distance/monitor, and the application manifest.

Summary table (ranked):

| # | Title | Category | Effort | Risk | User-visible |
|---|-------|----------|--------|------|--------------|
| 1 | Tray icon is lost when Explorer restarts | Robustness | S | low | yes |
| 2 | Pet can no longer be dragged with the mouse | UX-Feature | M | medium | yes |
| 3 | Chinese cat names are rejected (`isalpha`/`isdigit` only) | UX-Feature | S | low | yes |
| 4 | Tray menu has no Pause/Hide; tooltip is the constant "Neko" | UX-Feature | S | low | yes |
| 5 | UI text is compiled as GBK → mojibake on non-936 Windows | Compatibility | M | medium | yes |
| 6 | Two full recomposites per movement tick; fade timer repaints identical frames | Performance | M | medium | no |
| 7 | `DpiScaleForPoint` LoadLibrary/FreeLibrary on every call | Performance | S | low | no |
| 8 | Help button / F1 does nothing (WinHelp + missing .hlp) | UX-Feature | S | low | yes |
| 9 | `DestroyIcon` on a shared `LoadIcon` handle | Robustness | S | low | maybe |
| 10 | No repaint hook for lock/unlock, RDP reconnect, resume | Robustness | S | low | yes |
| 11 | `NumNekos` read from the registry is unbounded | Robustness | S | low | no |
| 12 | `WM_DISPLAYCHANGE` tears down and rebuilds every cat | Performance | M | medium | no |
| 13 | `g_dwSleepTime` is dead; two timers never unified; `WM_TIMER` ignores wParam | Code-quality | S | low | no |
| 14 | Surface shrink heuristic leaves a DIB up to 4× oversized | Performance | S | low | no |
| 15 | 7 integer divisions per blended pixel; linear sprite-cache scan | Performance | S | low | no |
| 16 | `PlaySound(SND_FILENAME)` re-reads the WAV from disk every time | Performance | M | low | no |
| 17 | No global hotkey to pause/hide the pet | UX-Feature | M | low | yes |
| 18 | Registry string reads are NUL/type-unsafe; inconsistent value types | Code-quality | S | low | no |
| 19 | Version resource contradicts itself; copyright is `?1999` | Compatibility | S | low | minor |

---

## 1. Tray icon is lost when Explorer restarts

- **Category:** Robustness
- **Evidence:** `Tray.cpp:28`, `Tray.cpp:39`, `Neko98.cpp:88-97`
- **What is wrong today:** The icon is registered exactly once, at the moment the
  settings say it should be shown:

  ```c
  // Tray.cpp:28
  Shell_NotifyIcon( NIM_ADD, &tnid );
  ```

  and is only ever removed/re-added in response to a configuration reload
  (`Neko98.cpp:90-96`). There is no handler for the shell's `TaskbarCreated`
  broadcast — a case-sensitive grep for `TaskbarCreated` / `RegisterWindowMessage`
  over every `*.cpp`/`*.h` returns nothing. When `explorer.exe` restarts (crash,
  update, "Restart Explorer", some full-screen games and shell replacements), the
  notification-area window is destroyed and every icon registered with `NIM_ADD`
  disappears. Neko98 never notices, so the tray icon is gone for the rest of the
  session — and with it the only way to reach the settings dialog. The pet itself
  keeps running, so the user cannot even tell why the icon vanished.
- **Proposed change:** Register the shell's restart message once (in
  `InitApplication`, `Neko98.cpp:173-210`):

  ```c
  UINT g_uTaskbarCreated = RegisterWindowMessage( "TaskbarCreated" );
  ```

  and in `WndProc` (`Neko98.cpp:103`) add a branch for it that re-creates the
  icon when `g_fTaskbar` is set, e.g. call `Tray->RemoveIcon()`/`AddIcon()` or
  add a `CTray::ReAddIcon()` helper. `CTray` will need to remember the `HICON`
  and `HWND` it was last given, because after `DestroyIcon` (item 9) the old
  handle is unusable — fix item 9 first, or store the icon and re-`LoadIcon` it.
- **Effort:** S (<30 min)
- **Risk:** low — an extra message branch; the worst case is a redundant
  `NIM_ADD` that the shell ignores.
- **User-visible:** yes — this is the difference between "cannot configure or
  quit the pet without Task Manager" and normal operation.

---

## 2. Pet can no longer be dragged with the mouse

- **Category:** UX-Feature
- **Evidence:** `TransparentPet.cpp:247` (window styles), absence of any mouse
  handling in `TransparentPet.cpp` (`grep` for `WM_NCHITTEST`, `WM_LBUTTON*`,
  `WM_SETCURSOR`, `HTTRANSPARENT`, `OnLButton` → no matches), and the original
  implementation at `AlwaysOnTopPet.cpp:192-208` / `AlwaysOnTopPet.cpp:216-220`
- **What is wrong today:** The layered window is created click-through:

  ```c
  // TransparentPet.cpp:247
  DWORD dwExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  ```

  `WS_EX_TRANSPARENT` makes the *whole* window transparent to the mouse, and the
  new window procedure handles no mouse messages at all (`TransparentPet.cpp:334-366`
  deals only with `WM_CREATE`, `WM_ERASEBKGND`, `WM_TIMER`). The previous
  implementation that this renderer replaced did support dragging:

  ```c
  // AlwaysOnTopPet.cpp:219  — left button begins a window move
  SendMessage( m_hWndOnTop, WM_SYSCOMMAND, SC_MOVE+2, 0 );
  ```

  with `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` bookkeeping at
  `AlwaysOnTopPet.cpp:192-208`. That file is still in the tree but is **not
  compiled** — `build_pet.bat:21` lists only
  `(Neko98 Neko Pet TransparentPet Tray NekoSettings AutoStart)`. So the 1998
  program's "grab the cat and put it where you want it" interaction was lost in
  the port: today the only way to influence the cat's position is to move the
  mouse and let it chase.
- **Proposed change:** Drop `WS_EX_TRANSPARENT` and make the hit-test
  alpha-aware instead, so only the cat's opaque pixels accept the mouse:

  - Handle `WM_NCHITTEST` in `CTransparentPet::WndProc`. Convert the cursor
    position to surface-local coordinates; sample `m_pBits` at that pixel; if
    `m_pBits[i*4+3]` is below a threshold (say `< 8`) return `HTTRANSPARENT` so
    the click falls through to whatever is underneath, otherwise return
    `HTCAPTION` so Windows performs the drag for you.
  - Because the surface is recomposited on every `Redraw`, re-hit-testing after
    `WM_EXITSIZEMOVE` is enough to resynchronise; add a
    `MoveTo(rc.left + ox, rc.top + oy)` from a `WM_MOVING`/`WM_EXITSIZEMOVE`
    handler, with the `ox,oy` from `NekoWorldOrigin`.
  - Alternatively (simpler, coarser): return `HTCAPTION` for the cat's bounding
    box only. Keeps the "grab the cat" feel, costs nothing, and cannot
    mis-sample the alpha buffer.
- **Effort:** M (1-3 h) — the alpha hit-test plus drag bookkeeping and a
  regression pass over click-through behaviour.
- **Risk:** medium — a wrong hit-test makes the pet either un-draggable again or
  an invisible click trap over a large monitor area; the bounding-box fallback
  keeps the downside small.
- **User-visible:** yes.

---

## 3. Chinese cat names are rejected (`isalpha`/`isdigit` only)

- **Category:** UX-Feature
- **Evidence:** `NekoCFG.cpp:140-148`
- **What is wrong today:** The New-Neko dialog validates the name like this:

  ```c
  // NekoCFG.cpp:142
  if( !( isalpha(*lpszPtr) || isdigit(*lpszPtr) || *lpszPtr == ' ' ) )
  ```

  Anything outside ASCII letters/digits/space is refused with
  `"名称无效：名字只能包含字母、数字和空格"`. In an otherwise fully Chinese UI
  whose own default name is `"Neko（默认）"` (`NekoCFG.cpp:25`), this means a
  Chinese user cannot name their cat in Chinese at all. The test is also
  locale-unsafe: `isalpha`/`isdigit` take an `int` that must be representable as
  `unsigned char` or `EOF`; passing a GBK lead byte (`>= 0x80`, negative as
  `char`) is undefined behaviour in the default `"C"` locale. A hostile name is
  also not what the registry cares about — what matters is that the name is used
  as a **registry subkey** (`Neko.cpp:81-85` builds `szKey` by `strcat`-ing the
  name onto `szNekoRegKey`), so `\` and control characters are the real hazard,
  not CJK.
- **Proposed change:** Replace the character-class check with a rejection list
  matching the two real constraints — registry subkey safety and
  `CATSETTINGS::szName` length (`MAX_NEKO_NAME`, already limited by
  `NekoCFG.cpp:96`'s `CB_LIMITTEXT`): reject `\ / : * ? " < > |`, any byte
  `< 0x20`, and a trailing space or dot. Everything else, including multi-byte
  CJK, is permitted.
- **Effort:** S (<30 min)
- **Risk:** low — only widens the accepted set inside an already length- and
  uniqueness-checked field.
- **User-visible:** yes.

---

## 4. Tray menu has no Pause/Hide; tooltip is the constant "Neko"

- **Category:** UX-Feature
- **Evidence:** `Tray.cpp:66-67`, `Tray.cpp:25`, `Neko98.cpp:132-148`
- **What is wrong today:** The entire context menu is two items:

  ```c
  // Tray.cpp:66-67
  AppendMenu( hMenu, MF_STRING, ID_SETTINGS,"设置(&S)" );
  AppendMenu( hMenu, MF_STRING, ID_EXIT, "退出(&E)" );
  ```

  So the only ways to make the pet stop bothering you are to open the settings
  dialog and set `NumNekos` to 0, or to quit it entirely. For a desktop pet,
  "pause it for a while" is the single most requested control. The tooltip is
  equally static:

  ```c
  // Tray.cpp:25
  strncpy( tnid.szTip, "Neko", sizeof(tnid.szTip) );
  ```

  so hovering tells you nothing about how many cats are running or what they are
  doing.
- **Proposed change:** Add a third and fourth menu item — `"暂停(&P)"` /
  `"继续(&R)"` (checkbox, `MF_CHECKED` while paused) and `"隐藏(&H)"` — handled
  in `WndProc`'s `WM_COMMAND` next to `ID_SETTINGS`/`ID_EXIT`
  (`Neko98.cpp:112-123`). Gate the update loop on the pause flag:

  ```c
  // Neko98.cpp:160
  if( !g_fPaused ) for( int i = 0; i < g_nNumCats; i++ ) g_NekoArray[i]->Update();
  ```

  A paused cat that is mid-trail should also let the fade timer drain, so no
  extra work is needed there. Separately, set `szTip` from the live state (e.g.
  `"Neko — 2 只"` / `"Neko — 已暂停"`) and refresh it with
  `NIM_MODIFY | NIF_TIP` when the state changes rather than only at `NIM_ADD`.
- **Effort:** S (<30 min) for pause + tooltip string; M if a hide/show-with-
  restore is wanted.
- **Risk:** low — a new branch in an existing switch plus a boolean checked by
  one `for` loop.
- **User-visible:** yes.

---

## 5. UI text is compiled as GBK → mojibake on non-936 Windows

- **Category:** Compatibility
- **Evidence:** `build_pet.bat:19-22`
- **What is wrong today:** The build pins the executable character set:

  ```bat
  rem Neko98.bat:19-21
  rem NOTE: sources are UTF-8 (no BOM). -finput-charset=UTF-8 reads them; -fexec-charset=GBK
  rem emits literals as GBK so menus/dialogs show correctly on a GBK(936) system.
  for %%f in (Neko98 Neko Pet TransparentPet Tray NekoSettings AutoStart) do (
    %MINGW%\g++.exe -O2 ... -finput-charset=UTF-8 -fexec-charset=GBK -c %%f.cpp -o %%f.o
  ```

  Every Chinese literal in the program (`"设置 Neko"`, the tray items, all
  `MessageBox` text, the `.rc` strings via `windres -c 936` at
  `build_pet.bat:13`) therefore reaches the **ANSI** Win32 APIs as a GBK byte
  stream. `MessageBoxA`/`CreateDialogA`/`AppendMenuA` interpret those bytes in
  the *system* ANSI codepage, so on an en-US, ja-JP, ko-KR, ru-RU or any other
  non-936 machine every Chinese string renders as garbage (`??????`/mojibake),
  and the `.rc` dialog text is decoded with the wrong table at load time. The
  program is otherwise locale-independent, so this is the only thing standing
  between it and running correctly worldwide.
- **Proposed change:** Two options, in increasing order of effort/quality:

  1. **UTF-8 + the Win10 1903 opt-in (M):** build with
     `-fexec-charset=UTF-8`, switch the process to the UTF-8 ANSI codepage at
     startup with an `app.manifest`-independent call:
     `SetProcessUILanguageOverride` is not enough — the supported route is the
     `activeCodePage` element in the application manifest (which the pet's
     manifest, `neko_manifest.xml`, currently lacks) or
     `SetConsoleOutputCP`-style approaches that do **not** affect
     `MessageBoxA`. So this option really means adding
     `<activeCodePage>UTF-8</activeCodePage>` to `neko_manifest.xml` plus the
     UTF-8 exec charset, and `windres` must be told the `.rc` is UTF-8 too.
  2. **Wide characters (L):** compile with `-municode` and switch the string
     literals to `L"..."` and the `A` APIs to their `W` counterparts. This is
     unconditionally correct and removes the codepage question entirely, but
     touches every string in both executables.

  For a program this size, option 1 is the pragmatic fix; option 2 is what I
  would recommend if the sources are being touched anyway. Note the `.rc` files
  are currently read as codepage 936 (`build_pet.bat:13`), so the resource step
  must change in step with the sources or the dialogs will disagree with the
  `MessageBox` text.
- **Effort:** M for option 1, L for option 2.
- **Risk:** medium — a half-done conversion (sources UTF-8, resources still
  GBK, or vice versa) produces mixed mojibake that is worse than today's
  consistent GBK on a 936 machine; verify on both a 936 and a 1252/65001
  system.
- **User-visible:** yes — on any non-Chinese Windows install.

---

## 6. Two full recomposites per movement tick; the fade timer repaints identical frames

- **Category:** Performance
- **Evidence:** `Neko.cpp:377`, `Neko.cpp:394-397`, `TransparentPet.cpp:904-908`,
  `TransparentPet.cpp:942`, `TransparentPet.cpp:836-859`, `TransparentPet.cpp:938`
- **What is wrong today:** With footprints enabled, one movement tick composes
  and pushes the surface **twice**:

  ```c
  // Neko.cpp:377
  m_pPet->SetImageAndMoveTo( GetStateAnimationFrameIndex(), nNewX, nNewY );
  ...
  // Neko.cpp:394-397
  if( m_uTickCount & 1 )
      m_pPet->DrawOnTarget( nX-(m_nDY/2), nY, m_hIconFootprints[iFpAnim] );
  else
      m_pPet->DrawOnTarget( nX, nY-(m_nDX/2), m_hIconFootprints[iFpAnim] );
  ```

  `SetImageAndMoveTo` → `MoveTo` → `Draw` → `Redraw` (`TransparentPet.cpp:904-908`)
  and `DrawOnTarget` ends in its own `Redraw()` (`TransparentPet.cpp:942`). Each
  `Redraw` clears the whole DIB and re-blends every footprint and the cat:

  ```c
  // TransparentPet.cpp:836
  memset( m_pBits, 0, (size_t)m_surfW * m_surfH * 4 );
  ```

  On top of that the fade timer runs at 60 ms (`TransparentPet.cpp:938`) while
  the movement clock runs at 200 ms (`Neko98.cpp:231`), so ~16 of those 60 ms
  ticks per second composite a frame in which **nothing has changed** — no
  footprint crosses an alpha threshold more often than roughly every 86 ms,
  and the cat image is static between movement ticks. Every one of those frames
  still pays the full `memset` plus a blend of the cat and up to `MAX_FP` (256)
  footprints, then an `UpdateLayeredWindow` (`TransparentPet.cpp:896`), which is
  a synchronous composition of the whole surface by the DWM. The cost scales
  with the *surface*, and the surface is sized by the bounding box of the whole
  trail — so on a 1080p monitor a long trail means a large fraction of a
  megapixel is touched and handed to the compositor 20+ times a second.
- **Proposed change:**
  1. Coalesce redraws. Give `CTransparentPet` an `m_bDirty` flag and a
     `RequestRedraw()` that only sets it; perform the actual `Redraw` once from
     the window's timer/`WM_TIMER` or from a short `PostMessage`-deferred step,
     so `SetImageAndMoveTo` + `DrawOnTarget` in the same tick cost one composite.
  2. Make the fade timer skip identical frames: cache the alpha value last
     composited for each footprint (one extra `BYTE` in `FOOTPRINT`,
     `TransparentPet.h`), and in the `WM_TIMER` path only `Redraw()` when at
     least one footprint's quantised alpha differs from the cached value.
  3. Optionally raise the fade interval to ~80-100 ms: `FP_LIFETIME` is 2200 ms,
     so 256 alpha levels are not perceptible; 20-25 steps is plenty and halves
     the timer wakeups.
- **Effort:** M (1-3 h)
- **Risk:** medium — a dirty-flag missed somewhere leaves a stale frame (a
  ghost footprint or a cat that does not move); the alpha-cache comparison must
  be re-validated after a DPI rebuild (`FreeSprites`, `TransparentPet.cpp:391`).
- **User-visible:** no — the visible result is identical; the win is CPU/GPU
  time and battery.

---

## 7. `DpiScaleForPoint` LoadLibrary/FreeLibrary on every call

- **Category:** Performance
- **Evidence:** `TransparentPet.cpp:145-156`, call sites `TransparentPet.cpp:384`
  and `TransparentPet.cpp:732`
- **What is wrong today:** Every DPI query opens and closes `shcore.dll`:

  ```c
  // TransparentPet.cpp:145-156
  HMODULE shcore = LoadLibraryA( "shcore.dll" );
  if( shcore )
  {
      typedef HRESULT (WINAPI *PFN_GetDpiForMonitor)( HMONITOR, int, UINT*, UINT* );
      PFN_GetDpiForMonitor pGet = (PFN_GetDpiForMonitor)GetProcAddress( shcore, "GetDpiForMonitor" );
      if( pGet ) { ... }
      FreeLibrary( shcore );
  }
  ```

  `Redraw` calls this at least once per frame (`TransparentPet.cpp:732`), and
  `SetImages` calls it again (`TransparentPet.cpp:384`). With the fade timer
  running at 60 ms and double redraws per tick (item 6) this is up to ~30-40
  loader round-trips per second, each one taking the loader lock, doing a
  reference-count update and a path lookup in the DLL cache. It is also
  needless: the binary already declares PerMonitorV2 awareness, and
  `GetDpiForWindow` has been available since Windows 10 1607 without any
  dynamic loading at all (it lives in `user32`).
- **Proposed change:** Resolve the function pointer exactly once in a
  function-local static or a file-scope lazy initialiser:

  ```c
  static PFN_GetDpiForMonitor GetDpiFn()
  {
      static PFN_GetDpiForMonitor s_pfn = NULL;
      static BOOL s_bTried = FALSE;
      if( !s_bTried )
      {
          s_bTried = TRUE;
          HMODULE h = LoadLibraryA( "shcore.dll" );      // deliberately never freed
          if( h ) s_pfn = (PFN_GetDpiForMonitor)GetProcAddress( h, "GetDpiForMonitor" );
          if( !s_pfn ) s_pfn = (PFN_GetDpiForMonitor)GetProcAddress( GetModuleHandleA("user32.dll"), "GetDpiForWindow" );
      }
      return s_pfn;
  }
  ```

  Do not call `FreeLibrary`: keeping `shcore.dll` resident is a few tens of
  kilobytes and removes an unload/reload cycle that can drop the module between
  two frames. (`GetDpiForWindow`'s signature differs — it takes the `HWND` and
  has no `MONITOR_DPI_TYPE` — so if you fall back to it, query the DPI for a
  size-1 window positioned at the point, or simply branch and call it with
  `m_hWnd` after the window has been moved, which is what `Redraw` wants
  anyway.)
- **Effort:** S (<30 min)
- **Risk:** low — same result, one initialisation instead of thousands; the only
  care needed is thread-safety, and all callers are on the UI thread.
- **User-visible:** no.

---

## 8. Help button / F1 does nothing (WinHelp + missing `.hlp`)

- **Category:** UX-Feature
- **Evidence:** `NekoCFG.cpp:26`, `NekoCFG.cpp:336`, `NekoCFG.cpp:388`,
  `NekoCFG.cpp:394`, and the same `WinHelp` pair at `NekoCFG_Tabs.cpp:236/242`,
  `280/286`, `442/448`, `507/513`, `594/600`
- **What is wrong today:** The dialog advertises help in three separate ways
  (the `IDC_HELP` button, `WM_HELP`, and `WM_CONTEXTMENU`) and every one of them
  funnels into:

  ```c
  // NekoCFG.cpp:26
  const char* szHelpFile = "Neko98.hlp";
  ...
  // NekoCFG.cpp:336
  WinHelp( hDlg, szHelpFile, HELP_FINDER, 0 );
  ```

  Two problems compound. First, **no help file exists**: a recursive search for
  `*.hlp` over the whole tree returns zero files, and `dist\` contains only the
  two executables and `使用说明.txt`. Second, **`WinHelp` itself is no longer
  present on Windows 10/11** — the viewer was removed and only exists if the
  user installed the separate `winhlp32` package (and even then it is
  deprecated). So pressing F1 or 帮助 today produces, at best, a
  "Windows Help has been discontinued" prompt and, at worst nothing at all.
  `WinHelp` also relies on an incorrectly-cast `LPARAM`/`WPARAM` for the
  `LPHELPINFO` path, which is fragile even where it does work.
- **Proposed change:** Either (a) delete the `WinHelp` calls and the `IDC_HELP`
  button, and point the help action at the already-shipped `使用说明.txt`
  (`ShellExecute(NULL, "open", "使用说明.txt", ...)`, with the same
  file-missing guard that `ExecuteConfigProcess` uses at `Neko98.cpp:31-35`), or
  (b) convert the documentation to a compiled HTML help file (`.chm`) and call
  `HtmlHelp` instead of `WinHelp`. Option (a) is a few lines and cannot fail on
  a modern OS; option (b) is more work but keeps context-sensitive help.
- **Effort:** S (<30 min) for (a); M for (b).
- **Risk:** low — removing dead UI paths.
- **User-visible:** yes — the help affordance currently lies to the user.

---

## 9. `DestroyIcon` on a shared `LoadIcon` handle

- **Category:** Robustness
- **Evidence:** `Tray.cpp:31`, caller `Neko98.cpp:91`
- **What is wrong today:** `CTray::AddIcon` deletes the handle it was given:

  ```c
  // Tray.cpp:31
  DestroyIcon( tnid.hIcon );
  ```

  but the only caller passes a shared, loader-owned resource handle:

  ```c
  // Neko98.cpp:91
  Tray->AddIcon( g_hWndMain, LoadIcon( g_hInstance, MAKEINTRESOURCE( IDI_AWAKE )), 1 );
  ```

  `LoadIcon` returns a **shared** icon; the documentation for `DestroyIcon`
  says a handle obtained from `LoadIcon` (or `LoadImage` with `LR_SHARED`) must
  not be passed to it. Destroying a shared handle can corrupt the icon for any
  other consumer and leaves `AddIcon`/`RemoveIcon` cycles re-registering a
  destroyed handle — which is exactly the path taken when the user toggles the
  tray-icon checkbox in the settings dialog (`Neko98.cpp:88-97`) or when item 1
  is fixed and the icon is re-added after an Explorer restart. In practice the
  loader often keeps the resource alive so the symptom is intermittent, which
  makes it a latent bug rather than a reproducible one.
- **Proposed change:** Delete the `DestroyIcon( tnid.hIcon );` line and let the
  loader own the handle, or — better, since item 1 needs `CTray` to hold a
  handle for re-registration — give `CTray` ownership of a private copy:
  `LoadImage(..., LR_DEFAULTSIZE)` (private, not `LR_SHARED`) or
  `CopyIcon(LoadIcon(...))`, store it in a member, and `DestroyIcon` it exactly
  once in `~CTray`. Use a sensible size rather than `LR_DEFAULTSIZE` so the
  icon is crisp at 150% DPI, matching the rest of the DPI work.
- **Effort:** S (<30 min)
- **Risk:** low — the change is either "remove a call" or "own one handle
  carefully"; the failure mode today is already undefined.
- **User-visible:** maybe — an intermittently missing or corrupted tray icon.

---

## 10. No repaint hook for lock/unlock, RDP reconnect, resume

- **Category:** Robustness
- **Evidence:** `TransparentPet.cpp:334-366` (`WndProc` handles only
  `WM_CREATE`, `WM_ERASEBKGND`, `WM_TIMER`), `TransparentPet.cpp:911-915`
  (`Erase` is a deliberate no-op), absence of `WM_WTSSESSION_CHANGE` /
  `WM_POWERBROADCAST` (grep over all `*.cpp`/`*.h` → no matches)
- **What is wrong today:** The layered window has no `WM_PAINT` path at all —
  its content exists only in the DIB that `Redraw` pushed with
  `UpdateLayeredWindow` (`TransparentPet.cpp:896`), and `Erase()` deliberately
  does nothing:

  ```c
  // TransparentPet.cpp:911-915
  void CTransparentPet::Erase()
  {
      //Nothing to erase: the whole surface is recomposited on every Draw(), and
      //footprints are deliberately kept so they can fade out.
  }
  ```

  That is a clean design while the process keeps drawing, and the 200 ms tick
  normally repaints within a fifth of a second. But there is no hook for the
  events that drop layered-window contents: a workstation lock/unlock cycle, an
  RDP session connecting/reconnecting (per-pixel-alpha windows are known to come
  back blank), or a resume from sleep. Between such an event and the next
  `Update()` the window can sit empty, and if the pet happens to be in a
  stationary state (`STOP`/`SLEEP`, where `Draw` is skipped by the
  `nImage != m_nLastIcon` test — see `Pet.cpp:104-113`) it can stay blank
  noticeably longer because no `Redraw` is issued while the image does not
  change.
- **Proposed change:** Add handlers in `CTransparentPet::WndProc` for
  `WM_WTSSESSION_CHANGE` (after `WTSRegisterSessionNotification( m_hWnd, NOTIFY_FOR_THIS_SESSION )`, which also needs `wtsapi32` in the link line —
  `build_pet.bat:27`) and `WM_POWERBROADCAST` (`PBT_APMRESUMEAUTOMATIC`), plus
  `WM_DISPLAYCHANGE`, and have them set a dirty flag that forces the next
  `Redraw` (or call `Redraw()` directly). A cheaper, API-free alternative that
  catches most of the same cases: repaint unconditionally from
  `Neko98.cpp`'s `WM_SETTINGCHANGE`/`WM_DISPLAYCHANGE` branch
  (`Neko98.cpp:125-127`), which today only sets `g_fNeedReinit` and therefore
  rebuilds everything (see item 12) rather than simply repainting.
- **Effort:** S (<30 min) for the power/display-change variant; M if
  `wtsapi32` session notifications are added.
- **Risk:** low — purely additive repaint triggers.
- **User-visible:** yes — the pet reappears immediately after an unlock or an
  RDP reconnect instead of remaining invisible until it next animates.

---

## 11. `NumNekos` read from the registry is unbounded

- **Category:** Robustness
- **Evidence:** `Neko98.cpp:52-65`, writer `NekoCFG.cpp:528`,
  reader `NekoCFG.cpp:599-620`
- **What is wrong today:** The count is taken from the registry verbatim and
  used to size an allocation and a loop:

  ```c
  // Neko98.cpp:52-65
  DWORD dwNumCats = 0;
  NekoSettings->GetInt( szNekoNumCatsKey, &dwNumCats );
  g_nNumCats = dwNumCats;
  ...
  g_NekoArray = new PNeko[g_nNumCats];
  for( i = 0; i < g_nNumCats; i++ ) { ... g_NekoArray[i] = new CNeko( szName ); }
  ```

  `NumNekos` is a plain `REG_DWORD` under
  `HKCU\Software\DHSoft\Neko95` (`NekoCommon.h:39`), so it is trivially
  editable by any registry tool, a broken installer, a bad backup/restore, or
  ordinary data rot. A value in the millions makes startup allocate that many
  pointers and instantiate that many `CNeko` objects, each of which extracts 32
  icons from disk (`Neko.cpp:484`) and builds a layered window and a full
  32-entry ARGB sprite set (`TransparentPet.cpp:409-451`). The result is a hang
  or an out-of-memory with no diagnostic. Note the same value is used at
  `Neko98.cpp:85` for the (dead, see item 13) sleep-time computation and at
  `NekoCFG.cpp:599-620` to walk the settings, so all three sites want the same
  clamp.
- **Proposed change:** Clamp on both sides. In `Neko98.cpp` right after the
  read, and in `NekoCFG.cpp:599-620`:

  ```c
  #define NEKO_MAX_CATS 20
  if( dwNumCats > NEKO_MAX_CATS ) dwNumCats = NEKO_MAX_CATS;
  ```

  20 is not arbitrary: `Neko98.cpp:85`'s existing formula
  `200 - ( 5 * g_nNumCats )` already produces a floor of 100 ms at exactly 20
  cats, so that is the number the original author had in mind as the ceiling.
  Also guard the `new` results (both `new PNeko[n]` and each `new CNeko`) and
  fall back to a single default cat on failure, matching the `g_nNumCats == 0`
  path at `Neko98.cpp:56-62`.
- **Effort:** S (<30 min)
- **Risk:** low — only rejects values the UI cannot produce anyway.
- **User-visible:** no (except that a corrupted setting no longer wedges
  startup).

---

## 12. `WM_DISPLAYCHANGE` tears down and rebuilds every cat

- **Category:** Performance
- **Evidence:** `Neko98.cpp:125-130`, `Neko98.cpp:40-81`, `Neko.cpp:442-517`
- **What is wrong today:** A resolution change, a monitor hot-plug, a color-depth
  change or the settings dialog's Apply all collapse into one action:

  ```c
  // Neko98.cpp:125-130
  case WM_DEVMODECHANGE:
  case WM_DISPLAYCHANGE:
  case WM_SETTINGCHANGE:
  case MY_UPDATENEKO:
      g_fNeedReinit = TRUE;
      break;
  ```

  and the next `WM_TIMER` runs the full `LoadConfiguration()`
  (`Neko98.cpp:153-156`), which deletes **every** `CNeko` (`Neko98.cpp:48`) and
  constructs them again from the registry (`Neko98.cpp:76`). Each reconstruction
  re-extracts 32 icons from the icon library and 8 footprint icons
  (`Neko.cpp:484-513`), rebuilds all 32 ARGB sprites
  (`TransparentPet.cpp:409-451`), destroys and recreates the layered window, and
  re-randomises the cat's position (`Neko.cpp:180-189`). For a pure geometry
  event such as plugging in a projector — where nothing about the configuration
  changed — that is a large amount of work and it visibly resets the pet.
- **Proposed change:** Split the two concerns. Keep `MY_UPDATENEKO` (a real
  settings change) as a full reload, but handle geometry changes with a
  lightweight path: refresh the pet-world bounds (`NekoWorldSize`, used in the
  `CTransparentPet` constructor at `TransparentPet.cpp:201-203`) and force each
  pet to drop and re-create its surface and re-clamp its position — without
  deleting the `CNeko`s or re-reading the registry. In practice: add a virtual
  `CPet::OnDisplayChange()` that `CTransparentPet` implements as
  `DropSurface()` + rebuild sprites for the new DPI + `Redraw()`, and call it
  from a new `g_fNeedGeometryRefresh` flag rather than routing through
  `LoadConfiguration()`.
- **Effort:** M (1-3 h)
- **Risk:** medium — the DPI-scale path already rebuilds sprites inside
  `Redraw` (`TransparentPet.cpp:732-739`), so the new hook must not fight it;
  the safe subset is "only do this for `WM_DISPLAYCHANGE`/`WM_DEVMODECHANGE`,
  leave `WM_SETTINGCHANGE` on the full reload".
- **User-visible:** no (except that the cat no longer teleports when a display
  is plugged in).

---

## 13. `g_dwSleepTime` is dead; two timers never unified; `WM_TIMER` ignores wParam

- **Category:** Code-quality
- **Evidence:** `Neko98.cpp:25`, `Neko98.cpp:83-85`, `Neko98.cpp:150`,
  `Neko98.cpp:231`, `TransparentPet.cpp:938`
- **What is wrong today:** Three related loose ends around the timing model.

  ```c
  // Neko98.cpp:25
  DWORD g_dwSleepTime = 100;
  ...
  // Neko98.cpp:83-85
  g_dwSleepTime = 100;
  if( g_nNumCats <= 20 ) g_dwSleepTime = 200 - ( 5 * g_nNumCats );
  ```

  `g_dwSleepTime` is written in two places and **read nowhere** — a grep over the
  whole tree finds it only at lines 25, 84 and 85. The adaptive "more cats ⇒ tick
  faster" intent it encodes is therefore not implemented; the actual timer is a
  hardcoded 200 ms at `Neko98.cpp:231` and ignores the number of cats entirely.
  Meanwhile the fade timer is a second, independent timer at a different period
  (`TransparentPet.cpp:938`, 60 ms), so the process wakes ~21 times a second
  instead of the ~5 the logic actually needs. Finally the message handler does
  not check which timer fired:

  ```c
  // Neko98.cpp:150
  case WM_TIMER:
  ```

  With only one timer on that window today this is harmless, but any second
  timer on `g_hWndMain` (a future tray animation, a debounce, the item 6
  coalescing tick) would silently drive `Update()`.
- **Proposed change:** (a) Either delete `g_dwSleepTime` and the two lines that
  compute it, or actually use it — recompute and apply the period in
  `LoadConfiguration` with `SetTimer( g_hWndMain, ID_TIMER_NEKO, g_dwSleepTime, NULL )`
  so the adaptive behaviour works as intended. (b) Move the footprint fade onto
  the same timer (or onto a `PostMessage`-driven dirty flag) so there is exactly
  one periodic wakeup; if a faster fade is genuinely wanted, drive it from the
  same tick with a sub-counter. (c) Add `if( wParam != ID_TIMER_NEKO ) break;`
  to the `WM_TIMER` branch.
- **Effort:** S (<30 min)
- **Risk:** low — the cleanup is mechanical; applying `g_dwSleepTime` changes
  animation speed distribution, so test with 1 and 20 cats.
- **User-visible:** no (unless the adaptive timer is enabled, in which case
  many-cat setups get smoother).

---

## 14. Surface shrink heuristic leaves a DIB up to 4× oversized

- **Category:** Performance
- **Evidence:** `TransparentPet.cpp:794-822`, `TransparentPet.cpp:836`
- **What is wrong today:** The surface is only ever grown by `EnsureSurface`
  (`TransparentPet.cpp:276` returns early when the existing surface is big
  enough), and `Redraw` decides to force a rebuild only for extreme shrinkage:

  ```c
  // TransparentPet.cpp:814-821
  BOOL bForce = FALSE;
  if( m_surfW > monW || m_surfH > monH ) bForce = TRUE;
  else if( m_hMemBmp )
  {
      long long cur = (long long)m_surfW * m_surfH;
      long long need = (long long)surfW * surfH;
      if( cur > need * 4 && cur > (long long)256 * 256 ) bForce = TRUE;
  }
  ```

  So a surface that has ballooned because the cat dragged a long trail across
  the screen can remain **just under 4×** the current need indefinitely — for
  example 900×900 (810 000 px) retained while the frame only needs 450×450
  (202 500 px). Every subsequent frame then pays the `memset` at
  `TransparentPet.cpp:836` and the `UpdateLayeredWindow` at
  `TransparentPet.cpp:896` on the whole oversized surface: roughly 4× the
  memory bandwidth and 4× the composited pixels, forever, until the cat
  triggers a bigger bounding box or moves to another monitor. There is also no
  hysteresis consideration: dropping the threshold to `cur > need * 2` bounds
  the waste at 2× at the cost of an occasional reallocation, which is far
  cheaper than paying it on every frame.
- **Proposed change:** Tighten the ratio and add an absolute slack term, e.g.
  rebuild when `cur > need * 2 + 64 * 1024`, keeping the existing
  `cur > 256*256` floor so a 32 px cat never churns. If the reallocation
  frequency is a concern, use hysteresis (shrink at 2×, grow at 1×) — the
  64-pixel rounding at `TransparentPet.cpp:799-800` already provides most of the
  growth-side slack.
- **Effort:** S (<30 min)
- **Risk:** low — a missed shrink only costs performance; the bound is a single
  comparison whose correctness is easy to reason about.
- **User-visible:** no.

---

## 15. 7 integer divisions per blended pixel; linear sprite-cache scan

- **Category:** Performance
- **Evidence:** `TransparentPet.cpp:592-636`, `TransparentPet.cpp:569-583`
- **What is wrong today:** The compositing inner loop performs seven integer
  divisions by 255 per pixel in the general case:

  ```c
  // TransparentPet.cpp:626-629
  pDst[0] = (BYTE)(((int)pS[0] * alphaMul) / 255 + ((int)pDst[0] * inv) / 255);
  pDst[1] = (BYTE)(((int)pS[1] * alphaMul) / 255 + ((int)pDst[1] * inv) / 255);
  pDst[2] = (BYTE)(((int)pS[2] * alphaMul) / 255 + ((int)pDst[2] * inv) / 255);
  pDst[3] = (BYTE)(sa + ((int)pDst[3] * inv) / 255);
  ```

  Integer division by a non-power-of-two constant is not free even when the
  compiler strength-reduces it to a multiply-shift sequence, and the loop runs
  over the whole surface (item 14) for every footprint plus the cat (item 6).
  There is also a redundant source-scale factor: `pS[]` values are already
  premultiplied by the sprite's own alpha
  (`TransparentPet.cpp:550-553`), yet they are multiplied by `alphaMul` again
  per pixel rather than scaling `alphaMul` once. Separately, the footprint
  sprite cache is searched linearly:

  ```c
  // TransparentPet.cpp:571-572
  for( int i = 0; i < m_ncache; i++ )
      if( m_cache[i].hIcon == hIcon ) return &m_cache[i];
  ```

  The cache is at most 16 entries and holds at most 8 today, so the scan is
  cheap — this half is a hygiene issue, not a hotspot.
- **Proposed change:** For each `BlendSprite` call, precompute the scaled
  reciprocal once (`int k = alphaMul; int k2 = ...;`) and replace the four
  `/255` divisions with the standard `(v * k + 127) / 255` → `((v * k + 128) * 0x8081) >> 23`
  form, or better, hoist `alphaMul` out of the per-pixel expression by scaling
  the **destination** alpha once — i.e. compute the footprint's effective alpha
  (`sa`) once per footprint and blend with a 256-entry `BYTE` multiply table.
  Since the loop is already split into an `sa <= 0` skip and an `sa >= 255` fast
  path, adding a third branch for the common integer-`sa` case with a
  precomputed table is straightforward. For the cache, the honest fix is to
  key it on `(hIcon, w, h)` rather than `hIcon` alone — see item 18's sibling
  note — and only switch to a hash if the count ever grows.
- **Effort:** S (<30 min) for the division hoist; the full table rewrite is
  still S/M.
- **Risk:** low — bit-identical if the rounding form is chosen correctly;
  compare against `test_render.cpp` before and after.
- **User-visible:** no.

---

## 16. `PlaySound(SND_FILENAME)` re-reads the WAV from disk every time

- **Category:** Performance
- **Evidence:** `Neko.cpp:636`, `Neko.cpp:640`, `Neko.cpp:646-648`
- **What is wrong today:** Sound playback is already correctly asynchronous —
  every call passes `SND_ASYNC`, so the UI thread is **not** blocked:

  ```c
  // Neko.cpp:636
  PlaySound( m_szSndAwake, NULL, SND_NOSTOP|SND_NOWAIT|SND_FILENAME|SND_NODEFAULT|SND_ASYNC );
  ```

  So the audit question "is sound playback blocking?" answers **no** for the pet
  itself. (The one genuinely blocking call is the *preview* in the settings
  dialog, `NekoCFG_Tabs.cpp:370`, which uses `SND_SYNC` and freezes the dialog
  for the length of the WAV — worth changing to `SND_ASYNC` with the existing
  "无法播放音频文件！" failure path moved to a completion check, though
  `SND_ASYNC` gives no completion callback so the message box would have to go.)
  What remains wasteful is `SND_FILENAME`: the wave file is opened, parsed and
  decoded by the OS on **every** playback. With five possible sounds and a
  frequency-gated playback (`Neko.cpp:629-631`), a long session with the sound
  frequency turned up re-opens the same files many times a minute. `SND_NOSTOP`
  also means a sound already playing silently suppresses the new one, which is
  intended but interacts with the reload cost.
- **Proposed change:** Load each configured WAV once into memory when the
  settings are read (`CNeko`'s constructor, `Neko.cpp:89-112`), store the buffer
  in a member alongside the path, and play with
  `PlaySound( pBuffer, NULL, SND_MEMORY|SND_NOSTOP|SND_NOWAIT|SND_NODEFAULT|SND_ASYNC )`.
  `SND_MEMORY` requires the buffer to remain valid for the duration of the
  asynchronous playback — standard practice is a double buffer or freeing only
  on destruction, since the five sounds are short. Free the buffers in the
  destructor next to the footprint-icon cleanup (`Neko.cpp:198-202`), and
  re-load them in the settings-reload path (which today destroys and recreates
  the `CNeko`, so the destructor/constructor pair already handles it).
- **Effort:** M (1-3 h) — buffer lifetime management across the async boundary
  plus the settings-reload path.
- **Risk:** low — a mistake shows up as a missing or truncated sound, not a
  crash, provided the buffer outlives playback.
- **User-visible:** no — possibly a slightly snappier sound onset.

---

## 17. No global hotkey to pause/hide the pet

- **Category:** UX-Feature
- **Evidence:** absence of `RegisterHotKey` anywhere in the tree (verified by a
  grep over all `*.cpp`/`*.h`); the only input paths are the tray menu
  (`Tray.cpp:58-75`) and the hidden window's `WM_COMMAND`
  (`Neko98.cpp:112-123`)
- **What is wrong today:** There is no keyboard control at all. To silence the
  pet the user must find the tray icon (which may be hidden in the overflow
  flyout), right-click, and choose an item — and today that menu has no pause
  (item 4), so the real answer is "open the settings dialog and set NumNekos to
  0". For a pet that is deliberately always on screen while you work, a single
  key combination to pause/show/hide is the difference between keeping it and
  uninstalling it.
- **Proposed change:** `RegisterHotKey( g_hWndMain, ID_HOTKEY_PAUSE, MOD_CONTROL|MOD_ALT, 'N' )`
  in `InitApplication` (`Neko98.cpp:173-210`), a `WM_HOTKEY` branch in `WndProc`
  (`Neko98.cpp:103`) toggling the same pause flag item 4 introduces, and
  `UnregisterHotKey` in the shutdown path next to `KillTimer`
  (`Neko98.cpp:242`). Make the combination a registry setting with a sensible
  default, and check the return of `RegisterHotKey` — if another application
  already owns the combination, tell the user once (`MessageBox`) rather than
  silently doing nothing. Display the current shortcut in the tray tooltip.
- **Effort:** M (1-3 h) — hotkey registration, failure handling, and a settings
  UI to change it.
- **Risk:** low — a failure to register affects only the convenience feature.
- **User-visible:** yes.

---

## 18. Registry string reads are NUL/type-unsafe; inconsistent value types

- **Category:** Code-quality
- **Evidence:** `NekoSettings.cpp:36-40` (`GetString`), `NekoSettings.cpp:42-47`
  (`GetInt`), `NekoSettings.cpp:49-54` (`GetBool`), `NekoSettings.cpp:57-73`
  (setters), `NekoCFG.cpp:467` vs `NekoCFG.cpp:499`
- **What is wrong today:** All three getters ignore the value's registry type,
  and `GetString` hands the caller's buffer size straight to the API without
  guaranteeing termination:

  ```c
  // NekoSettings.cpp:36-40  (GetString)
  LONG lResult = RegQueryValueEx( m_hKey, szValueName, NULL, NULL, (LPBYTE)szValue, &size );
  ```

  A `REG_SZ` whose data fills the buffer is not guaranteed to be
  NUL-terminated on return (and a non-`REG_SZ` value is copied in verbatim).
  The destination of that call is routinely passed straight into string
  operations: `m_szLibname`/`m_szFootprintLibname` go into `ExtractIcon`
  (`Neko.cpp:484`, `Neko.cpp:508`) and the cat name is `strcat`-ed to build a
  registry key (`Neko.cpp:81-85`). An unterminated buffer there is an overread.
  The setters are inconsistent in the same area:

  ```c
  // NekoSettings.cpp:69-73   (SetBool -> REG_BINARY)
  LONG lResult = RegSetValueEx( m_hKey, szValueName, 0, REG_BINARY, (const BYTE*)&bValue, sizeof(BOOL) );
  ```

  but `On Top` is written with `SetInt` (`NekoCFG.cpp:467`
  `NekoSettings.SetInt( szNekoOnTopKey, cat->fAlwaysOnTop );`) while `Taskbar
  Icon` uses `SetBool` (`NekoCFG.cpp:499`) — two booleans, two different
  registry types, both readable only because the getters ignore the type and
  both values happen to be 4 bytes. That is an accident, not a contract.
- **Proposed change:** In `GetString`, force termination
  (`szValue[size - 1] = '\0';` after a successful read, and treat
  `ERROR_MORE_DATA` as a truncation) and pass a valid `dwType` out; in `GetInt`
  and `GetBool`, verify `dwType` is `REG_DWORD`/`REG_BINARY` and fall back to
  the caller's initial value otherwise. Make `SetBool` write `REG_DWORD` like
  `SetInt` (or make `On Top` use `SetBool`) so booleans have one representation,
  and add a one-line migration so existing installs' `REG_BINARY` `Taskbar Icon`
  value is still honoured. Also consider that `KEY_ALL_ACCESS` in the
  constructor (`NekoSettings.cpp:17`) asks for far more than the class uses —
  `KEY_READ|KEY_WRITE` (which the non-creating branch already uses at
  `NekoSettings.cpp:21`) is sufficient.
- **Effort:** S (<30 min)
- **Risk:** low — the fallbacks preserve today's behaviour for well-formed
  values, which is all the app writes.
- **User-visible:** no.

---

## 19. Version resource contradicts itself; copyright is `?1999`

- **Category:** Compatibility
- **Evidence:** `Neko95.rc:31-32`, `Neko95.rc:50`, `Neko95.rc:52`,
  `Neko95.rc:57`
- **What is wrong today:** The numeric and string versions disagree, and the
  copyright contains a literal question mark where the `©` character should be
  (the file was presumably re-encoded at some point, dropping the non-ASCII
  byte):

  ```
  Neko95.rc:31  FILEVERSION 1,0,0,1
  Neko95.rc:32  PRODUCTVERSION 1,0,0,1
  Neko95.rc:50  VALUE "FileVersion", "4.0h\0"
  Neko95.rc:52  VALUE "LegalCopyright", "Copyright ?1999\0"
  Neko95.rc:57  VALUE "ProductVersion", "4.0h\0"
  ```

  So Explorer's Properties → Details shows version `4.0h` for a binary whose
  fixed version block is `1,0,0,1`, and the copyright line reads
  `Copyright ?1999`. On a modern Windows this is the only place the program
  identifies itself, and it currently looks like corrupted metadata. (The
  related packaging note: `neko_manifest.xml` has **no** `trustInfo` /
  `requestedExecutionLevel` element, while `NekoCFG\manifest_cfg.xml` does —
  the pet therefore relies on installer-detection heuristics to decide its
  elevation, which is a bad idea for a program that ShellExecutes a sibling
  executable at `Neko98.cpp:33`. Adding
  `<requestedExecutionLevel level="asInvoker" uiAccess="false"/>` to the pet's
  manifest is a one-line fix in the same area.)
- **Proposed change:** Pick one version and use it in all four places (the
  advertised feature set suggests bumping to something like `1,0,0,2` /
  `"1.0.0.2"` rather than preserving the contradictory `4.0h`), replace the
  stray `?` with `©`, and add the `asInvoker` trust-info block to the pet's
  manifest. While in the file, check the other string values for the same
  encoding damage.
- **Effort:** S (<30 min)
- **Risk:** low — resource-only change.
- **User-visible:** minor — Explorer's file properties become correct.

---

## Notes, non-findings, and things I could not confirm

- **`GetSprite`'s 16-entry cache does not currently overflow.** The cap is
  `TransparentPet.cpp:574` (`if( m_ncache >= 16 ) return NULL;`) and the caller
  tolerates `NULL` (`TransparentPet.cpp:847`). The cache is only ever populated
  from `Redraw`'s footprint loop (`TransparentPet.cpp:846`) with the eight
  footprint `HICON`s, and it is cleared by `FreeSprites`
  (`TransparentPet.cpp:391-406`) on every DPI rebuild — so at most 8 of 16
  entries are used and the `NULL` path is unreachable today. It is a latent
  trap rather than a live bug: the lookup keys on `hIcon` alone
  (`TransparentPet.cpp:571-572`) and ignores `w`/`h`, so if the cache were ever
  populated with the same icon at two scales without an intervening
  `FreeSprites`, the stale-size sprite would be returned. Reporting this
  accurately rather than as a leak.
- **The `Render`/composite path does not leak handles.** `DropSurface`
  (`TransparentPet.cpp:312-331`) restores the old bitmap, deletes the DC and the
  DIB, and `FreeSprites` (`TransparentPet.cpp:391-406`) deletes every sprite
  bitmap plus every cache bitmap. `BuildARGB` frees both `ReadBitmap32` buffers
  and both `GetIconInfo` bitmaps (`TransparentPet.cpp:558-561`). `CNeko`'s
  destructor destroys the eight footprint icons (`Neko.cpp:201`) and
  `CPet::DestroyImages` destroys the 32 cat icons (`Pet.cpp:94`). The one
  genuinely suspect handle is the shared `LoadIcon` result at `Tray.cpp:31`
  (item 9). Two smaller leaks I did find and did not raise as separate items
  because they are cosmetic and bounded: the `IDR_PLAYBITMAP` loaded at
  `NekoCFG_Tabs.cpp:303` is never freed, and `LoadImage(...,IMAGE_ICON)` at
  `NekoCFG_Tabs.cpp:81` is freed per-icon in the loop
  (`NekoCFG_Tabs.cpp:83`) so the image list holds stale bitmaps after
  `ShutdownPropertyDialog` (`NekoCFG_Tabs.cpp:113`).
- **Registry access frequency is not a hot path.** `CNeko`'s constructor opens
  one key per cat (`Neko.cpp:89`) and reads roughly 14 values, and
  `LoadConfiguration` opens the root once (`Neko98.cpp:43`). That is fine. The
  real registry issue is not frequency but the missing bounds/type validation
  (items 11 and 18). I found no repeated `CreateKey`/`OpenKey` churn.
- **Windows 11 / ARM:** I could not test on ARM64. The build
  (`build_pet.bat:3`) hardcodes a WinLibs POSIX UCRT MinGW toolchain and
  `-mwindows`; nothing in the source is x86-specific, so an ARM64 build should
  be a toolchain matter only. The one ARM-relevant behavioural note is that
  `DpiScaleForPoint` (item 7) depends on `shcore.dll`'s `GetDpiForMonitor`,
  which is present on ARM64 Windows 11, so that is fine.
- **Multi-monitor hot-plug** is handled coarsely through `WM_DISPLAYCHANGE`
  (item 12): the pet does recover, but by rebuilding everything. `NekoWorldSize`
  is read from the `SM_*VIRTUALSCREEN` metrics at construction time
  (`TransparentPet.cpp:201-203`), so the pet-world bounds are only correct as
  long as the rebuild happens — which item 12 preserves.
- **`test_render.cpp`, `probe_pet.cpp`, `dpi_probe.cpp`, `parse_res.py`** are
  not part of either executable (`build_pet.bat:21` lists only the seven real
  translation units) and are not shipped (`dist\` has the two `.exe`s and
  `使用说明.txt`). I treated them as tooling.
- **Dead legacy sources:** `DesktopHack.cpp`, `DesktopPet.cpp` and
  `AlwaysOnTopPet.cpp` are present but not compiled. They are still useful as
  documentation of the original behaviour (which is how item 2 was established),
  but they are also ~600 lines of unreferenced code in the shipped source tree;
  moving them to an `attic/` folder is a zero-risk cleanup.
