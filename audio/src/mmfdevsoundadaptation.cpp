// SPDX-License-Identifier: Apache-2.0
// A3F's public adaptor ABI, with NeoAudio replacing ACF/MMRC and the hardware codec.
#include "mmfdevsoundadaptation.h"
#include "neoaudio.h"
#include <mmf/server/mmfdatabuffer.h>

namespace
    {
    const TInt KBufferBytes = 4096;
    const TInt KMaxLevel = 10000;
    const TInt KRates = EMMFSampleRate8000Hz | EMMFSampleRate11025Hz
        | EMMFSampleRate12000Hz | EMMFSampleRate16000Hz | EMMFSampleRate22050Hz
        | EMMFSampleRate24000Hz | EMMFSampleRate32000Hz | EMMFSampleRate44100Hz
        | EMMFSampleRate48000Hz;

    TInt Rate(TInt aRate)
        {
        switch (aRate)
            {
            case EMMFSampleRate8000Hz: return 8000;
            case EMMFSampleRate11025Hz: return 11025;
            case EMMFSampleRate12000Hz: return 12000;
            case EMMFSampleRate16000Hz: return 16000;
            case EMMFSampleRate22050Hz: return 22050;
            case EMMFSampleRate24000Hz: return 24000;
            case EMMFSampleRate32000Hz: return 32000;
            case EMMFSampleRate44100Hz: return 44100;
            case EMMFSampleRate48000Hz: return 48000;
            default: return 0;
            }
        }

    TInt Clamp(TInt aValue, TInt aMax)
        { return aValue < 0 ? 0 : (aValue > aMax ? aMax : aValue); }

    TInt64 ToneFrames(TInt64 aMicroseconds)
        {
        // Split before multiplying, including for very long TTimeInterval values.
        return (aMicroseconds / 1000000) * NeoAudio::KRate
            + (aMicroseconds % 1000000) * NeoAudio::KRate / 1000000;
        }

    // Q15 sine approximation, as used by NeoAudio's test tone. No FP run time.
    TInt Sine(TUint32 aPhase)
        {
        TInt64 x = TInt32(aPhase) >> 16;
        TInt64 ax = x < 0 ? -x : x;
        TInt64 y = (4 * x * 32768 - 4 * x * ax) >> 15;
        TInt64 ay = y < 0 ? -y : y;
        return TInt(y + (((((y * ay) >> 15) - y) * 7373) >> 15));
        }

    struct TToneSegment
        {
        TInt iFirst;
        TInt iSecond;
        TUint32 iStep1;
        TUint32 iStep2;
        TInt64 iFrames;
        };

    TBool Dtmf(TUint aChar, TInt& aFirst, TInt& aSecond)
        {
        static const TInt rows[] = {697, 770, 852, 941};
        static const TInt columns[] = {1209, 1336, 1477, 1633};
        static const TUint8 keys[] = "123A456B789C*0#D";
        if (aChar >= 'a' && aChar <= 'd') aChar -= 'a' - 'A';
        for (TInt i = 0; i != 16; ++i)
            if (keys[i] == aChar)
                {
                aFirst = rows[i / 4];
                aSecond = columns[i % 4];
                return ETrue;
                }
        return EFalse;
        }
    }

class CMMFDevSoundAdaptation::CBody : public CActive
    {
public:
    enum TEvent { ENone, EComplete, EInitialize, EStartPlay, EStartRecord,
                  EStartTone, EPause, EResume, EFinish, EBuffer, EError };

    static CBody* NewL(MDevSoundAdaptationObserver& aObserver)
        {
        CBody* self = new(ELeave) CBody(aObserver);
        CleanupStack::PushL(self);
        self->iBuffer = CMMFDataBuffer::NewL(KBufferBytes);
        self->iBuffer->SetRequestSizeL(KBufferBytes);
        self->iCallback = new(ELeave) CAsyncCallBack(TCallBack(Dispatch, self), EPriorityStandard);
        self->iDrain = CPeriodic::NewL(EPriorityStandard);
        CleanupStack::Pop(self);
        return self;
        }

    virtual ~CBody()
        {
        StopStream();
        delete iDrain;
        delete iCallback;
        delete iBuffer;
        iTones.Close();
        }

    CBody(MDevSoundAdaptationObserver& aObserver)
        : CActive(EPriorityStandard), iObserver(aObserver)
        {
        CActiveScheduler::Add(this);
        iConfig.iRate = EMMFSampleRate8000Hz;
        iConfig.iEncoding = EMMFSoundEncoding16BitPCM;
        iConfig.iChannels = EMMFMono;
        iConfig.iBufferSize = KBufferBytes;
        iVolume = iGain = KMaxLevel;
        iPlayLeft = iPlayRight = iRecordLeft = iRecordRight = 50;
        iOn = 250000; iOff = 50000; iPauseLength = 250000;
        }

    void ScheduleL(TEvent aEvent, TInt aError = KErrNone)
        {
        if (iEvent != ENone) User::Leave(KErrInUse);
        iEvent = aEvent;
        iEventError = aError;
        iCallback->CallBack();
        }

    static TInt Dispatch(TAny* aSelf)
        { static_cast<CBody*>(aSelf)->DispatchEvent(); return KErrNone; }

    void DispatchEvent()
        {
        const TEvent event = iEvent;
        const TInt err = iEventError;
        const TUint serial = iSerial;
        const TMMFState mode = iMode;
        const TInt finishError = iFinishError;
        iEvent = ENone;
        if (event == EBuffer)
            { RequestBuffer(); return; }
        if (event == EError)
            { Finish(err); return; }

        // The session sets its pending flag after returning from the exported call.
        // Completion can synchronously dequeue Stop/Initialize: never use stale state.
        if (event == EInitialize) iInitialized = ETrue;
        iObserver.AsynchronousOperationComplete(KErrNone, ETrue);
        if (serial != iSerial) return;
        switch (event)
            {
            case EInitialize:
                iObserver.InitializeComplete(err);
                break;
            case EStartPlay:
                if (err) Finish(err); else RequestBuffer();
                break;
            case EStartRecord:
                if (err) Finish(err); else ReadBuffer();
                break;
            case EStartTone:
                if (err) Finish(err); else WriteTone();
                break;
            case EPause:
                if (mode == EMMFStateRecording)
                    {
                    // STOP completed the READ before Cancel returned. Preserve partial data.
                    if (iRecordPauseBuffer)
                        {
                        iRecordPauseBuffer = EFalse;
                        DeliverRecord(ETrue);
                        }
                    iObserver.CallbackFromAdaptorReceived(KCallbackRecordPauseComplete, KErrNone);
                    }
                break;
            case EResume:
                if (iNeedBuffer) RequestBuffer();
                else if (mode == EMMFStateRecording && !IsActive() && !iClientBuffer)
                    ReadBuffer();
                else if (mode == EMMFStateTonePlaying && !IsActive() && !iDraining)
                    WriteTone();
                break;
            case EFinish:
                // This is the second half of the ProcessingFinished/Error handshake.
                if (mode == EMMFStateTonePlaying) iObserver.ToneFinished(finishError);
                else if (mode == EMMFStateRecording) iObserver.RecordError(finishError);
                else iObserver.PlayError(finishError);
                break;
            default: break;
            }
        }

    void InitializeL(TMMFState aMode)
        {
        if (aMode != EMMFStatePlaying && aMode != EMMFStateRecording
            && aMode != EMMFStateTonePlaying) User::Leave(KErrNotSupported);
        StopStream();
        iMode = aMode;
        iInitialized = EFalse;
        // Absence of MODEMD must not prevent OpenComplete in legacy applications.
        // Connect is retried on each actual start, and failure becomes a mode error.
        ScheduleL(EInitialize);
        }

    TInt OpenStream(TBool aTone)
        {
        TInt err = iAudio.Connect();
        if (err) return KErrNotReady;
        iStreamRate = aTone ? NeoAudio::KRate : Rate(iConfig.iRate);
        iStreamChannels = aTone ? 1 : (iConfig.iChannels == EMMFStereo ? 2 : 1);
        err = iAudio.Open(iStreamRate, iStreamChannels,
            iMode == EMMFStateRecording ? NeoAudio::ERecord : 0);
        if (!err) err = iAudio.Volume((iMode == EMMFStateRecording ? iGain : iVolume) / 100);
        if (err) iAudio.Close();
        return err;
        }

    void StartL(TEvent aEvent)
        {
        if (!iInitialized) User::Leave(KErrNotReady);
        StopStream();
        iFrames = 0;
        iBuffer->SetRequestSizeL(KBufferBytes);
        TInt err = OpenStream(aEvent == EStartTone);
        iRunning = !err;
        ScheduleL(aEvent, err);
        }

    void Snapshot()
        {
        if (!iAudio.Handle()) return;
        NeoAudio::TInfo info;
        if (iAudio.Info(info) == KErrNone) iFrames = info.iFrames;
        }

    void StopStream()
        {
        ++iSerial;
        if (iCallback) iCallback->Cancel();
        iEvent = ENone;
        if (iDrain) iDrain->Cancel();
        Snapshot();
        if (IsActive()) Cancel();
        else if (iAudio.Handle()) iAudio.Stop();
        iAudio.Close();
        iRunning = iPaused = iDraining = iNeedBuffer = iClientBuffer = EFalse;
        iFinishing = iRecordPauseBuffer = EFalse;
        }

    void RequestBuffer()
        {
        if (!iRunning || iFinishing) return;
        if (iPaused) { iNeedBuffer = ETrue; return; }
        iNeedBuffer = EFalse;
        iBuffer->Data().Zero();
        iBuffer->SetLastBuffer(EFalse);
        iClientBuffer = ETrue;
        iObserver.BufferToBeFilled(iBuffer);
        }

    void PlayData()
        {
        if (!iRunning || iFinishing || IsActive() || !iClientBuffer) return;
        iClientBuffer = EFalse;
        TInt size = iBuffer->Data().Length();
        if (size % (iStreamChannels * 2))
            { DeferError(KErrArgument); return; }
        iLastWrite = iBuffer->LastBuffer();
        if (!size)
            {
            if (iLastWrite) Drain();
            else
                {
                TRAPD(err, ScheduleL(EBuffer));
                if (err) DeferError(err);
                }
            return;
            }
        WriteBuffer();
        }

    void WriteBuffer()
        {
        iWriteBytes = iBuffer->Data().Length();
        iAudio.Write(iBuffer->Data(), iStatus);
        SetActive();
        }

    void ReadBuffer()
        {
        if (!iRunning || iPaused || iFinishing || IsActive() || iClientBuffer) return;
        iBuffer->Data().Zero();
        iBuffer->SetLastBuffer(EFalse);
        // RequestSize may have been shortened by a paused READ.
        TRAPD(err, iBuffer->SetRequestSizeL(KBufferBytes));
        if (err) { DeferError(err); return; }
        iAudio.Read(iBuffer->Data(), iStatus);
        SetActive();
        }

    void RecordData()
        {
        if (iMode != EMMFStateRecording || !iClientBuffer) return;
        iClientBuffer = EFalse;
        ReadBuffer();
        }

    void DeliverRecord(TBool aLast)
        {
        // The session copies RequestSize rather than Data.Length.
        TInt length = iBuffer->Data().Length();
        TRAPD(err, iBuffer->SetRequestSizeL(length));
        if (err) { Finish(err); return; }
        iBuffer->SetLastBuffer(aLast);
        iClientBuffer = ETrue;
        iObserver.BufferToBeEmptied(iBuffer);
        }

    virtual void RunL()
        {
        const TInt result = iStatus.Int();
        if (!iRunning || iFinishing) return;
        if (result < KErrNone) { Finish(result); return; }
        if (iMode == EMMFStateRecording)
            {
            if (result != iBuffer->Data().Length() || result % (iStreamChannels * 2))
                { Finish(KErrCorrupt); return; }
            DeliverRecord(EFalse);
            }
        else if (result != iWriteBytes) Finish(KErrCorrupt);
        else if (iLastWrite) Drain();
        else if (iMode == EMMFStateTonePlaying) WriteTone();
        else RequestBuffer();
        }

    virtual void DoCancel()
        {
        // Synchronous STOP completes pending data IPC before its descriptor is reused.
        // Closing is a final cancellation barrier if the native server died.
        if (iAudio.Handle())
            {
            TInt err = iAudio.Stop();
            if (err) iAudio.Close();
            }
        }

    virtual TInt RunError(TInt aError)
        { Finish(aError); return KErrNone; }

    void Drain()
        {
        iDraining = ETrue;
        iDrain->Start(TTimeIntervalMicroSeconds32(50000),
            TTimeIntervalMicroSeconds32(50000), TCallBack(Poll, this));
        }

    static TInt Poll(TAny* aSelf)
        {
        CBody* self = static_cast<CBody*>(aSelf);
        if (self->iPaused) return 1;
        NeoAudio::TInfo info;
        TInt err = self->iAudio.Info(info);
        if (!err) self->iFrames = info.iFrames;
        // PREEMPTED deliberately keeps running on NeoAudio's clock sink.
        if (err || info.iQueued == 0)
            {
            self->iDrain->Cancel();
            self->Finish(err ? err : KErrUnderflow);
            return 0;
            }
        return 1;
        }

    void DeferError(TInt aError)
        {
        // Buffer exchange has no async-operation IPC to complete.
        // Keep errors out of the middle of session PlayData/RecordData handling.
        if (iEvent == ENone)
            {
            iEvent = EError;
            iEventError = aError;
            iCallback->CallBack();
            }
        }

    void Finish(TInt aError)
        {
        if (iFinishing) return;
        iFinishing = ETrue;
        iFinishError = aError;
        iRunning = EFalse;
        iDrain->Cancel();
        iDraining = EFalse;
        Snapshot();
        if (IsActive()) Cancel();
        else if (iAudio.Handle()) iAudio.Stop();
        // Keep session/INFO until Stop or next start so sample counters stay visible.
        iObserver.CallbackFromAdaptorReceived(aError == KErrUnderflow
            ? KCallbackProcessingFinished : KCallbackProcessingUnitError, KErrNone);
        }

    TInt Processing(TBool& aAsync)
        {
        aAsync = EFalse;
        if (!iFinishing) return KErrNotReady;
        TRAPD(err, ScheduleL(EFinish));
        if (!err) aAsync = ETrue;
        return err;
        }

    TInt Pause()
        {
        if (!iRunning || iPaused || iFinishing) return KErrNotReady;
        TInt err = iAudio.Pause(ETrue);
        if (err) return err;
        iPaused = ETrue;
        if (iMode == EMMFStateRecording && IsActive())
            {
            Cancel(); // completes any partial recording with STOP; session remains open
            iRecordPauseBuffer = ETrue;
            }
        TRAP(err, ScheduleL(EPause));
        return err;
        }

    TInt Resume()
        {
        if (!iRunning || !iPaused || iFinishing) return KErrNotReady;
        TInt err = iAudio.Pause(EFalse);
        if (err) return err;
        iPaused = EFalse;
        TRAP(err, ScheduleL(EResume));
        return err;
        }

    TInt Samples()
        {
        Snapshot();
        return iFrames > TUint64(KMaxTInt) ? KMaxTInt : TInt(iFrames);
        }

    void AddSegmentL(TInt aFirst, TInt aSecond, TInt64 aMicroseconds)
        {
        if (aMicroseconds < 0) User::Leave(KErrArgument);
        TToneSegment segment;
        segment.iFirst = aFirst; segment.iSecond = aSecond;
        segment.iStep1 = TUint32((TUint64(aFirst) << 32) / NeoAudio::KRate);
        segment.iStep2 = TUint32((TUint64(aSecond) << 32) / NeoAudio::KRate);
        segment.iFrames = ToneFrames(aMicroseconds);
        if (segment.iFrames) User::LeaveIfError(iTones.Append(segment));
        }

    void ToneL(TInt aFirst, TInt aSecond, const TTimeIntervalMicroSeconds& aDuration)
        {
        if (iMode != EMMFStateTonePlaying || !iInitialized) User::Leave(KErrNotReady);
        if (aFirst < 0 || aFirst > 24000 || aSecond < 0 || aSecond > 24000
            || aDuration.Int64() < 0) User::Leave(KErrArgument);
        if (iRunning || iEvent != ENone) User::Leave(KErrInUse);
        iTones.Reset();
        AddSegmentL(aFirst, aSecond, aDuration.Int64());
        StartToneL();
        }

    void DtmfL(const TDesC& aString)
        {
        if (iMode != EMMFStateTonePlaying || !iInitialized) User::Leave(KErrNotReady);
        if (iRunning || iEvent != ENone) User::Leave(KErrInUse);
        iTones.Reset();
        for (TInt i = 0; i < aString.Length(); ++i)
            {
            TInt first = 0, second = 0;
            TUint ch = aString[i];
            if (ch == ',' || ch == 'p' || ch == 'P') AddSegmentL(0, 0, iPauseLength);
            else if (Dtmf(ch, first, second))
                {
                AddSegmentL(first, second, iOn);
                AddSegmentL(0, 0, iOff);
                }
            else User::Leave(KErrArgument);
            }
        StartToneL();
        }

    void StartToneL()
        {
        iToneIndex = 0; iToneFrame = 0; iPhase1 = iPhase2 = 0;
        StartL(EStartTone);
        }

    void WriteTone()
        {
        if (!iRunning || iPaused || iFinishing) return;
        TDes8& pcm = iBuffer->Data();
        pcm.Zero();
        while (pcm.Length() < KBufferBytes && iToneIndex < iTones.Count())
            {
            const TToneSegment& s = iTones[iToneIndex];
            TInt sample = 0;
            if (s.iFirst) sample = Sine(iPhase1) / (s.iSecond ? 4 : 2);
            if (s.iSecond) sample += Sine(iPhase2) / 4;
            iPhase1 += s.iStep1;
            iPhase2 += s.iStep2;
            // 5ms fades suppress clicks, including at DTMF segment boundaries.
            TInt64 remaining = s.iFrames - 1 - iToneFrame;
            TInt64 edge = iToneFrame < remaining ? iToneFrame : remaining;
            if (edge < 240) sample = TInt(TInt64(sample) * edge / 240);
            pcm.Append(TUint8(sample & 255));
            pcm.Append(TUint8((sample >> 8) & 255));
            if (++iToneFrame == s.iFrames)
                { ++iToneIndex; iToneFrame = 0; iPhase1 = iPhase2 = 0; }
            }
        iLastWrite = iToneIndex == iTones.Count();
        if (pcm.Length()) WriteBuffer(); else Drain();
        }

public:
    MDevSoundAdaptationObserver& iObserver;
    RNeoAudio iAudio;
    CMMFDataBuffer* iBuffer;
    CAsyncCallBack* iCallback;
    CPeriodic* iDrain;
    TMMFCapabilities iConfig;
    TMMFState iMode;
    TEvent iEvent;
    TInt iEventError;
    TUint iSerial;
    TBool iInitialized, iRunning, iPaused, iDraining, iFinishing;
    TBool iClientBuffer, iNeedBuffer, iLastWrite, iRecordPauseBuffer;
    TInt iFinishError, iWriteBytes, iStreamRate, iStreamChannels;
    TInt iVolume, iGain, iPlayLeft, iPlayRight, iRecordLeft, iRecordRight;
    TInt iOn, iOff, iPauseLength;
    TProcessId iActualProcess, iProcess;
    TUint64 iFrames;
    RArray<TToneSegment> iTones;
    TInt iToneIndex;
    TInt64 iToneFrame;
    TUint32 iPhase1, iPhase2;
    };

// Frozen ordinals 50/51 are data, even with -fno-rtti. Supply the same root
// type metadata representation the build emitter synthesizes for legacy DLLs.
// No typeid/dynamic_cast or runtime RTTI traversal is used by this adaptor.
extern "C" const TAny* const KClassTypeVtable[]
    __asm__("_ZTVN10__cxxabiv117__class_type_infoE");
struct TAdaptorTypeInfo { const TAny* const* iVtable; const char* iName; };
extern "C" const TAdaptorTypeInfo KAdaptorTypeInfo
    __asm__("_ZTI22CMMFDevSoundAdaptation") =
    { KClassTypeVtable + 2, "22CMMFDevSoundAdaptation" };
extern "C" const TAdaptorTypeInfo KBodyTypeInfo
    __asm__("_ZTIN22CMMFDevSoundAdaptation5CBodyE") =
    { KClassTypeVtable + 2, "N22CMMFDevSoundAdaptation5CBodyE" };

CMMFDevSoundAdaptation::CMMFDevSoundAdaptation() {}
void CMMFDevSoundAdaptation::ConstructL(MDevSoundAdaptationObserver& aObserver, MGlobalProperties&)
    { iBody = CBody::NewL(aObserver); }
EXPORT_C CMMFDevSoundAdaptation* CMMFDevSoundAdaptation::NewL(
    MDevSoundAdaptationObserver& aObserver, MGlobalProperties& aProperties)
    {
    CMMFDevSoundAdaptation* self = new(ELeave) CMMFDevSoundAdaptation;
    CleanupStack::PushL(self);
    self->ConstructL(aObserver, aProperties);
    CleanupStack::Pop(self);
    return self;
    }
EXPORT_C CMMFDevSoundAdaptation::~CMMFDevSoundAdaptation() { delete iBody; }
EXPORT_C void CMMFDevSoundAdaptation::PostOpenL() { iBody->ScheduleL(CBody::EComplete); }
EXPORT_C void CMMFDevSoundAdaptation::InitializeL(TMMFState aMode) { iBody->InitializeL(aMode); }
EXPORT_C void CMMFDevSoundAdaptation::InitializeL(TUid, TMMFState)
    { User::Leave(KErrNotSupported); }
EXPORT_C void CMMFDevSoundAdaptation::InitializeL(TFourCC aFourCC, TMMFState aMode)
    {
    if (aFourCC != KMMFFourCCCodePCM16) User::Leave(KErrNotSupported);
    iBody->InitializeL(aMode);
    }
EXPORT_C TInt CMMFDevSoundAdaptation::CancelInitialize()
    {
    if (iBody->iEvent != CBody::EInitialize) return KErrNotReady;
    iBody->StopStream();
    TRAPD(err, iBody->ScheduleL(CBody::EComplete));
    return err;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::Capabilities(TMMFCapabilities& aCap)
    {
    aCap.iRate = KRates;
    aCap.iEncoding = EMMFSoundEncoding16BitPCM;
    aCap.iChannels = EMMFMono | EMMFStereo;
    aCap.iBufferSize = KBufferBytes;
    TRAPD(err, iBody->ScheduleL(CBody::EComplete));
    return err;
    }
EXPORT_C TMMFCapabilities CMMFDevSoundAdaptation::Config() const { return iBody->iConfig; }
EXPORT_C void CMMFDevSoundAdaptation::SetConfigL(const TMMFCapabilities& aCaps)
    {
    if (!Rate(aCaps.iRate) || aCaps.iEncoding != EMMFSoundEncoding16BitPCM
        || (aCaps.iChannels != EMMFMono && aCaps.iChannels != EMMFStereo))
        User::Leave(KErrNotSupported);
    if (iBody->iRunning || iBody->iEvent != CBody::ENone) User::Leave(KErrInUse);
    iBody->ScheduleL(CBody::EComplete);
    iBody->iConfig = aCaps;
    iBody->iConfig.iBufferSize = KBufferBytes;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::MaxVolume() { return KMaxLevel; }
EXPORT_C TInt CMMFDevSoundAdaptation::Volume() { return iBody->iVolume; }
EXPORT_C TInt CMMFDevSoundAdaptation::SetVolume(TInt aVolume, TBool& aAsync)
    {
    aAsync = EFalse;
    TInt volume = Clamp(aVolume, KMaxLevel);
    if (iBody->iAudio.Handle() && iBody->iMode != EMMFStateRecording)
        {
        TInt err = iBody->iAudio.Volume(volume / 100);
        if (err) return err;
        }
    iBody->iVolume = volume;
    return KErrNone;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::MaxGain() { return KMaxLevel; }
EXPORT_C TInt CMMFDevSoundAdaptation::Gain() { return iBody->iGain; }
EXPORT_C TInt CMMFDevSoundAdaptation::SetGain(TInt aGain, TBool& aAsync)
    {
    aAsync = EFalse;
    TInt gain = Clamp(aGain, KMaxLevel);
    if (iBody->iAudio.Handle() && iBody->iMode == EMMFStateRecording)
        {
        TInt err = iBody->iAudio.Volume(gain / 100);
        if (err) return err;
        }
    iBody->iGain = gain;
    return KErrNone;
    }
EXPORT_C void CMMFDevSoundAdaptation::GetPlayBalanceL(TInt& aLeft, TInt& aRight)
    { aLeft = iBody->iPlayLeft; aRight = iBody->iPlayRight; }
EXPORT_C void CMMFDevSoundAdaptation::SetPlayBalanceL(TInt aLeft, TInt aRight, TBool& aAsync)
    {
    aAsync = EFalse;
    if (aLeft < 0 || aLeft > 100 || aRight < 0 || aRight > 100) User::Leave(KErrArgument);
    iBody->iPlayLeft = aLeft; iBody->iPlayRight = aRight;
    }
EXPORT_C void CMMFDevSoundAdaptation::GetRecordBalanceL(TInt& aLeft, TInt& aRight)
    { aLeft = iBody->iRecordLeft; aRight = iBody->iRecordRight; }
EXPORT_C void CMMFDevSoundAdaptation::SetRecordBalanceL(TInt aLeft, TInt aRight, TBool& aAsync)
    {
    aAsync = EFalse;
    if (aLeft < 0 || aLeft > 100 || aRight < 0 || aRight > 100) User::Leave(KErrArgument);
    iBody->iRecordLeft = aLeft; iBody->iRecordRight = aRight;
    }
EXPORT_C void CMMFDevSoundAdaptation::PlayInitL()
    {
    if (iBody->iMode != EMMFStatePlaying) User::Leave(KErrNotReady);
    iBody->StartL(CBody::EStartPlay);
    }
EXPORT_C void CMMFDevSoundAdaptation::RecordInitL()
    {
    if (iBody->iMode != EMMFStateRecording) User::Leave(KErrNotReady);
    iBody->StartL(CBody::EStartRecord);
    }
EXPORT_C void CMMFDevSoundAdaptation::PlayData() { iBody->PlayData(); }
EXPORT_C void CMMFDevSoundAdaptation::RecordData() { iBody->RecordData(); }
EXPORT_C TBool CMMFDevSoundAdaptation::Stop() { iBody->StopStream(); return ETrue; }
EXPORT_C TBool CMMFDevSoundAdaptation::CloseDevSound() { iBody->StopStream(); return ETrue; }
EXPORT_C TInt CMMFDevSoundAdaptation::Pause() { return iBody->Pause(); }
EXPORT_C TInt CMMFDevSoundAdaptation::Resume() { return iBody->Resume(); }
EXPORT_C TBool CMMFDevSoundAdaptation::IsResumeSupported() { return ETrue; }
EXPORT_C TInt CMMFDevSoundAdaptation::SamplesPlayed() { return iBody->Samples(); }
EXPORT_C TInt CMMFDevSoundAdaptation::SamplesRecorded() { return iBody->Samples(); }
EXPORT_C void CMMFDevSoundAdaptation::PlayToneL(TInt aFrequency, const TTimeIntervalMicroSeconds& aDuration)
    { iBody->ToneL(aFrequency, 0, aDuration); }
EXPORT_C void CMMFDevSoundAdaptation::PlayDualToneL(TInt aFirst, TInt aSecond,
    const TTimeIntervalMicroSeconds& aDuration) { iBody->ToneL(aFirst, aSecond, aDuration); }
EXPORT_C void CMMFDevSoundAdaptation::PlayDTMFStringL(const TDesC& aString) { iBody->DtmfL(aString); }
EXPORT_C void CMMFDevSoundAdaptation::PlayToneSequenceL(const TDesC8&) { User::Leave(KErrNotSupported); }
EXPORT_C TInt CMMFDevSoundAdaptation::SetToneRepeats(TInt aCount, const TTimeIntervalMicroSeconds& aSilence)
    {
    if (aCount < -1 || aSilence.Int64() < 0) return KErrArgument;
    return aCount == 0 && aSilence.Int64() == 0 ? KErrNone : KErrNotSupported;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::SetDTMFLengths(TTimeIntervalMicroSeconds32& aOn,
    TTimeIntervalMicroSeconds32& aOff, TTimeIntervalMicroSeconds32& aPause)
    {
    if (aOn.Int() < 0 || aOff.Int() < 0 || aPause.Int() < 0) return KErrArgument;
    iBody->iOn = aOn.Int(); iBody->iOff = aOff.Int(); iBody->iPauseLength = aPause.Int();
    return KErrNone;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::SetVolumeRamp(const TTimeIntervalMicroSeconds& aDuration)
    {
    if (aDuration.Int64() < 0) return KErrArgument;
    return aDuration.Int64() == 0 ? KErrNone : KErrNotSupported;
    }
EXPORT_C TInt CMMFDevSoundAdaptation::SetPrioritySettings(const TMMFPrioritySettings&)
    { return KErrNotSupported; }
EXPORT_C TAny* CMMFDevSoundAdaptation::CustomInterface(TUid) { return NULL; }
EXPORT_C void CMMFDevSoundAdaptation::GetSupportedInputDataTypesL(RArray<TFourCC>& aTypes,
    const TMMFPrioritySettings&) const
    { aTypes.Reset(); User::LeaveIfError(aTypes.Append(KMMFFourCCCodePCM16)); }
EXPORT_C void CMMFDevSoundAdaptation::GetSupportedOutputDataTypesL(RArray<TFourCC>& aTypes,
    const TMMFPrioritySettings&) const
    { aTypes.Reset(); User::LeaveIfError(aTypes.Append(KMMFFourCCCodePCM16)); }
EXPORT_C TInt CMMFDevSoundAdaptation::SetClientConfig(const TProcessId& aProcess)
    { return SetClientConfig(aProcess, aProcess); }
EXPORT_C TInt CMMFDevSoundAdaptation::SetClientConfig(const TProcessId& aActual, const TProcessId& aProcess)
    { iBody->iActualProcess = aActual; iBody->iProcess = aProcess; return KErrNone; }
EXPORT_C TInt CMMFDevSoundAdaptation::EmptyBuffers() { return KErrNotSupported; }
EXPORT_C TInt CMMFDevSoundAdaptation::ProcessingFinishedReceived(TBool& aAsync)
    { return iBody->Processing(aAsync); }
EXPORT_C TInt CMMFDevSoundAdaptation::ProcessingError(TBool& aAsync)
    { return iBody->Processing(aAsync); }
EXPORT_C TInt CMMFDevSoundAdaptation::RegisterAsClient(TUid, const TDesC8&) { return KErrNotSupported; }
EXPORT_C TInt CMMFDevSoundAdaptation::CancelRegisterAsClient(TUid) { return KErrNotSupported; }
EXPORT_C TInt CMMFDevSoundAdaptation::GetResourceNotificationData(TUid, TDes8&) { return KErrNotSupported; }
EXPORT_C TInt CMMFDevSoundAdaptation::WillResumePlay() { return KErrNotSupported; }
EXPORT_C TInt CMMFDevSoundAdaptation::GetTimePlayed(TTimeIntervalMicroSeconds& aTime)
    {
    iBody->Snapshot();
    TInt rate = iBody->iStreamRate ? iBody->iStreamRate : Rate(iBody->iConfig.iRate);
    TUint64 frames = iBody->iFrames;
    TUint64 seconds = frames / rate;
    if (seconds > TUint64(KMaxTInt64 / 1000000)) return KErrOverflow;
    aTime = TTimeIntervalMicroSeconds(TInt64(seconds * 1000000 + (frames % rate) * 1000000 / rate));
    return KErrNone;
    }
EXPORT_C void CMMFDevSoundAdaptation::BufferErrorEvent() { iBody->DeferError(KErrNoMemory); }
EXPORT_C void CMMFDevSoundAdaptation::RollbackAdaptorActiveStateToBeforeCommit()
    {
    // NeoAudio has no commit cycle. Its synchronous config operations are atomic;
    // there is no pending ACF transaction to roll back.
    }
