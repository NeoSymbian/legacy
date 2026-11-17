// SPDX-License-Identifier: Apache-2.0
#include "neoaudio.h"

namespace
    {
    TUint32 U32(const TDesC8& aBytes, TInt aOffset)
        {
        return TUint32(aBytes[aOffset]) | (TUint32(aBytes[aOffset + 1]) << 8)
            | (TUint32(aBytes[aOffset + 2]) << 16)
            | (TUint32(aBytes[aOffset + 3]) << 24);
        }
    }

TInt RNeoAudio::Connect()
    {
    // One outstanding data request, plus a slot for STOP/INFO/PAUSE/VOLUME.
    return CreateSession(NeoAudio::KName, TVersion(1, 0, 0), 2);
    }

TInt RNeoAudio::Open(TInt aRate, TInt aChannels, TInt aFlags)
    { return SendReceive(NeoAudio::EOpen, TIpcArgs(aRate, aChannels, aFlags)); }
void RNeoAudio::Write(const TDesC8& aPcm, TRequestStatus& aStatus)
    { SendReceive(NeoAudio::EWrite, TIpcArgs(&aPcm), aStatus); }
void RNeoAudio::Read(TDes8& aPcm, TRequestStatus& aStatus)
    { SendReceive(NeoAudio::ERead, TIpcArgs(&aPcm), aStatus); }
TInt RNeoAudio::Volume(TInt aVolume)
    { return SendReceive(NeoAudio::EVolume, TIpcArgs(aVolume)); }
TInt RNeoAudio::Pause(TBool aPaused)
    { return SendReceive(NeoAudio::EPause, TIpcArgs(TInt(aPaused))); }
TInt RNeoAudio::Stop()
    { return SendReceive(NeoAudio::EStop); }
TInt RNeoAudio::Route(TInt aRoute)
    { return SendReceive(NeoAudio::ERoute, TIpcArgs(aRoute)); }
TInt RNeoAudio::Master(TInt aVolume)
    { return SendReceive(NeoAudio::EMaster, TIpcArgs(aVolume)); }

TInt RNeoAudio::Info(NeoAudio::TInfo& aInfo)
    {
    TBuf8<NeoAudio::KInfoSize> bytes;
    TInt err = SendReceive(NeoAudio::EInfo, TIpcArgs(&bytes));
    if (err < KErrNone)
        return err;
    if (bytes.Length() != NeoAudio::KInfoSize)
        return KErrCorrupt;
    aInfo.iState = bytes[0];
    aInfo.iSink = bytes[1];
    aInfo.iQueued = U32(bytes, 4);
    aInfo.iFrames = TUint64(U32(bytes, 8)) | (TUint64(U32(bytes, 12)) << 32);
    aInfo.iUnderruns = U32(bytes, 16);
    aInfo.iOverruns = U32(bytes, 20);
    return KErrNone;
    }
