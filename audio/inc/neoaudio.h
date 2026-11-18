// SPDX-License-Identifier: Apache-2.0
#ifndef LEGACY_NEOAUDIO_H
#define LEGACY_NEOAUDIO_H

#include <e32base.h>

// Source of truth: telephony/crates/neoaudio/src/lib.rs (protocol v1.0.0).
namespace NeoAudio
    {
    _LIT(KName, "NeoAudio");
    enum TOperation { EOpen = 1, EWrite, ERead, EVolume, EPause, EStop,
                      EInfo, ERoute, EMaster };
    enum TFlag { ERecord = 1 };
    enum TState { EClosed = 0, ERunning, EPaused, EPreempted };
    enum TSink { EClock = 0, EDsp = 1 };
    enum TRoute { ESpeaker = 0, EEarpiece, EBottom };
    enum TError { ECancel = -3, EArgument = -6, EInUse = -14, ENotReady = -18 };
    const TInt KRate = 48000;
    const TInt KQueueMilliseconds = 250;
    const TInt KInfoSize = 24;

    // Decode the wire bytes explicitly: no dependency on compiler struct packing.
    struct TInfo
        {
        TUint8 iState;
        TUint8 iSink;
        TUint32 iQueued;
        TUint64 iFrames;
        TUint32 iUnderruns;
        TUint32 iOverruns;
        };
    }

class RNeoAudio : public RSessionBase
    {
public:
    TInt Connect();
    TInt Open(TInt aRate, TInt aChannels, TInt aFlags);
    void Write(const TDesC8& aPcm, TRequestStatus& aStatus);
    void Read(TDes8& aPcm, TRequestStatus& aStatus);
    TInt Volume(TInt aVolume);
    TInt Pause(TBool aPaused);
    // STOP is also the cancellation barrier for a pending WRITE or READ.
    TInt Stop();
    TInt Info(NeoAudio::TInfo& aInfo);
    TInt Route(TInt aRoute);
    TInt Master(TInt aVolume);
    };

#endif
