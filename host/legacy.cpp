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
//   5. under the guest, an on-screen key pad (pad.cpp: Navigator, Gamepad, Number, Abc, Hidden), remembered per app as
//      the setting legacy.<uid>.pad, its options from Settings → Old apps (legacy.pad.*).
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
#include <AknCapServerClient.h>
#include "pad.h"

namespace {

// --- the NeoUi protocol (neoui/crates/neoui-proto/src/ipc.rs) ---
_LIT(KNeoUi, "NeoUi");
const TInt KGeometry = 1, KSurface = 2, KDamage = 3, KNextEvent = 4, KKeyboard = 5, KNav = 6, KSettingGet = 8,
	KSettingSet = 9, KSettingWatch = 10, KEntry = 13, KFront = 14, KPage = 15, KNote = 17, KHaptic = 25;
const TInt KNavBack = 1, KNavOptions = 2, KKeyboardText = 1;
const TUint KTouch = 1, KKey = 2, KQuit = 3, KBack = 4, KOptions = 5, KVisible = 6, KSettings = 7, KKeyboardEvent = 9,
	KEntryEvent = 12, KArea = 18, KInputReset = 21;
const TUint8 KPageCanBack = 1, KPageFull = 4;		// a PAGE's flags (neoui-proto page.rs)
const TUint8 KValueBool = 1, KValueInt = 2, KValueBytes = 4;	// a setting's tag (neoui-proto settings/value.rs)
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
	TInt Keyboard(TInt aMode) { return SendReceive(KKeyboard, TIpcArgs(aMode)); }	// its height
	TInt SettingGet(const TDesC8& aKey, TDes8& aValue) { return SendReceive(KSettingGet, TIpcArgs(&aKey, &aValue)); }
	TInt SettingSet(const TDesC8& aKey, const TDesC8& aValue)
		{ return SendReceive(KSettingSet, TIpcArgs(&aKey, &aValue)); }
	TInt SettingWatch(const TDesC8& aPrefix) { return SendReceive(KSettingWatch, TIpcArgs(&aPrefix)); }
	TInt Note(const TDesC8& aNote) { return SendReceive(KNote, TIpcArgs(&aNote)); }
	TInt Page(const TDesC8& aPage) { return SendReceive(KPage, TIpcArgs(&aPage)); }	// the area's height
	TInt Haptic(TInt aMs, TInt aAmp) { return SendReceive(KHaptic, TIpcArgs(aMs, aAmp)); }
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

// The guest screen (HAL geometry) fitted into a box of the surface (above the key pad): aspect kept, centred across,
// at the box's top (or centred, aTop false).
struct TFit
	{
	TInt iW, iH, iStride, iOffset;	// guest: pixels, bytes per line, bytes to the first pixel
	TInt iSw, iSh;					// surface
	TInt iDx, iDy, iDw, iDh;		// where the guest lands in the surface
	void Compute(const TPadRect& aBox, TBool aTop)
		{
		if (aBox.iW * iH <= aBox.iH * iW)
			{ iDw = aBox.iW; iDh = iH * aBox.iW / iW; }
		else
			{ iDh = aBox.iH; iDw = iW * aBox.iH / iH; }
		iDx = aBox.iX + (aBox.iW - iDw) / 2;
		iDy = aBox.iY + (aTop ? 0 : (aBox.iH - iDh) / 2);
		}
	// a surface point in guest pixels, clamped to the guest screen (a finger that slid off it still lifts there)
	void ToGuest(TInt aX, TInt aY, TInt& aGx, TInt& aGy) const
		{
		aGx = Max(0, Min(iW - 1, (aX - iDx) * iW / Max(1, iDw)));
		aGy = Max(0, Min(iH - 1, (aY - iDy) * iH / Max(1, iDh)));
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

// The console: the guest turned a quarter (the phone's top to the right), aF in canvas coordinates (TPad::ToCanvas)
void BlitTurned(const TFit& aF, const TUint8* aGuest, TUint32* aSurface)
	{
	for (TInt u = 0; u < aF.iDw; ++u)
		{
		const TInt sx = u * aF.iW / aF.iDw;
		TUint32* dst = aSurface + (aF.iSh - 1 - (aF.iDx + u)) * aF.iSw + aF.iDy;
		for (TInt v = 0; v < aF.iDh; ++v)
			dst[v] = 0xFF000000
				| reinterpret_cast<const TUint32*>(aGuest + aF.iOffset + (v * aF.iH / aF.iDh) * aF.iStride)[sx];
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
	TBuf8<KTitleMax> iCaption;
	TBool iGame;		// AppArc's group "Games": a first open gets the Gamepad
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
		const TBool capOk = aLs.GetAppCapability(cap, info.iUid) == KErrNone;
		if (capOk && cap().iAppIsHidden)
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
		TOldApp app;
		app.iUid = info.iUid;
		app.iExe = info.iFullName;
		app.iCaption = caption;
		app.iGame = capOk && cap().iGroupName.CompareF(_L("Games")) == 0;
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

// --- the key pads: settings, the per-app profile, layout ---

// a setting as an int (a bool is 0/1); aDefault when it has no value or another type
TInt SettingInt(RNeoUi& aUi, const TDesC8& aKey, TInt aDefault)
	{
	TBuf8<8> v;
	if (aUi.SettingGet(aKey, v) < 0 || v.Length() < 2)
		return aDefault;
	if (v[0] == KValueBool && v.Length() == 2)
		return v[1];
	if (v[0] == KValueInt && v.Length() == 5)
		return TInt(Le32(v, 1));
	return aDefault;
	}

// the per-app profile, legacy.<uid8>.pad: version 1, mode, last visible pad, orientation, A and B (0 = Settings'),
// the reset epoch it was saved under (u32 LE)
const TInt KProfileLen = 10;
const TUint32 KFrozenBubble = 0xA0002AAA;	// first opens in Gamepad (the UID table)

class TView
	{
public:
	RNeoUi* iUi;
	RWsSession* iWs;
	TUint32* iSurface;
	const TUint8* iGuestPix;
	TFit iFit;
	TPad iPad;
	TPadSettings iSet;
	TInt iDefaultPad, iEpoch;
	TInt iKeyboardH;
	TPadMode iMode, iLast;		// the pad now; the last of Nav, Game and Number (Hidden's and Abc's way back)
	TUint32 iUid;
	TUint8 iA, iB;				// the app's own A and B (0: Settings')
	TBuf8<KTitleMax> iTitle;	// the app's name (its page)
	TInt iGuestX, iGuestY;
	TBool iGuestDown;
	TInt iTick;				// the key vibration's amplitude (0: off)
	TBool iConsole, iConsoleFirst;
	TInt iCleared;			// the surface rows Layout clears (the tallest area so far)	// the Gamepad's landscape console now; a game's first open is in it

	void ReadSettings()
		{
		iSet.iNumberDirs = SettingInt(*iUi, _L8("legacy.pad.dirs"), 0) == 1;
		const TInt a = iA ? iA - 1 : SettingInt(*iUi, _L8("legacy.pad.a"), 0);
		const TInt b = iB ? iB - 1 : SettingInt(*iUi, _L8("legacy.pad.b"), 0);
		iSet.iA = a == 1 ? '5' : EStdKeyDevice3;
		iSet.iB = b == 1 ? '0' : b == 2 ? EStdKeyHash : EStdKeyDevice1;
		iSet.iLefty = SettingInt(*iUi, _L8("legacy.pad.lefty"), 0) == 1;
		iDefaultPad = SettingInt(*iUi, _L8("legacy.pad.default"), 0);
		iEpoch = SettingInt(*iUi, _L8("legacy.pad.epoch"), 0);
		const TInt strength = SettingInt(*iUi, _L8("legacy.pad.strength"), 1);
		iTick = SettingInt(*iUi, _L8("legacy.pad.vibrate"), 1) ? (strength == 0 ? 40 : strength == 2 ? 110 : 70) : 0;
		iConsoleFirst = SettingInt(*iUi, _L8("legacy.pad.console"), 0) == 1;
		}

	// the app's page: its name, Back stays in it (closes it); the shell drops its header, the pad has the softkeys
	void Declare(const TDesC8& aTitle)
		{
		TBuf8<6 + KTitleMax> page;
		page.Append(KPageCanBack | (iConsole ? KPageFull : 0));
		page.AppendFill(0, 4);
		page.Append(aTitle.Length());
		page.Append(aTitle);
		const TInt h = iUi->Page(page);
		if (h > 0)
			Resize(h);
		}

	// the area is now aH tall (a page declared, the navigation mode changed)
	void Resize(TInt aH)
		{
		ReleaseInput();
		iFit.iSh = aH;
		iPad.Resize(iFit.iSw, aH);
		}

	void Key(TBuf8<32>& aKey) const
		{
		aKey.Format(_L8("legacy.%08x.pad"), iUid);
		}

	void Save()
		{
		if (!iUid)
			return;
		TBuf8<32> key;
		Key(key);
		TBuf8<1 + KProfileLen> v;
		v.Append(KValueBytes);
		v.Append(1);
		v.Append(iMode);
		v.Append(iLast);
		v.Append(iConsole ? 1 : 0);		// orientation: the console
		v.Append(iA);
		v.Append(iB);
		for (TInt i = 0; i < 4; ++i)
			v.Append(TUint8(TUint(iEpoch) >> (8 * i)));
		const TInt err = iUi->SettingSet(key, v);
		if (err)
			RDebug::Printf("legacy: saving the pad of %08x failed (%d)", iUid, err);
		}

	// the app's pad: its profile; else Settings' default, the UID table, AppArc's Games group, Navigator (a first open,
	// told in a toast and saved at once so the toast shows once)
	void Open(const TOldApp& aApp)
		{
		iUid = aApp.iUid.iUid;
		iTitle = aApp.iCaption;
		iA = iB = 0;
		TBuf8<32> key;
		Key(key);
		TBuf8<1 + KProfileLen + 4> v;
		TBool known = iUi->SettingGet(key, v) >= 0 && v.Length() == 1 + KProfileLen && v[0] == KValueBytes
			&& v[1] >= 1 && TInt(Le32(v, 7)) == iEpoch;
		TPadMode mode = EPadNav;
		if (known)
			{
			mode = v[2] < EPadModes ? TPadMode(v[2]) : EPadNav;
			iLast = v[3] <= EPadNumber ? TPadMode(v[3]) : EPadNav;
			iA = v[5] <= 2 ? v[5] : 0;
			iB = v[6] <= 3 ? v[6] : 0;
			iConsole = v[4] == 1 && mode == EPadGame;
			}
		else
			{
			mode = iDefaultPad == 1 ? EPadNav : iDefaultPad == 2 ? EPadGame : iDefaultPad == 3 ? EPadNumber
				: iUid == KFrozenBubble || aApp.iGame ? EPadGame : EPadNav;
			iLast = mode;
			iConsole = mode == EPadGame && iConsoleFirst;
			}
		ReadSettings();
		Declare(iTitle);
		iMode = EPadModes;		// SetMode lays out even when the mode is the default
		SetMode(mode, EFalse);
		if (!known)
			{
			Save();
			Toast(aApp);
			}
		}

	void Toast(const TOldApp& aApp)
		{
		TBuf8<80> text;
		TInt cut = Min(aApp.iCaption.Length(), 30);
		while (cut < aApp.iCaption.Length() && cut > 0 && (aApp.iCaption[cut] & 0xC0) == 0x80)
			--cut;		// at a whole UTF-8 character
		text.Copy(aApp.iCaption.Left(cut));
		text.Append(_L8(" opened with "));
		text.Append(iMode == EPadGame ? _L8("Gamepad") : iMode == EPadNumber ? _L8("Number") : _L8("Navigator"));
		text.Append(_L8(" \xC2\xB7 tap "));		// a middle dot
		text.Append(iMode == EPadNav ? _L8("Game") : _L8("Nav"));
		text.Append(_L8(" to change"));
		TBuf8<82> note;
		note.Append(text.Length());
		note.Append(text);
		note.Append(0);		// no action
		iUi->Note(note);
		}

	void SetMode(TPadMode aMode, TBool aSave = ETrue)
		{
		if (aMode == iMode)
			return;
		ReleaseInput();
		if (iMode == EPadAbc)
			{
			iUi->Keyboard(0);
			iKeyboardH = 0;
			}
		if (aMode == EPadAbc)
			iKeyboardH = Max(0, iUi->Keyboard(KKeyboardText));
		if (aMode <= EPadNumber)
			iLast = aMode;
		iMode = aMode;
		if (aSave)
			Save();
		Layout();
		}

	void Layout()
		{
		iPad.Layout(iMode, iMode == EPadAbc ? iKeyboardH : 0, iSet, iConsole);
		iFit.Compute(iPad.Guest(), iMode != EPadHidden);
		iCleared = Max(iCleared, iFit.iSh);		// the console's pixels below a shorter portrait area too
		Mem::FillZ(iSurface, iFit.iSw * iCleared * 4);
		Frame();
		iPad.Draw(iSurface);
		iUi->Damage(0, 0, iFit.iSw, iFit.iSh);
		}

	// a tick: the guest's new frame (Hidden's button stays on top of it)
	void Frame()
		{
		if (iConsole)
			BlitTurned(iFit, iGuestPix, iSurface);
		else
			Blit(iFit, iGuestPix, iSurface);
		if (iMode == EPadHidden)
			iPad.Draw(iSurface);
		const TPadRect shown = { iFit.iDx, iFit.iDy, iFit.iDw, iFit.iDh };
		const TPadRect r = iPad.ToSurface(shown);
		iUi->Damage(r.iX, r.iY, r.iW, r.iH);
		}

	// a contact of the guest's, at surface point aX, aY
	void Guest(TInt aPhase, TInt aX, TInt aY)
		{
		TInt u, v;
		iPad.ToCanvas(aX, aY, u, v);
		iFit.ToGuest(u, v, iGuestX, iGuestY);
		GuestRaw(aPhase == 0 ? TRawEvent::EButton1Down : aPhase == 2 ? TRawEvent::EButton1Up : TRawEvent::EPointerMove);
		}

	void GuestRaw(TRawEvent::TType aType)
		{
		TRawEvent raw;
		raw.Set(aType, iGuestX, iGuestY);
		iWs->SimulateRawEvent(raw);
		iWs->Flush();
		iGuestDown = aType != TRawEvent::EButton1Up;
		if (aType != TRawEvent::EPointerMove)				// neoui's virt UI CI checks a touch reached the old app
			RDebug::Printf("legacy: raw %d %d %d", aType, iGuestX, iGuestY);
		}

	// everything held goes up: the app went to the background, closed, or the pad changed under the fingers
	void ReleaseInput()
		{
		iPad.ReleaseAll();
		if (iGuestDown)
			GuestRaw(TRawEvent::EButton1Up);
		}

	// the console on or off (the Gamepad's ⟳ and Portrait): a full-screen page, the canvas turned
	void SetConsole(TBool aOn)
		{
		ReleaseInput();
		if (iMode == EPadAbc)
			{
			iUi->Keyboard(0);
			iKeyboardH = 0;
			}
		iConsole = aOn;
		iMode = EPadGame;
		iLast = EPadGame;
		Declare(iTitle);
		Save();
		Layout();
		}

	void Touch(TUint aA, TInt aX, TInt aY)
		{
		const TInt phase = aA & 0xFF, id = aA >> 8 & 0xFF;
		const TInt act = iPad.Touch(id, phase, aX, aY);
		if (act & EPadGuest)
			Guest(phase, aX, aY);
		if (act & EPadTick && iTick)
			iUi->Haptic(12, iTick);
		if (act & EPadModeChange)
			SetMode(iPad.NewMode());
		else if (act & EPadRestore)
			SetMode(iLast);
		else if (act & EPadRedraw)
			{
			iPad.Draw(iSurface);
			const TPadRect r = iPad.ToSurface(iPad.Rect());		// the console's canvas is turned
			iUi->Damage(r.iX, r.iY, r.iW, r.iH);
			}
		if (act & EPadRotate)
			SetConsole(!iConsole);
		}

	// Abc: a typed character into the old app's focused editor, as a key event with its code (no FEP needed)
	void Type(TUint aCode)
		{
		TKeyEvent k;
		k.iCode = aCode;
		k.iScanCode = aCode == 8 ? EStdKeyBackspace : aCode == 13 ? EStdKeyEnter : ScanCode(aCode);
		k.iModifiers = 0;
		k.iRepeats = 0;
		iWs->SimulateKeyEvent(k);
		iWs->Flush();
		}

	// the app closed or went away: nothing held, no keyboard
	void Close()
		{
		ReleaseInput();
		if (iMode == EPadAbc)
			iUi->Keyboard(0);
		iKeyboardH = 0;
		iUid = 0;
		}
	};

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
	const TPadRect whole = { 0, 0, f.iSw, f.iSh };
	f.Compute(whole, EFalse);

	// The guest desktop (its screen chunk, window server and Avkon's system servers) outlives LEGACY, as a phone's
	// does: a later LEGACY reuses it. Taken down, the system-critical AknCapServer panicked (CONE 9) and faulted the
	// kernel; killed first, it could not start again (KErrAlreadyExists: its definitions outlive the process).
	RChunk guest;
	const TInt size = f.iOffset + f.iStride * f.iH;
	err = guest.CreateGlobal(KLegacyChunk, size, size);
	const TBool reuse = err == KErrAlreadyExists;
	if (reuse)
		err = guest.OpenGlobal(KLegacyChunk, EFalse);
	if (err != KErrNone)
		return Fail("guest screen chunk", err);
	if (!reuse)
		{
		Mem::FillZ(guest.Base(), size);
		const TGuestHeader header = { KGuestMagic, f.iW, f.iH, f.iStride, f.iOffset };
		Mem::Copy(guest.Base(), &header, sizeof(header));
		}
	Mem::FillZ(surface.Base(), f.iSw * f.iSh * 4);

	RProcess wserv;
	TRequestStatus ready(KErrNone);
	if (!reuse)
		{
		if ((err = wserv.Create(KGuestWserv, KNoShell)) != KErrNone)
			return Fail("start the window server", err);
		// wait for the window server's rendezvous (WSTOP.CPP, once it serves): connecting in a retry loop while it
		// starts left a half-made session in ws32/fbscli that the next try faulted in (KERN-EXEC 3)
		wserv.Rendezvous(ready);
		wserv.Resume();
		User::WaitForRequest(ready);
		}
	RWsSession ws;
	err = ready.Int() != KErrNone ? ready.Int() : ws.Connect();
	if (err)
		{
		if (wserv.Handle())
			wserv.Kill(0);
		return Fail("connect to the window server", err);
		}
	RDebug::Printf("legacy: guest wserv %dx%d %s, surface %dx%d", f.iW, f.iH, reuse ? "reused" : "up", f.iSw, f.iSh);

	TView view;
	view.iUi = &ui;
	view.iWs = &ws;
	view.iSurface = reinterpret_cast<TUint32*>(surface.Base());
	view.iGuestPix = guest.Base();
	view.iFit = f;
	view.iPad.Init(&ws, f.iSw, f.iSh, TInt(Le32(geo, 8)));
	view.iKeyboardH = 0;
	view.iMode = view.iLast = EPadNav;
	view.iUid = 0;
	view.iA = view.iB = 0;
	view.iGuestDown = EFalse;
	view.iConsole = EFalse;
	view.iCleared = 0;
	view.ReadSettings();
	ui.SettingWatch(_L8("legacy.pad."));
	if (reuse)
		view.iPad.Sweep();	// the guest desktop outlived a LEGACY: no key may stay held from it

	// Avkon's system servers: AknCapServer, which brings up EikSrv and the skin server. A 2010 phone starts them at boot
	// (the starter's AknCapServerStart, the same call); started by the first app instead, their windows took the focus
	// in its start-up, and Frozen Bubble lost the foreground before its first idle callback (KERN-EXEC 3)
	if (!reuse)
		{
		TRAPD(caps, RAknUiServer::StartAknCapserverL());
		if (caps)
			RDebug::Printf("legacy: AknCapServer not started (%d)", caps);
		}

	RApaLsSession ls;
	RArray<TOldApp> apps;
	RProcess app;
	TBool running = EFalse;		// an old app is up (its surface in front)
	if (program.Length())
		{
		if ((err = Launch(NULL, program, app)) != KErrNone)
			{
			RDebug::Printf("legacy: cannot start the program (%d)", err);
			return err;		// the guest desktop stays up for the next LEGACY
			}
		running = ETrue;
		ui.Nav(KNavBack | KNavOptions);
		ui.Front(ETrue);
		view.iMode = EPadModes;
		view.SetMode(EPadNav, EFalse);		// no app UID: nothing remembered
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
				view.Frame();
			if (running && app.ExitType() != EExitPending)
				{
				TExitCategoryName cat = app.ExitCategory();
				TBuf8<KMaxExitCategoryName + 1> cat8;
				cat8.Copy(cat);
				RDebug::Printf("legacy: program ended: type %d reason %d '%s'", app.ExitType(), app.ExitReason(),
					reinterpret_cast<const char*>(cat8.PtrZ()));
				app.Close();
				running = EFalse;
				view.Close();
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
			view.Touch(a, b, c);
		else if (kind == KKey && a && running)
			{
			if (view.iMode == EPadAbc)
				view.Type(b);		// the system keyboard's characters, any script
			else if (const TInt scan = ScanCode(b))
				view.iPad.Pulse(scan);		// through the pad's counts: a key the pad holds stays held
			}
		else if (kind == KOptions && running)
			view.iPad.Pulse(EStdKeyDevice0);		// the left softkey (Options)
		else if (kind == KKeyboardEvent && running && view.iMode == EPadAbc)
			{
			if (a == 0)
				view.SetMode(view.iLast);	// the keyboard's chevron: back to the pad
			else
				{
				view.iKeyboardH = a;
				view.Layout();
				}
			}
		else if (kind == KArea && running)
			{
			view.Resize(a);
			view.Layout();
			}
		else if (kind == KInputReset && running)
			{
			view.ReleaseInput();		// the shell took the fingers away (the screen went off, a call)
			view.Layout();
			}
		else if (kind == KSettings)
			{
			const TInt epoch = view.iEpoch;
			view.ReadSettings();
			if (view.iEpoch != epoch)
				{
				view.iA = view.iB = 0;		// Reset remembered pads: the app's own A and B go too
				view.ReadSettings();
				}
			if (running)
				{
				view.ReleaseInput();
				view.Layout();
				}
			}
		else if (kind == KVisible)
			{
			visible = a != 0;
			if (!visible)
				view.ReleaseInput();
			else if (running)
				view.Layout();
			}
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
					ui.Nav(KNavBack | KNavOptions);
					ui.Front(ETrue);
					view.Open(apps[i]);
					}
				}
			else
				ui.Front(ETrue);	// shortcut: one old app at a time; a second tile shows the running one
			}
		else if (kind == KBack && running && !program.Length())
			{
			// shortcut: killed, not asked to exit (TApaTask::EndTask needs its window group): unsaved data is lost
			view.Close();
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
		view.Close();
		if (app.ExitType() == EExitPending)
			app.Kill(0);
		app.Close();
		}
	apps.Close();
	ls.Close();
	ws.Close();
	wserv.Close();		// the guest window server keeps running (above)
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
