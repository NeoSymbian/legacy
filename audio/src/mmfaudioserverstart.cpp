// SPDX-License-Identifier: Apache-2.0
// The stock RMMFAudioServerProxy starts MMFAudioServerStart.exe and waits
// for rendezvous. CAudioSvrLoader owns the existing root audio server.
#include <e32base.h>
#include <a3f/mmfaudioserverinterface.h>

static void RunAudioServerL()
    {
    CActiveScheduler* scheduler = new(ELeave) CActiveScheduler;
    CleanupStack::PushL(scheduler);
    CActiveScheduler::Install(scheduler);

    // The default EFalse retains the root server's normal idle shutdown:
    // the final DevSound session can close and a later application restart it.
    CAudioSvrLoader* server = CAudioSvrLoader::NewL();
    CleanupStack::PushL(server);

    // Construction includes StartL(!MMFAudioServer), FourCC metadata and
    // the server factory. Never advertise readiness before all three exist.
    RProcess::Rendezvous(KErrNone);
    CActiveScheduler::Start();

    CleanupStack::PopAndDestroy(server);
    CActiveScheduler::Install(NULL);
    CleanupStack::PopAndDestroy(scheduler);
    }

GLDEF_C TInt E32Main()
    {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    if (!cleanup)
        {
        RProcess::Rendezvous(KErrNoMemory);
        return KErrNoMemory;
        }
    TRAPD(error, RunAudioServerL());
    if (error != KErrNone)
        {
        // In particular KErrAlreadyExists allows the stock proxy's duplicate
        // launcher recovery to reconnect to the process that won the race.
        RProcess::Rendezvous(error);
        }
    delete cleanup;
    return error;
    }
