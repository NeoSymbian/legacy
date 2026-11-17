// SPDX-License-Identifier: Apache-2.0
// The old apps' on-screen key pads (NeoSymbian/legacy): S60 3rd-edition apps take keys, not touch, so LEGACY draws a
// pad under the guest screen and turns touches on it into held raw keys for the guest window server. The design is
// the owner's canvas (Old apps: Navigator, Gamepad, Number, Hidden, Abc; "Pad keys", "Touch & hold").

#ifndef PAD_H
#define PAD_H

#include <e32base.h>

class RWsSession;

// the modes, as stored in the per-app profile (legacy.<uid>.pad byte 1)
enum TPadMode { EPadNav = 0, EPadGame = 1, EPadNumber = 2, EPadHidden = 3, EPadAbc = 4, EPadModes };

// what a touch asks LEGACY to do (Touch's result, a mask)
enum
	{
	EPadRedraw = 1,			// the pad changed: Draw it and report its rect
	EPadModeChange = 2,		// a strip cell: switch to NewMode()
	EPadRestore = 4,		// Hidden's keyboard button: back to the last visible pad
	EPadTick = 8,			// a key went down: a short vibration
	EPadGuest = 16,			// the contact is the guest's: give it to the guest window server
	EPadRotate = 32			// the console button
	};

struct TPadRect
	{
	TInt iX, iY, iW, iH;
	TBool Has(TInt aX, TInt aY, TInt aSlop = 0) const
		{ return aX >= iX - aSlop && aY >= iY - aSlop && aX < iX + iW + aSlop && aY < iY + iH + aSlop; }
	};

// Settings → Old apps, as the pad uses them
struct TPadSettings
	{
	TBool iNumberDirs;		// the Gamepad's d-pad sends 1-9 instead of the arrows
	TInt iA, iB;			// the scan codes A and B send
	TBool iLefty;			// the d-pad on the right
	};

class TPad
	{
public:
	void Init(RWsSession* aWs, TInt aW, TInt aH, TInt aDp256);
	void Resize(TInt aW, TInt aH) { iW = aW; iH = aH; }		// lay out again after it
	// lays the pad out for aMode in a surface aW x aH whose bottom aKeyboardH pixels are the system keyboard's
	// aConsole: the Gamepad's landscape console (the phone sideways, its top to the right): the pad and the guest are
	// laid out on a landscape canvas, drawn rotated into the portrait surface
	void Layout(TPadMode aMode, TInt aKeyboardH, const TPadSettings& aSettings, TBool aConsole = EFalse);
	TBool Console() const { return iConsole; }
	// a surface point on the canvas Guest() and Rect() are in (the same point unless in the console)
	void ToCanvas(TInt aX, TInt aY, TInt& aU, TInt& aV) const
		{
		aU = iConsole ? iH - 1 - aY : aX;
		aV = iConsole ? aX : aY;
		}
	// a canvas rect as the surface rect it covers
	TPadRect ToSurface(const TPadRect& aR) const
		{
		if (!iConsole)
			return aR;
		const TPadRect r = { aR.iY, iH - aR.iX - aR.iW, aR.iH, aR.iW };
		return r;
		}
	TPadMode Mode() const { return iMode; }
	TPadMode NewMode() const { return iNewMode; }
	const TPadRect& Guest() const { return iGuest; }	// where the guest screen fits (aspect kept, at its top)
	const TPadRect& Rect() const { return iRect; }		// the pad (Hidden: its keyboard button)
	TInt Touch(TInt aId, TInt aPhase, TInt aX, TInt aY);
	void Draw(TUint32* aSurface);
	TBool ReleaseAll();		// every held key up, every contact forgotten; ETrue if the guest had a finger down
	void Pulse(TInt aScan);	// a press and release (a hardware or system-keyboard key), counted with the pad's
	void Sweep();			// a key-up for every pad key: a crashed LEGACY can have left one held in the guest

private:
	enum { KMaxItems = 32, KMaxContacts = 5, KCorners = 4 };
	struct TItem
		{
		TPadRect iR;
		TUint8 iKind, iScan, iGlyph, iSub, iIcon, iTint, iArg, iRound;
		};
	struct TContact
		{
		TInt iId;
		TInt iItem;			// -1 nobody, -2 the guest
		TBool iInside;
		TInt iZone;			// the d-pad's zone (-1 none)
		TUint8 iKeys[2];
		TInt iKeyCount;
		};
	void Add(TInt aKind, TInt aX, TInt aY, TInt aW, TInt aH, TInt aScan = 0, TInt aGlyph = -1, TInt aSub = -1,
		TInt aTint = 0, TInt aArg = 0, TBool aRound = EFalse);
	TInt Dp(TInt aDp) const { return aDp * iDp256 / 256; }
	void LayoutConsole();
	// the surface pixel under canvas point (aU, aV), which must be on the canvas
	TUint32& Px(TUint32* aS, TInt aU, TInt aV) const
		{ return iConsole ? aS[(iH - 1 - aU) * iW + aV] : aS[aV * iW + aU]; }
	TInt Hit(TInt aX, TInt aY) const;
	TInt Zone(const TItem& aDpad, TInt aX, TInt aY) const;
	TInt ZoneKeys(TInt aZone, TUint8* aKeys) const;
	void Hold(TContact& aC, const TUint8* aKeys, TInt aCount);
	void Press(TInt aScan);
	void Release(TInt aScan);
	void Raw(TInt aType, TInt aScan);
	TBool Pressed(TInt aItem, TInt aZone = -1) const;
	void Finish(TContact& aC);
	TContact* Find(TInt aId);
	// drawing
	void Fill(TUint32* aS, const TPadRect& aR, TInt aRad, TUint32 aColour) ;
	void Glyph(TUint32* aS, TInt aGlyph, TInt aCx, TInt aCy, TUint32 aColour);
	void Triangle(TUint32* aS, TInt aCx, TInt aCy, const TInt* aPts, TUint32 aColour);
	const TUint8* Corner(TInt aRad);

	RWsSession* iWs;
	TInt iW, iH, iDp256;		// the surface
	TBool iConsole;
	TInt iLw, iLh;				// the canvas (the surface, or turned for the console)
	TPadRect iPanels[2];		// the console's sides
	TPadMode iMode, iNewMode;
	TPadSettings iSet;
	TPadRect iRect, iGuest;
	TItem iItems[KMaxItems];
	TInt iItemCount;
	TContact iContacts[KMaxContacts];
	TUint8 iRef[256];		// held count per scan code: two fingers on one key send one down and one up
	TInt iCornerRad[KCorners];
	TUint8* iCorner[KCorners];	// a quarter circle's coverage per radius (rounded keys, A and B)
	};

#endif
