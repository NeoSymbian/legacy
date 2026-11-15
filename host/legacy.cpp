// SPDX-License-Identifier: Apache-2.0
// LEGACY.EXE -- the old Symbian apps inside neoui (NeoSymbian/legacy): neoui's "Old apps" screen and its guest window
// server.
//
// neoui owns the display and the input, so the stock window server cannot. This neoui app:
//   1. makes the global chunk "NeoUiLegacy": a header with the guest screen's geometry (360x640, the nHD screen the
//      S60 apps were laid out for; a panel-sized guest also runs the window server out of memory on the phone), then
//      the pixels. The patched screen driver (graphics scnew.cpp) takes its size from that header and draws there, and
//      the patched window server (WSTOP.CPP, EVENT.CPP) runs as a guest: no display or event-hook ownership;
//   2. starts EWSRV.EXE -NoShell, and keeps it while it runs: one guest desktop for every old app;
//   3. asks AppArc (the apparc server) for the installed apps and sends each one, with its icon rasterized by Avkon's
//      icon server, to neoui as a launcher entry (protocol 1.6 ENTRY): neoui draws the Old apps grid;
//   4. on a tap (the Entry event) starts that app through AppArc and brings its surface to the front (FRONT): the
//      guest screen, scaled to fit, with neoui's taps and keys turned into raw events for the guest window server
//      (RWsSession::SimulateRawEvent). Back or the app's end closes the app and gives the grid back.
// With a program on the command line ("grid", the MiniGUI test app) it runs that one at once, as before AppArc.

#include <e32base.h>
#include <e32debug.h>
#include <e32svr.h>
#include <e32keys.h>
#include <f32file.h>
#include <w32std.h>
#include <apgcli.h>
#include <apasvst.h>
#include <apacmdln.h>
#include <fbs.h>
#include <AknIconUtils.h>
#include <AknIconSrvClient.h>

namespace {

// --- the NeoUi protocol (neoui/crates/neoui-proto/src/ipc.rs) ---
_LIT(KNeoUi, "NeoUi");
const TInt KGeometry = 1, KSurface = 2, KDamage = 3, KNextEvent = 4, KNav = 6, KEntry = 13, KFront = 14;
const TInt KNavBack = 1, KNavOptions = 2;
const TUint KTouch = 1, KKey = 2, KQuit = 3, KBack = 4, KOptions = 5, KVisible = 6, KEntryEvent = 12;
const TInt KTitleMax = 48;			// a caption's UTF-8 bytes (neoui TITLE_MAX)
const TInt KIconSize = 128;			// the icons' pixels a side (neoui ENTRY_ICON_MAX; the grid scales them)

class RNeoUi : public RSessionBase
	{
public:
	TInt Connect() { return CreateSession(KNeoUi, TVersion(1, 0, 0), 4); }
	TInt Geometry(TDes8& aOut) { return SendReceive(KGeometry, TIpcArgs(&aOut)); }
	TInt Surface() { return SendReceive(KSurface); }
	TInt Damage(TInt aX, TInt aY, TInt aW, TInt aH) { return SendReceive(KDamage, TIpcArgs(aX, aY, aW, aH)); }
	void NextEvent(TDes8& aOut, TRequestStatus& aStatus) { SendReceive(KNextEvent, TIpcArgs(&aOut), aStatus); }
	TInt Nav(TInt aClaims) { return SendReceive(KNav, TIpcArgs(aClaims)); }
	TInt Entry(TUint aId, const TDesC8& aCaption, const TDesC8& aIcon)
		{ return SendReceive(KEntry, TIpcArgs(aId, &aCaption, &aIcon)); }
	TInt Front(TBool aFront) { return SendReceive(KFront, TIpcArgs(aFront ? 1 : 0)); }
	};

TUint Le32(const TDesC8& aB, TInt aI)
	{
	return aB[aI] | aB[aI + 1] << 8 | aB[aI + 2] << 16 | TUint(aB[aI + 3]) << 24;
	}

_LIT(KLegacyChunk, "NeoUiLegacy");
// the chunk's header, read by scnew.cpp's ScreenHal: magic "NEOL", width, height, stride, offset of the first pixel
struct TGuestHeader { TUint32 iMagic; TInt32 iWidth, iHeight, iStride, iOffset; };
const TUint32 KGuestMagic = 0x4C4F454E;
const TInt KGuestW = 360, KGuestH = 640, KGuestOffset = 32;
_LIT(KGuestWserv, "EWSRV.EXE");
_LIT(KNoShell, "-NoShell");
const TInt KFrameUs = 100000;	// shortcut: the whole frame copied 10 times a second; real damage via SubmitUpdate later

// The guest screen (HAL geometry) fitted into the surface: centred, aspect kept.
struct TFit
	{
	TInt iW, iH, iStride, iOffset;	// guest: pixels, bytes per line, bytes to the first pixel
	TInt iSw, iSh;					// surface
	TInt iDx, iDy, iDw, iDh;		// where the guest lands in the surface
	void Compute()
		{
		if (iSw * iH <= iSh * iW)
			{ iDw = iSw; iDh = iH * iSw / iW; }
		else
			{ iDh = iSh; iDw = iW * iSh / iH; }
		iDx = (iSw - iDw) / 2;
		iDy = (iSh - iDh) / 2;
		}
	TBool ToGuest(TInt aX, TInt aY, TInt& aGx, TInt& aGy) const
		{
		if (aX < iDx || aY < iDy || aX >= iDx + iDw || aY >= iDy + iDh)
			return EFalse;
		aGx = (aX - iDx) * iW / iDw;
		aGy = (aY - iDy) * iH / iDh;
		return ETrue;
		}
	};

void Blit(const TFit& aF, const TUint8* aGuest, TUint32* aSurface)
	{
	for (TInt y = 0; y < aF.iDh; ++y)
		{
		const TUint32* src = reinterpret_cast<const TUint32*>(aGuest + aF.iOffset + (y * aF.iH / aF.iDh) * aF.iStride);
		TUint32* dst = aSurface + (aF.iDy + y) * aF.iSw + aF.iDx;
		for (TInt x = 0; x < aF.iDw; ++x)
			dst[x] = 0xFF000000 | src[x * aF.iW / aF.iDw];	// guest EColor16MA -> XRGB
		}
	}

// neoui key codes (Unicode + Enter 13, Backspace 8, Esc 27, arrows 0xF807..0xF80A) -> standard scan codes
TInt ScanCode(TUint aCode)
	{
	switch (aCode)
		{
	case 13: return EStdKeyEnter;
	case 8: return EStdKeyBackspace;
	case 27: return EStdKeyEscape;
	case ' ': return EStdKeySpace;
	case EKeyLeftArrow: return EStdKeyLeftArrow;
	case EKeyRightArrow: return EStdKeyRightArrow;
	case EKeyUpArrow: return EStdKeyUpArrow;
	case EKeyDownArrow: return EStdKeyDownArrow;
	default: break;
		}
	if (aCode >= 'a' && aCode <= 'z')
		return aCode - 'a' + 'A';
	if ((aCode >= 'A' && aCode <= 'Z') || (aCode >= '0' && aCode <= '9'))
		return aCode;
	return 0;
	}

void Key(RWsSession& aWs, TInt aScan)
	{
	TRawEvent ev;
	ev.Set(TRawEvent::EKeyDown, aScan);
	aWs.SimulateRawEvent(ev);
	ev.Set(TRawEvent::EKeyUp, aScan);
	aWs.SimulateRawEvent(ev);
	aWs.Flush();
	}

// a failed start-up step, in the log (LEGACY's exit reason alone does not say which)
TInt Fail(const char* aStep, TInt aErr)
	{
	RDebug::Printf("legacy: %s failed (%d)", aStep, aErr);
	return aErr;
	}

// UTF-16 -> UTF-8 (a caption), cut at whole characters to fit aOut
void Utf8(const TDesC& aIn, TDes8& aOut)
	{
	aOut.Zero();
	for (TInt i = 0; i < aIn.Length(); ++i)
		{
		TUint c = aIn[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < aIn.Length())		// a surrogate pair
			c = 0x10000 + ((c - 0xD800) << 10) + (aIn[++i] - 0xDC00);
		TBuf8<4> u;
		if (c < 0x80)
			u.Append(c);
		else if (c < 0x800)
			{ u.Append(0xC0 | c >> 6); u.Append(0x80 | (c & 0x3F)); }
		else if (c < 0x10000)
			{ u.Append(0xE0 | c >> 12); u.Append(0x80 | (c >> 6 & 0x3F)); u.Append(0x80 | (c & 0x3F)); }
		else
			{ u.Append(0xF0 | c >> 18); u.Append(0x80 | (c >> 12 & 0x3F)); u.Append(0x80 | (c >> 6 & 0x3F));
			  u.Append(0x80 | (c & 0x3F)); }
		if (aOut.Length() + u.Length() > aOut.MaxLength())
			break;
		aOut.Append(u);
		}
	}

// The app's icon (its registration's icon file: a MIF, ids from 16384, or an MBM, ids from 0) rasterized by Avkon's
// icon server at KIconSize, as neoui's ENTRY icon: width, height (u16), then 0xAARRGGBB pixels from the bitmap and
// its mask. Empty when the app has none or it cannot be drawn (neoui shows its generic tile).
void IconL(RApaLsSession& aLs, TUid aUid, RBuf8& aOut)
	{
	HBufC* file = NULL;
	TInt err = aLs.GetAppIcon(aUid, file);
	if (err != KErrNone || !file)
		{
		RDebug::Printf("legacy: app %08x: no icon file (%d)", aUid.iUid, err);
		return;
		}
	CleanupStack::PushL(file);
	const TBool mif = file->Right(4).CompareF(_L(".mif")) == 0;
	CFbsBitmap* bitmap = NULL;
	CFbsBitmap* mask = NULL;
	AknIconUtils::CreateIconL(bitmap, mask, *file, mif ? 16384 : 0, mif ? 16385 : 1);
	CleanupStack::PushL(bitmap);
	CleanupStack::PushL(mask);
	User::LeaveIfError(AknIconUtils::SetSize(bitmap, TSize(KIconSize, KIconSize)));	// rasterizes; the mask follows
	const TSize size = bitmap->SizeInPixels();
	const TInt w = Min(size.iWidth, KIconSize), h = Min(size.iHeight, KIconSize);	// SetSize keeps the aspect
	aOut.CreateL(4 + w * h * 4);
	aOut.SetLength(4);
	aOut[0] = w & 0xFF; aOut[1] = w >> 8;
	aOut[2] = h & 0xFF; aOut[3] = h >> 8;
	// by scan line: the icon server compresses its bitmaps in the background (AknIconSrv.rss, RLE), and
	// CFbsBitmap::GetPixel does not read compressed bitmaps (it leaves the colour white); GetScanLine decodes them
	TBuf8<KIconSize * 4> line;
	TBuf8<KIconSize> alpha;
	for (TInt y = 0; y < h; ++y)
		{
		bitmap->GetScanLine(line, TPoint(0, y), w, EColor16MA);		// 0xAARRGGBB, little-endian
		if (mask)
			mask->GetScanLine(alpha, TPoint(0, y), w, EGray256);
		for (TInt x = 0; x < w; ++x)
			{
			const TUint32 rgb = line[x * 4] | line[x * 4 + 1] << 8 | line[x * 4 + 2] << 16;
			const TUint32 argb = TUint32(mask ? alpha[x] : 0xFF) << 24 | rgb;
			aOut.Append(reinterpret_cast<const TUint8*>(&argb), 4);
			}
		}
	CleanupStack::PopAndDestroy(3, file);
	}

struct TOldApp
	{
	TUid iUid;
	TFileName iExe;
	};

// Every app AppArc knows (once its first scan is done), except hidden ones, to neoui as launcher entries
void PublishL(RNeoUi& aUi, RApaLsSession& aLs, RArray<TOldApp>& aApps, TBool aIcons)
	{
	TRequestStatus scanned, timeout;
	RTimer timer;
	User::LeaveIfError(timer.CreateLocal());
	aLs.SetNotify(ETrue, scanned);		// completes at once if no scan is pending
	timer.After(timeout, 20000000);
	User::WaitForRequest(scanned, timeout);
	if (scanned == KRequestPending)
		{
		RDebug::Printf("legacy: apparc's scan still running after 20 s; listing what it has");
		aLs.CancelNotify();
		User::WaitForRequest(scanned);
		}
	else
		{
		timer.Cancel();
		User::WaitForRequest(timeout);
		}
	timer.Close();

	User::LeaveIfError(aLs.GetAllApps());
	TApaAppInfo info;
	TInt err;
	while ((err = aLs.GetNextApp(info)) == KErrNone)
		{
		TApaAppCapabilityBuf cap;
		if (aLs.GetAppCapability(cap, info.iUid) == KErrNone && cap().iAppIsHidden)
			continue;
		RBuf8 icon;
		TInt iconErr = KErrNone;
		if (aIcons)
			{
			TRAP(iconErr, IconL(aLs, info.iUid, icon));
			}
		if (iconErr)
			{
			RDebug::Printf("legacy: app %08x: icon failed (%d)", info.iUid.iUid, iconErr);
			icon.Close();
			}
		TBuf8<KTitleMax> caption;
		Utf8(info.iCaption.Length() ? info.iCaption : info.iShortCaption, caption);
		if (!caption.Length())
			caption.Copy(info.iFullName.Mid(info.iFullName.LocateReverse('\\') + 1));
		const TInt sent = aUi.Entry(info.iUid.iUid, caption, icon);
		RDebug::Printf("legacy: app %08x '%S': icon %d bytes, entry %d", info.iUid.iUid, &caption, icon.Length(), sent);
		icon.Close();
		TOldApp app = { info.iUid, info.iFullName };
		aApps.AppendL(app);
		}
	if (err != RApaLsSession::ENoMoreAppsInList)
		RDebug::Printf("legacy: apparc's list ended with %d", err);
	}

void StartL(RApaLsSession& aLs, const TDesC& aExe, TThreadId& aThread)
	{
	CApaCommandLine* cmd = CApaCommandLine::NewLC();
	cmd->SetExecutableNameL(aExe);
	cmd->SetCommandL(EApaCommandRun);
	User::LeaveIfError(aLs.StartApp(*cmd, aThread));
	CleanupStack::PopAndDestroy(cmd);
	}

// Start an app: by AppArc (aUid) or, without it, the program aExe directly; aApp = its process
TInt Launch(RApaLsSession* aLs, const TDesC& aExe, RProcess& aApp)
	{
	if (!aLs)
		{
		const TInt err = aApp.Create(aExe, KNullDesC);
		if (err == KErrNone)
			aApp.Resume();
		return err;
		}
	TThreadId thread;
	TRAPD(err, StartL(*aLs, aExe, thread));
	if (err)
		return err;
	RThread t;
	if ((err = t.Open(thread)) == KErrNone)
		{
		err = t.Process(aApp);
		t.Close();
		}
	return err;
	}

TInt Run()
	{
	// a program on the command line (neoui's debug "grid") runs at once; else the Old apps grid
	TBuf<64> program;
	User::CommandLine(program);
	program.Trim();

	RNeoUi ui;
	TInt err = ui.Connect();
	if (err)
		return Fail("connect to neoui", err);
	TBuf8<12> geo;
	if ((err = ui.Geometry(geo)) != KErrNone)
		return Fail("geometry", err);
	RChunk surface;
	if ((err = surface.SetReturnedHandle(ui.Surface())) != KErrNone)
		return Fail("surface", err);

	TFit f;
	f.iSw = Le32(geo, 0);
	f.iSh = Le32(geo, 4);
	f.iW = KGuestW;
	f.iH = KGuestH;
	f.iStride = KGuestW * 4;	// EColor16MA, as the panel's 32-bpp mode
	f.iOffset = KGuestOffset;
	f.Compute();

	RChunk guest;
	const TInt size = f.iOffset + f.iStride * f.iH;
	if ((err = guest.CreateGlobal(KLegacyChunk, size, size)) != KErrNone)
		return Fail("guest screen chunk", err);
	Mem::FillZ(guest.Base(), size);
	const TGuestHeader header = { KGuestMagic, f.iW, f.iH, f.iStride, f.iOffset };
	Mem::Copy(guest.Base(), &header, sizeof(header));
	Mem::FillZ(surface.Base(), f.iSw * f.iSh * 4);

	RProcess wserv;
	if ((err = wserv.Create(KGuestWserv, KNoShell)) != KErrNone)
		return Fail("start the window server", err);
	// wait for the window server's rendezvous (WSTOP.CPP, once it serves): connecting in a retry loop while it starts
	// left a half-made session in ws32/fbscli that the next try faulted in (KERN-EXEC 3)
	TRequestStatus ready;
	wserv.Rendezvous(ready);
	wserv.Resume();
	User::WaitForRequest(ready);
	RWsSession ws;
	err = ready.Int() != KErrNone ? ready.Int() : ws.Connect();
	if (err)
		{
		wserv.Kill(0);
		return Fail("connect to the window server", err);
		}
	RDebug::Printf("legacy: guest wserv %dx%d up, surface %dx%d", f.iW, f.iH, f.iSw, f.iSh);

	RApaLsSession ls;
	RArray<TOldApp> apps;
	RProcess app;
	TBool running = EFalse;		// an old app is up (its surface in front)
	if (program.Length())
		{
		if ((err = Launch(NULL, program, app)) != KErrNone)
			{
			RDebug::Printf("legacy: cannot start the program (%d)", err);
			wserv.Kill(0);
			return err;
			}
		running = ETrue;
		ui.Nav(KNavBack | KNavOptions);
		ui.Front(ETrue);
		}
	else
		{
		const TInt fbs = RFbsSession::Connect();		// the icons' bitmaps (fbserv runs with the window server)
		// Avkon's icon server session, as CEikonEnv opens it for an app (AknIconUtils faults without it)
		const TInt icons = RAknIconSrvClient::Connect();
		if (icons)
			RDebug::Printf("legacy: no icon server (%d): generic tiles", icons);
		// the apparc server: started at boot on a 2010 phone (the starter), by us here
		if ((err = ls.Connect()) == KErrNotFound && (err = StartupApaServerProcess()) == KErrNone)
			err = ls.Connect();
		if (err == KErrNone)
			{
			TRAP(err, PublishL(ui, ls, apps, icons == KErrNone));
			}
		if (err)
			RDebug::Printf("legacy: listing the apps failed (%d)", err);	// an empty grid
		if (icons == KErrNone)
			RAknIconSrvClient::Disconnect();
		if (fbs == KErrNone)
			RFbsSession::Disconnect();
		}

	TBuf8<16> ev;
	TRequestStatus evStatus, tick;
	RTimer timer;
	timer.CreateLocal();
	ui.NextEvent(ev, evStatus);
	timer.After(tick, KFrameUs);
	TBool visible = ETrue, alive = ETrue;
	while (alive)
		{
		User::WaitForRequest(evStatus, tick);
		if (tick != KRequestPending && evStatus != KRequestPending)
			User::WaitForAnyRequest();	// both completed: two signals, WaitForRequest took one
		if (tick != KRequestPending)
			{
			if (running && visible)
				{
				Blit(f, guest.Base(), reinterpret_cast<TUint32*>(surface.Base()));
				ui.Damage(f.iDx, f.iDy, f.iDw, f.iDh);
				}
			if (running && app.ExitType() != EExitPending)
				{
				TExitCategoryName cat = app.ExitCategory();
				TBuf8<KMaxExitCategoryName + 1> cat8;
				cat8.Copy(cat);
				RDebug::Printf("legacy: program ended: type %d reason %d '%s'", app.ExitType(), app.ExitReason(),
					reinterpret_cast<const char*>(cat8.PtrZ()));
				app.Close();
				running = EFalse;
				if (program.Length())
					alive = EFalse;		// the command line's program: LEGACY ends with it
				else
					{
					ui.Nav(0);
					ui.Front(EFalse);	// back to the grid
					}
				}
			if (alive)
				timer.After(tick, KFrameUs);
			}
		if (evStatus == KRequestPending)
			continue;
		if (evStatus.Int() != KErrNone)
			break;
		const TUint kind = Le32(ev, 0), a = Le32(ev, 4), b = Le32(ev, 8), c = Le32(ev, 12);
		if (kind == KTouch && running)
			{
			TInt gx, gy;
			const TUint phase = a & 0xFF;
			if (f.ToGuest(b, c, gx, gy))
				{
				TRawEvent raw;
				raw.Set(phase == 0 ? TRawEvent::EButton1Down : phase == 2 ? TRawEvent::EButton1Up
					: TRawEvent::EPointerMove, gx, gy);
				ws.SimulateRawEvent(raw);
				ws.Flush();
				if (phase != 1)
					RDebug::Printf("legacy: raw %d %d %d", raw.Type(), gx, gy);
				}
			}
		else if (kind == KKey && a && running)
			{
			const TInt scan = ScanCode(b);
			if (scan)
				Key(ws, scan);
			}
		else if (kind == KOptions && running)
			Key(ws, EStdKeyDevice0);		// the left softkey (Options)
		else if (kind == KVisible)
			visible = a != 0;
		else if (kind == KEntryEvent)
			{
			TInt i = apps.Count();
			while (--i >= 0 && TUint(apps[i].iUid.iUid) != a) {}
			if (i < 0)
				RDebug::Printf("legacy: no app %08x", a);
			else if (!running)
				{
				err = Launch(&ls, apps[i].iExe, app);
				RDebug::Printf("legacy: start app %08x: %d", a, err);
				if (err == KErrNone)
					{
					running = ETrue;
					Mem::FillZ(surface.Base(), f.iSw * f.iSh * 4);
					ui.Nav(KNavBack | KNavOptions);
					ui.Front(ETrue);
					}
				}
			else
				ui.Front(ETrue);	// shortcut: one old app at a time; a second tile shows the running one
			}
		else if (kind == KBack && running && !program.Length())
			{
			// shortcut: killed, not asked to exit (TApaTask::EndTask needs its window group): unsaved data is lost
			app.Kill(0);
			app.Close();
			running = EFalse;
			ui.Nav(0);
			ui.Front(EFalse);
			}
		else if (kind == KQuit || kind == KBack)
			alive = EFalse;
		if (alive)
			ui.NextEvent(ev, evStatus);
		}

	RDebug::Printf("legacy: closing");
	if (tick == KRequestPending)
		{
		timer.Cancel();
		User::WaitForRequest(tick);
		}
	timer.Close();
	if (running)
		{
		if (app.ExitType() == EExitPending)
			app.Kill(0);
		app.Close();
		}
	apps.Close();
	ls.Close();
	ws.Close();
	wserv.Kill(0);
	wserv.Close();
	guest.Close();
	surface.Close();
	ui.Close();		// an outstanding NEXT_EVENT ends with the process, as for neoui's own apps
	return KErrNone;
	}

} // namespace

GLDEF_C TInt E32Main()
	{
	// the old-app failure trace (kernel euser us_trp.cpp, F32): leaves, error exits, failed lookups, file and loader
	// requests of the guest's processes go to the debug log as "neotrace:" lines.
	// shortcut: on whenever the runtime runs (bring-up); make it a setting once old apps work.
	User::SetDebugMask(UserSvr::DebugMask(3) | 0x80000000u, 3);
	CTrapCleanup* cleanup = CTrapCleanup::New();
	const TInt err = cleanup ? Run() : KErrNoMemory;
	delete cleanup;
	return err;
	}
