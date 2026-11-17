// SPDX-License-Identifier: Apache-2.0
// The old apps' on-screen key pads: layout, touch and drawing (see pad.h). Sizes are dp from the owner's canvas
// (Old apps · Navigator pad, Gamepad, Number pad, Pad hidden); keys are raw scan codes ("Pad keys"), held while the
// finger is down, with the guest window server repeating them; one-shot keys fire on a lift inside ("Touch & hold").

#include "pad.h"
#include "pad_atlas.h"
#include <e32math.h>
#include <e32keys.h>
#include <e32event.h>
#include <w32std.h>

namespace {

enum { KHeld, KOnce, KDpad, KStrip, KRestore, KRotate };	// TItem::iKind
enum { ETintText, ETintGreen, ETintRed, ETintBar };			// TItem::iTint
const TUint8 KNone = 0xFF;

// neoui's theme (ui/theme.slint) and the canvas's pad colours
const TUint32 KBg = 0xFF000000, KKey = 0xFF15171B, KBorder = 0xFF2A2E35, KPressedKey = 0xFF3A3F48,
	KAccent = 0xFF3D8BFD, KText = 0xFFFFFFFF, KText2 = 0xFFB9BEC7, KDim = 0xFF7D838C, KGreen = 0xFF30A46C,
	KRed = 0xFFE5484D, KDivider = 0xFF1C1F24;

// pad heights (dp), strip included
const TInt KPadDp[EPadModes] = { 240, 248, 298, 0, 44 };

// every scan code a pad can send (Sweep)
const TUint8 KPadScans[] =
	{
	EStdKeyDevice0, EStdKeyDevice1, EStdKeyDevice3, EStdKeyYes, EStdKeyNo, EStdKeyBackspace, EStdKeyLeftShift,
	EStdKeyLeftArrow, EStdKeyRightArrow, EStdKeyUpArrow, EStdKeyDownArrow, EStdKeyNkpAsterisk, EStdKeyHash,
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
	};

inline TUint32 Mix(TUint32 aDst, TUint32 aSrc, TInt aA)
	{
	if (aA >= 255)
		return aSrc;
	const TInt b = 255 - aA;
	const TUint32 r = (((aSrc >> 16) & 0xFF) * aA + ((aDst >> 16) & 0xFF) * b) / 255;
	const TUint32 g = (((aSrc >> 8) & 0xFF) * aA + ((aDst >> 8) & 0xFF) * b) / 255;
	const TUint32 bl = ((aSrc & 0xFF) * aA + (aDst & 0xFF) * b) / 255;
	return 0xFF000000 | r << 16 | g << 8 | bl;
	}

// arrow triangles per d-pad zone (0 NW .. 8 SE), as 3 points in eighths of the arrow's half size
const TInt KArrow[9][6] =
	{
	{ -8, -8, 6, -8, -8, 6 }, { 0, -8, 8, 5, -8, 5 }, { 8, -8, -6, -8, 8, 6 },
	{ -8, 0, 5, -8, 5, 8 }, { 0, 0, 0, 0, 0, 0 }, { 8, 0, -5, 8, -5, -8 },
	{ -8, 8, -8, -6, 6, 8 }, { 0, 8, -8, -5, 8, -5 }, { 8, 8, 8, -6, -6, 8 }
	};

} // namespace

void TPad::Init(RWsSession* aWs, TInt aW, TInt aH, TInt aDp256)
	{
	iWs = aWs;
	iW = aW;
	iH = aH;
	iDp256 = aDp256 > 0 ? aDp256 : 768;
	iMode = iNewMode = EPadNav;
	iItemCount = 0;
	for (TInt i = 0; i < KMaxContacts; ++i)
		iContacts[i].iId = -1;
	Mem::FillZ(iRef, sizeof(iRef));
	for (TInt i = 0; i < KCorners; ++i)
		{
		iCornerRad[i] = 0;
		iCorner[i] = NULL;
		}
	}

void TPad::Add(TInt aKind, TInt aX, TInt aY, TInt aW, TInt aH, TInt aScan, TInt aGlyph, TInt aSub, TInt aTint,
	TInt aArg, TBool aRound)
	{
	if (iItemCount == KMaxItems)
		return;
	TItem& it = iItems[iItemCount++];
	// aX, aY, aW, aH are dp from the pad's top left; the d-pad's arrows and the keys' arrows use iIcon
	it.iR.iX = iRect.iX + Dp(aX);
	it.iR.iY = iRect.iY + Dp(aY);
	it.iR.iW = Dp(aW);
	it.iR.iH = Dp(aH);
	it.iKind = TUint8(aKind);
	it.iScan = TUint8(aScan);
	it.iGlyph = aGlyph < 0 ? KNone : TUint8(aGlyph);
	it.iSub = aSub < 0 ? KNone : TUint8(aSub);
	it.iIcon = KNone;
	it.iTint = TUint8(aTint);
	it.iArg = TUint8(aArg);
	it.iRound = TUint8(aRound);
	}

void TPad::Layout(TPadMode aMode, TInt aKeyboardH, const TPadSettings& aSettings)
	{
	iMode = aMode;
	iSet = aSettings;
	iItemCount = 0;
	const TInt area = iH - aKeyboardH;
	const TInt wdp = iW * 256 / iDp256;		// the surface's width in dp (360 on a phone)
	const TInt h = Dp(KPadDp[aMode]);
	iRect.iX = 0;
	iRect.iY = area - h;
	iRect.iW = iW;
	iRect.iH = h;
	iGuest.iX = 0;
	iGuest.iY = 0;
	iGuest.iW = iW;
	iGuest.iH = iRect.iY;

	if (aMode == EPadHidden)
		{
		// the keyboard button over the guest, above its softkey bar (canvas: Old apps · Pad hidden)
		const TInt areaDp = area * 256 / iDp256;
		iRect.iY = 0;
		Add(KRestore, wdp - 12 - 48, areaDp - 48 - 72, 48, 48, 0, EGlyphKeyboard);
		iRect = iItems[0].iR;
		return;
		}

	// the mode strip: Nav · Game · 123 · Abc · Hide · console
	const TInt cw = (wdp - 24 - 5 * 6) / 6;
	const TInt strip[6] = { EPadNav, EPadGame, EPadNumber, EPadAbc, EPadHidden, -1 };
	const TInt label[6] = { EGlyphNav, EGlyphGame, EGlyphNum, EGlyphAbc, EGlyphHide, EGlyphRotate };
	for (TInt i = 0; i < 6; ++i)
		Add(strip[i] < 0 ? KRotate : KStrip, 12 + i * (cw + 6), 8, cw, 28, 0, label[i], -1, ETintText,
			strip[i] < 0 ? 0 : strip[i]);
	if (aMode == EPadAbc)
		return;

	// the softkeys: a short bar each, as on the canvas (their meaning changes with the app)
	TInt y = 46;
	const TInt sw = aMode == EPadGame ? 87 : 100, sh = aMode == EPadGame ? 32 : aMode == EPadNumber ? 35 : 36;
	Add(KOnce, 12, y, sw, sh, EStdKeyDevice0, -1, -1, ETintBar);
	Add(KOnce, wdp - 12 - sw, y, sw, sh, EStdKeyDevice1, -1, -1, ETintBar);
	y += sh + 6;

	if (aMode == EPadNav)
		{
		const TInt mid = wdp / 2;
		Add(KHeld, mid - 43, y, 87, 40, EStdKeyUpArrow);
		iItems[iItemCount - 1].iIcon = 1;
		y += 46;
		Add(KOnce, 12, y, 72, 47, EStdKeyYes, EGlyphCall, -1, ETintGreen);
		Add(KHeld, mid - 36 - 6 - 48, y, 48, 47, EStdKeyLeftArrow);
		iItems[iItemCount - 1].iIcon = 3;
		Add(KHeld, mid - 36, y, 72, 47, EStdKeyDevice3, EGlyphOk);
		Add(KHeld, mid + 36 + 6, y, 48, 47, EStdKeyRightArrow);
		iItems[iItemCount - 1].iIcon = 5;
		Add(KOnce, wdp - 12 - 72, y, 72, 47, EStdKeyNo, EGlyphEnd, -1, ETintRed);
		y += 53;
		Add(KOnce, 12, y, 72, 40, EStdKeyBackspace, EGlyphC);
		Add(KHeld, mid - 43, y, 87, 40, EStdKeyDownArrow);
		iItems[iItemCount - 1].iIcon = 7;
		Add(KOnce, wdp - 12 - 72, y, 72, 40, EStdKeyLeftShift, EGlyphEdit);
		}
	else if (aMode == EPadGame)
		{
		// the d-pad on the left, B above-right of A on the right; left-handed swaps the sides
		const TInt side = 149;
		Add(KDpad, iSet.iLefty ? wdp - 16 - side : 16, y, side, side);
		const TInt bx = iSet.iLefty ? 48 : wdp - 48, ax = iSet.iLefty ? 118 : wdp - 118;
		Add(KHeld, bx - 31, 115 - 31, 62, 62, iSet.iB, EGlyphB, -1, ETintText, 0, ETrue);
		Add(KHeld, ax - 37, 187 - 37, 74, 74, iSet.iA, EGlyphA, -1, ETintText, 0, ETrue);
		}
	else	// EPadNumber
		{
		const TInt kw = (wdp - 24 - 14) / 3;
		const TUint8 scans[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', EStdKeyNkpAsterisk, '0', EStdKeyHash };
		const TInt glyphs[12] = { EGlyphD1, EGlyphD2, EGlyphD3, EGlyphD4, EGlyphD5, EGlyphD6, EGlyphD7, EGlyphD8,
			EGlyphD9, EGlyphStar, EGlyphD0, EGlyphHash };
		const TInt subs[12] = { -1, EGlyphL2, EGlyphL3, EGlyphL4, EGlyphL5, EGlyphL6, EGlyphL7, EGlyphL8, EGlyphL9,
			-1, EGlyphL0, -1 };
		for (TInt i = 0; i < 12; ++i)
			Add(KHeld, 12 + (i % 3) * (kw + 7), y + (i / 3) * 51, kw, 45, scans[i], glyphs[i], subs[i]);
		}
	}

// --- touch ---

TInt TPad::Hit(TInt aX, TInt aY) const
	{
	for (TInt i = iItemCount; --i >= 0;)
		if (iItems[i].iR.Has(aX, aY))
			return i;
	return -1;
	}

// the d-pad's zone under a finger: 0 NW .. 8 SE (4 the neutral centre), -1 beyond the square's 16 dp margin
TInt TPad::Zone(const TItem& aDpad, TInt aX, TInt aY) const
	{
	const TPadRect& r = aDpad.iR;
	if (!r.Has(aX, aY, Dp(16)))
		return -1;
	const TInt cx = Max(0, Min(2, (aX - r.iX) * 3 / r.iW)), cy = Max(0, Min(2, (aY - r.iY) * 3 / r.iH));
	return cy * 3 + cx;
	}

TInt TPad::ZoneKeys(TInt aZone, TUint8* aKeys) const
	{
	if (aZone < 0 || aZone == 4)
		return 0;
	if (iSet.iNumberDirs)
		{
		aKeys[0] = TUint8('1' + aZone);		// the zones are laid out as the phone keys 1-9
		return 1;
		}
	TInt n = 0;
	if (aZone / 3 != 1)
		aKeys[n++] = aZone / 3 == 0 ? EStdKeyUpArrow : EStdKeyDownArrow;
	if (aZone % 3 != 1)
		aKeys[n++] = aZone % 3 == 0 ? EStdKeyLeftArrow : EStdKeyRightArrow;
	return n;
	}

void TPad::Raw(TInt aType, TInt aScan)
	{
	TRawEvent ev;
	ev.Set(TRawEvent::TType(aType), aScan);
	iWs->SimulateRawEvent(ev);
	iWs->Flush();
	}

void TPad::Press(TInt aScan)
	{
	if (iRef[aScan]++ == 0)
		Raw(TRawEvent::EKeyDown, aScan);
	}

void TPad::Release(TInt aScan)
	{
	if (iRef[aScan] && --iRef[aScan] == 0)
		Raw(TRawEvent::EKeyUp, aScan);
	}

// the contact now holds aKeys: what it leaves goes up first, then what is new goes down
void TPad::Hold(TContact& aC, const TUint8* aKeys, TInt aCount)
	{
	for (TInt i = 0; i < aC.iKeyCount; ++i)
		{
		TBool kept = EFalse;
		for (TInt j = 0; j < aCount; ++j)
			kept |= aKeys[j] == aC.iKeys[i];
		if (!kept)
			Release(aC.iKeys[i]);
		}
	for (TInt j = 0; j < aCount; ++j)
		{
		TBool had = EFalse;
		for (TInt i = 0; i < aC.iKeyCount; ++i)
			had |= aKeys[j] == aC.iKeys[i];
		if (!had)
			Press(aKeys[j]);
		}
	for (TInt j = 0; j < aCount; ++j)
		aC.iKeys[j] = aKeys[j];
	aC.iKeyCount = aCount;
	}

TPad::TContact* TPad::Find(TInt aId)
	{
	for (TInt i = 0; i < KMaxContacts; ++i)
		if (iContacts[i].iId == aId)
			return &iContacts[i];
	return NULL;
	}

void TPad::Finish(TContact& aC)
	{
	Hold(aC, NULL, 0);
	aC.iId = -1;
	}

TInt TPad::Touch(TInt aId, TInt aPhase, TInt aX, TInt aY)
	{
	TContact* c = Find(aId);
	if (aPhase == 0)	// down: the contact's owner, for its whole life
		{
		if (c)
			Finish(*c);		// a lost lift
		c = Find(-1);
		if (!c)
			return 0;
		c->iId = aId;
		c->iZone = -1;
		c->iKeyCount = 0;
		c->iInside = ETrue;
		c->iItem = Hit(aX, aY);
		if (c->iItem < 0)
			{
			// only the first finger drives the guest's pointer; the rest of the screen swallows a contact
			c->iItem = aId == 0 && iGuest.Has(aX, aY) ? -2 : -1;
			return c->iItem == -2 ? EPadGuest : 0;
			}
		const TItem& it = iItems[c->iItem];
		if (it.iKind == KHeld)
			{
			Hold(*c, &it.iScan, 1);
			return EPadRedraw | EPadTick;
			}
		if (it.iKind == KDpad)
			{
			TUint8 keys[2];
			c->iZone = Zone(it, aX, aY);
			const TInt n = ZoneKeys(c->iZone, keys);
			Hold(*c, keys, n);
			return EPadRedraw | (n ? EPadTick : 0);
			}
		return EPadRedraw;
		}
	if (!c)
		return 0;
	if (c->iItem < 0)
		{
		const TInt guest = c->iItem == -2 ? EPadGuest : 0;
		if (aPhase == 2)
			c->iId = -1;
		return guest;
		}
	const TItem& it = iItems[c->iItem];
	if (aPhase == 1)	// move
		{
		if (it.iKind == KDpad)
			{
			const TInt zone = Zone(it, aX, aY);
			if (zone == c->iZone)
				return 0;
			TUint8 keys[2];
			const TInt n = ZoneKeys(zone, keys);
			const TInt before = c->iKeyCount;
			Hold(*c, keys, n);
			c->iZone = zone;
			return EPadRedraw | (n > before || (n && n == before) ? EPadTick : 0);
			}
		const TBool inside = it.iR.Has(aX, aY, Dp(8));
		if (it.iKind == KHeld)
			{
			if (c->iKeyCount && !inside)	// slid off: the key goes up now
				{
				Hold(*c, NULL, 0);
				return EPadRedraw;
				}
			return 0;
			}
		if (inside == c->iInside)
			return 0;
		c->iInside = inside;
		return EPadRedraw;
		}
	// up: held keys go up wherever the finger is; one-shot keys and the pad's own buttons act on a lift inside
	TInt act = EPadRedraw;
	if (c->iInside)
		{
		switch (it.iKind)
			{
		case KOnce:
			Press(it.iScan);
			Release(it.iScan);
			act |= EPadTick;
			break;
		case KStrip:
			iNewMode = TPadMode(it.iArg);
			act |= EPadModeChange;
			break;
		case KRestore:
			act |= EPadRestore;
			break;
		case KRotate:
			act |= EPadRotate;
			break;
		default:
			break;
			}
		}
	Finish(*c);
	return act;
	}

TBool TPad::ReleaseAll()
	{
	TBool guest = EFalse;
	for (TInt i = 0; i < KMaxContacts; ++i)
		if (iContacts[i].iId >= 0)
			{
			guest |= iContacts[i].iItem == -2;
			Finish(iContacts[i]);
			}
	for (TInt s = 0; s < 256; ++s)
		if (iRef[s])
			{
			iRef[s] = 0;
			Raw(TRawEvent::EKeyUp, s);
			}
	return guest;
	}

void TPad::Sweep()
	{
	for (TUint i = 0; i < sizeof(KPadScans); ++i)
		Raw(TRawEvent::EKeyUp, KPadScans[i]);
	}

// --- drawing ---

TBool TPad::Pressed(TInt aItem, TInt aZone) const
	{
	for (TInt i = 0; i < KMaxContacts; ++i)
		{
		const TContact& c = iContacts[i];
		if (c.iId < 0 || c.iItem != aItem)
			continue;
		const TInt kind = iItems[aItem].iKind;
		if (kind == KDpad ? c.iZone == aZone : kind == KHeld ? c.iKeyCount > 0 : c.iInside)
			return ETrue;
		}
	return EFalse;
	}

// a quarter circle's coverage (0-255) for radius aRad, per pixel from the corner: rounded keys, A and B
const TUint8* TPad::Corner(TInt aRad)
	{
	for (TInt i = 0; i < KCorners; ++i)
		if (iCornerRad[i] == aRad && iCorner[i])
			return iCorner[i];
	TInt slot = 0;
	while (slot < KCorners - 1 && iCorner[slot])
		++slot;
	User::Free(iCorner[slot]);
	iCorner[slot] = static_cast<TUint8*>(User::Alloc(aRad * aRad));
	iCornerRad[slot] = aRad;
	if (!iCorner[slot])
		return NULL;	// square corners
	for (TInt j = 0; j < aRad; ++j)
		for (TInt i = 0; i < aRad; ++i)
			{
			const TReal dx = aRad - i - 0.5, dy = aRad - j - 0.5;
			TReal d;
			Math::Sqrt(d, dx * dx + dy * dy);
			const TReal cov = aRad - d + 0.5;
			iCorner[slot][j * aRad + i] = TUint8(cov <= 0 ? 0 : cov >= 1 ? 255 : TInt(cov * 255));
			}
	return iCorner[slot];
	}

void TPad::Fill(TUint32* aS, const TPadRect& aR, TInt aRad, TUint32 aColour)
	{
	aRad = Min(aRad, Min(aR.iW, aR.iH) / 2);
	const TUint8* corner = aRad > 0 ? Corner(aRad) : NULL;
	const TInt y0 = Max(aR.iY, 0), y1 = Min(aR.iY + aR.iH, iH);
	const TInt x0 = Max(aR.iX, 0), x1 = Min(aR.iX + aR.iW, iW);
	for (TInt y = y0; y < y1; ++y)
		{
		const TInt top = y - aR.iY, bottom = aR.iY + aR.iH - 1 - y;
		const TInt j = top < aRad ? top : bottom < aRad ? bottom : -1;
		TUint32* row = aS + y * iW;
		for (TInt x = x0; x < x1; ++x)
			{
			TInt a = 255;
			if (j >= 0 && corner)
				{
				const TInt left = x - aR.iX, right = aR.iX + aR.iW - 1 - x;
				const TInt i = left < aRad ? left : right < aRad ? right : -1;
				if (i >= 0)
					a = corner[j * aRad + i];
				}
			if (a)
				row[x] = Mix(row[x], aColour, a);
			}
		}
	}

// a mask from the atlas (3 px per dp), scaled to the screen's dp by averaging, centred on aCx, aCy
void TPad::Glyph(TUint32* aS, TInt aGlyph, TInt aCx, TInt aCy, TUint32 aColour)
	{
	const TPadGlyphInfo& g = KPadGlyphs[aGlyph];
	const TInt scale256 = KPadAtlasScale * 256;
	const TInt dw = Max(1, (g.iW * iDp256 + scale256 / 2) / scale256), dh = Max(1, (g.iH * iDp256 + scale256 / 2) / scale256);
	const TInt x0 = aCx - dw / 2, y0 = aCy - dh / 2;
	const TUint8* src = KPadAlpha + g.iOffset;
	for (TInt dy = 0; dy < dh; ++dy)
		{
		const TInt y = y0 + dy;
		if (y < 0 || y >= iH)
			continue;
		const TInt sy0 = dy * g.iH / dh, sy1 = Max(sy0 + 1, (dy + 1) * g.iH / dh);
		for (TInt dx = 0; dx < dw; ++dx)
			{
			const TInt x = x0 + dx;
			if (x < 0 || x >= iW)
				continue;
			const TInt sx0 = dx * g.iW / dw, sx1 = Max(sx0 + 1, (dx + 1) * g.iW / dw);
			TInt sum = 0;
			for (TInt sy = sy0; sy < sy1; ++sy)
				for (TInt sx = sx0; sx < sx1; ++sx)
					sum += src[sy * g.iW + sx];
			const TInt a = sum / ((sy1 - sy0) * (sx1 - sx0));
			if (a)
				aS[y * iW + x] = Mix(aS[y * iW + x], aColour, a);
			}
		}
	}

// an arrow: KArrow's points (eighths of 6 dp) around aCx, aCy, 4x4 supersampled
void TPad::Triangle(TUint32* aS, TInt aCx, TInt aCy, const TInt* aPts, TUint32 aColour)
	{
	const TInt half = Dp(6);
	TInt p[6];		// quarter pixels
	for (TInt i = 0; i < 6; i += 2)
		{
		p[i] = (aCx * 8 + aPts[i] * half) / 2;
		p[i + 1] = (aCy * 8 + aPts[i + 1] * half) / 2;
		}
	const TInt area = (p[2] - p[0]) * (p[5] - p[1]) - (p[3] - p[1]) * (p[4] - p[0]);
	if (!area)
		return;
	const TInt sign = area > 0 ? 1 : -1;
	for (TInt y = aCy - half - 1; y <= aCy + half + 1; ++y)
		for (TInt x = aCx - half - 1; x <= aCx + half + 1; ++x)
			{
			if (x < 0 || y < 0 || x >= iW || y >= iH)
				continue;
			TInt hits = 0;
			for (TInt sy = 0; sy < 4; ++sy)
				for (TInt sx = 0; sx < 4; ++sx)
					{
					const TInt qx = x * 4 + sx, qy = y * 4 + sy;
					TBool in = ETrue;
					for (TInt e = 0; e < 3 && in; ++e)
						{
						const TInt ax = p[e * 2], ay = p[e * 2 + 1];
						const TInt bx = p[(e * 2 + 2) % 6], by = p[(e * 2 + 3) % 6];
						in = sign * ((bx - ax) * (qy - ay) - (by - ay) * (qx - ax)) >= 0;
						}
					hits += in;
					}
			if (hits)
				aS[y * iW + x] = Mix(aS[y * iW + x], aColour, hits * 255 / 16);
			}
	}

void TPad::Draw(TUint32* aS)
	{
	const TInt border = Max(1, Dp(1) / 2);
	if (iMode != EPadHidden)
		{
		Fill(aS, iRect, 0, KBg);
		TPadRect line = { iRect.iX, iRect.iY, iRect.iW, Max(1, Dp(1) / 2) };
		Fill(aS, line, 0, KDivider);
		}
	for (TInt i = 0; i < iItemCount; ++i)
		{
		const TItem& it = iItems[i];
		const TInt rad = it.iRound ? it.iR.iW / 2 : Dp(it.iKind == KRestore ? 12 : 8);
		if (it.iKind == KDpad)
			{
			const TInt gap = Dp(4), cell = (it.iR.iW - 2 * gap) / 3;
			for (TInt z = 0; z < 9; ++z)
				{
				const TPadRect c = { it.iR.iX + (z % 3) * (cell + gap), it.iR.iY + (z / 3) * (cell + gap), cell, cell };
				const TPadRect in = { c.iX + border, c.iY + border, c.iW - 2 * border, c.iH - 2 * border };
				Fill(aS, c, Dp(8), KBorder);
				Fill(aS, in, Dp(8) - border, z == 4 ? KBg : Pressed(i, z) ? KPressedKey : KKey);
				if (z != 4)
					Triangle(aS, c.iX + cell / 2, c.iY + cell / 2, KArrow[z], KText);
				}
			continue;
			}
		const TBool selected = it.iKind == KStrip && it.iArg == iMode;
		const TBool pressed = Pressed(i);
		const TPadRect in = { it.iR.iX + border, it.iR.iY + border, it.iR.iW - 2 * border, it.iR.iH - 2 * border };
		if (selected)
			Fill(aS, it.iR, rad, pressed ? KPressedKey : KAccent);
		else
			{
			Fill(aS, it.iR, rad, KBorder);
			Fill(aS, in, rad - border, pressed ? KPressedKey : KKey);
			}
		const TInt cx = it.iR.iX + it.iR.iW / 2, cy = it.iR.iY + it.iR.iH / 2;
		const TUint32 colour = it.iTint == ETintGreen ? KGreen : it.iTint == ETintRed ? KRed
			: it.iKind == KStrip && !selected ? KText2 : KText;
		if (it.iTint == ETintBar)
			{
			const TPadRect bar = { cx - Dp(8), cy - Dp(3) / 2, Dp(16), Max(2, Dp(3)) };
			Fill(aS, bar, bar.iH / 2, KText2);
			}
		else if (it.iIcon != KNone)
			Triangle(aS, cx, cy, KArrow[it.iIcon], KText);
		else if (it.iGlyph != KNone && it.iSub != KNone)
			{
			Glyph(aS, it.iGlyph, cx, cy - Dp(6), colour);
			Glyph(aS, it.iSub, cx, cy + Dp(12), KDim);
			}
		else if (it.iGlyph != KNone)
			Glyph(aS, it.iGlyph, cx, cy, it.iKind == KRotate ? KText2 : colour);
		}
	}
