# SPDX-License-Identifier: Apache-2.0
"""components -- what the legacy environment adds to NeoSymbian/build's component registry (its emitter/components.py
merges this file from src/legacy/): the 2010 application framework, Uikon and Avkon that old S60 apps run on, our
LEGACY.EXE host, the C++ run time of RVCT-built binaries, and the libraries old apps load.

Same format as build's registry (key = the LIBRARY stem; `mmp` relative to build's src/; `rom_data` paths relative to
the build checkout's root; the fields are documented there).
"""
import os

SRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # build's src/ (this repo is src/legacy)

KCS = "oss.FCL.sf.os.kernelhwsrv/kernel/eka/compsupp"   # the C++ run-time support (.defs, scppnwdl, our gcce/ DLLs)
OSSRV = "oss.FCL.sf.os.ossrv"
PDATA = "oss.FCL.sf.os.persistentdata"
TXT = "oss.FCL.sf.os.textandloc"
CUI = "oss.FCL.sf.mw.classicui"
APF = "oss.FCL.sf.mw.appsupport/appfw"
G = "oss.FCL.sf.os.graphics"
SFVG = f"{G}/openvg/openvgrefimplementation/sfopenvg"   # the software OpenVG + EGL reference implementation
SVGT = "oss.FCL.sf.mw.svgt"
XML = "oss.FCL.sf.os.xmlsrv"
OPENC = "oss.FCL.sf.os.ossrv/genericopenlibs/openenvcore"
# Open C's headers mark float functions __SOFTFP (RVCT's __softfp); ours is the soft-float ABI already
OPENC_DEFS = ["__SOFTFP="]
# STLport takes std::nothrow_t from symcpp.h, which RVCT's prefix header included (as scppnwdl's entry does)
SYMCPP = ["-include", os.path.join(SRC, KCS, "symcpp", "symcpp.h")]
# FindUtil.h is only in classicui master (BRANCH_RCL_3 dropped findutil_api): that one directory, not master's headers
FINDUTIL_INC = ["-I", os.path.join(SRC, "oss.FCL.sf.mw.classicui@master", "classicui_plat", "findutil_api", "inc")]

COMPONENTS = {
    # --- the application framework and control environment (legacy GUI apps; Phorepoc's registry is the map) ---
    "numberconversion": dict(mmp=f"{TXT}/textrendering/numberformatting/group/NumberConversion.mmp"),
    "field": dict(mmp=f"{TXT}/textrendering/texthandling/group/FIELD.MMP"),
    "etext": dict(mmp=f"{TXT}/textrendering/texthandling/group/ETEXT.MMP"),
    "tagma": dict(mmp=f"{TXT}/textrendering/textformatting/group/TAGMA.MMP"),
    "form": dict(mmp=f"{TXT}/textrendering/textformatting/group/FORM2.MMP"),
    "apmime": dict(mmp=f"{OSSRV}/genericservices/mimerecognitionfw/apmime/APMIME.MMP"),
    "apparc": dict(mmp=f"{APF}/apparchitecture/group/APPARC.MMP"),
    "apgrfx": dict(mmp=f"{APF}/apparchitecture/group/APGRFX.MMP"),
    "apfile": dict(mmp=f"{APF}/apparchitecture/group/APFILE.MMP"),
    "aplist": dict(mmp=f"{APF}/apparchitecture/group/APLIST.MMP"),
    # the server's Central Repository settings (apparc's own and the icon/caption overrides), as the 2010 release shipped them
    "apserv": dict(mmp=f"{APF}/apparchitecture/group/APSERV.MMP", rom_data=[
        f"private\\10202be9\\{uid}.txt=src/{APF}/apparchitecture/tdata/{uid}.txt" for uid in ("10003a3f", "1028583d")]),
    "apsexe": dict(mmp=f"{APF}/apparchitecture/group/APSEXE.MMP"),
    "viewcli": dict(mmp=f"{APF}/viewserver/group/VIEWCLI.MMP"),
    "viewsrv": dict(mmp=f"{APF}/viewserver/group/VIEWSRV.MMP"),
    "egul": dict(mmp=f"{CUI}/lafagnosticuifoundation/uigraphicsutils/group/EGUL.MMP"),
    "cone": dict(mmp=f"{CUI}/lafagnosticuifoundation/cone/group/CONE.MMP"),
    # with the FEP framework's Central Repository settings (CCoeEnv's FEP tracker opens 10272618 at start-up)
    "fepbase": dict(mmp="oss.FCL.sf.mw.inputmethods/fep/frontendprocessor/group/FEPBASE.MMP", rom_data=[
        f"private\\10202be9\\{uid}.txt=src/oss.FCL.sf.mw.inputmethods/fep/frontendprocessor/group/{uid}.txt"
        for uid in ("10272618", "10272619", "10009793")]),
    # the error-resolver texts (errorres.rsg is included by eikcore and commonengine); also at the path S60's CTextResolver opens
    "errorres": dict(mmp="oss.FCL.sf.os.devicesrv/commonservices/commonengineresources/group/commonengineresources.mmp",
                     rsc_only=True, rom_data=[r"resource\errors\errorres.rsc=emitter/rsc/resource/errorres.rsc"]),
    # --- Uikon + Avkon (S60 classicui) and their support libraries: the legacy GUI apps' base (map: Phorepoc's registry) ---
    "commonengine": dict(mmp=f"{OSSRV}/genericservices/s60compatibilityheaders/commonengine/group/commonengine.mmp"),
    "featmgr": dict(mmp=f"{PDATA}/featuremgmt/featuremgr/group/featmgr.mmp"),
    "featmgrserver": dict(mmp=f"{PDATA}/featuremgmt/featuremgr/group/featmgrserver.mmp", rom_data=[r"private\10205054\features.dat=emitter/features.dat"]),
    "cenrepnotifhandler": dict(mmp=f"{PDATA}/persistentstorage/centralrepository/cenrepnotifierhandler/group/cenrepnotifhandler.mmp"),
    "platformenv": dict(mmp="oss.FCL.sf.os.devicesrv/commonservices/PlatformEnv/group/PlatformEnv.mmp", **{"def": "oss.FCL.sf.os.devicesrv/commonservices/PlatformEnv/EABI/PlatformEnvCenRepU.DEF"}),
    "sysversioninfo": dict(mmp="oss.FCL.sf.os.devicesrv/commonservices/PlatformEnv/sysversioninfo/group/sysversioninfo.mmp"),
    "platformver": dict(mmp="oss.FCL.sf.os.devicesrv/commonservices/PlatformEnv/platformver/group/platformver.mmp"),
    "disknotifyhandler": dict(mmp="oss.FCL.sf.os.devicesrv/commonservices/PlatformEnv/disknotifyhandler/group/disknotifyhandler.mmp"),
    "syslangutil": dict(mmp="oss.FCL.sf.os.devicesrv/sysstatemgmt/syslangutil/group/syslangutil.mmp"),
    "jplangutil": dict(mmp=f"{TXT}/textandlocutils/jplangutil/group/jplangutil.mmp"),
    "numbergrouping": dict(mmp=f"{TXT}/textandlocutils/numbergrouping/group/NumberGrouping.mmp"),
    "etul": dict(mmp=f"{CUI}/lafagnosticuifoundation/uigraphicsutils/group/etul.mmp"),
    "clock": dict(mmp=f"{CUI}/lafagnosticuifoundation/clockanim/group/CLOCK.MMP"),
    "clocka": dict(mmp=f"{CUI}/lafagnosticuifoundation/clockanim/group/CLOCKA.MMP"),
    "bmpanim": dict(mmp=f"{CUI}/lafagnosticuifoundation/bmpanimation/group/BmpAnim.mmp"),
    "bmpansrv": dict(mmp=f"{CUI}/lafagnosticuifoundation/bmpanimation/group/BmpAnSrv.mmp"),
    "eiksrvc": dict(mmp=f"{CUI}/commonuisupport/uikon/group/EIKSRVC.MMP"),
    "eikcore": dict(mmp=f"{CUI}/commonuisupport/uikon/group/EIKCORE.MMP", foff_dynamic=True, rtti=True),   # dynamic_cast<CAknAppUi*> needs the typeinfo; eikprivate.cpp: _FOFF(.., iBuf[maxLength])
    "eiksrv": dict(mmp=f"{CUI}/commonuisupport/uikon/group/eiksrv.mmp"),
    "eiksrvs": dict(mmp=f"{CUI}/commonuisupport/uikon/group/EIKSRVS.MMP"),
    "eiksrvui": dict(mmp=f"{CUI}/uifw/EikStd/srvuisrc/EIKSRVUI.MMP"),
    "eikalert": dict(mmp=f"{CUI}/uifw/EikStd/alertsrc/Eikalert.mmp"),
    "uiklaf": dict(mmp=f"{CUI}/uifw/uiklaf/Group/UIKLAF.MMP", **{"def": f"{CUI}/commonuisupport/uilaf/eabi/uiklaf_unbranchedu.def"}),
    "eikcoctllaf": dict(mmp=f"{CUI}/uifw/uiklaf/Group/EikCoCtlLaf.MMP"),
    "eikcoctl": dict(mmp=f"{CUI}/uifw/EikStd/coctlsrc/EIKCOCTL.MMP", flags=["-fkeep-inline-functions"], rtti=True),
    "eikctl": dict(mmp=f"{CUI}/uifw/eikctl/group/eikctl.mmp"),
    "eikdlg": dict(mmp=f"{CUI}/uifw/EikStd/dlgsrc/EIKDLG.MMP"),
    "eikcdlg": dict(mmp=f"{CUI}/uifw/EikStd/cdlgsrc/EIKCDLG.MMP"),
    "avkon": dict(mmp=f"{CUI}/uifw/AvKon/group/avkon.mmp", rtti=True, flags=FINDUTIL_INC, rom_data=[f"private\\10202be9\\101F876E.txt=src/{CUI}/uifw/AvKon/CenRep/101F876E.txt",
                                                                                          "resource\\apps\\avkon2.mif=emitter/avkon2.mif",
                                                                                          f"private\\10202be9\\2001844B.txt=src/{CUI}/uifw/AvKon/CenRep/2001844B.txt"]),
    "aknnotify": dict(mmp=f"{CUI}/uifw/AvKon/notifsrc/aknNotify.mmp"),
    # the Uikon library (ECOM) plug-in that installs Avkon's app UI factory and control factories into CEikonEnv
    "akninit": dict(mmp=f"{CUI}/uifw/AvKon/initsrc/akninit.mmp"),
    "akncapserverclient": dict(mmp=f"{CUI}/uifw/AknGlobalUI/AknCapServer/group/AknCapServerClient.mmp"),
    # what AknCapServer links besides (it panicked on their stubs at start-up)
    "akndynamicsoftnoteeventmanager": dict(mmp=f"{CUI}/uifw/AknGlobalUI/AknDynamicSoftNote/group/AknDynamicSoftNoteEventManager.mmp"),
    "ecompluginnotifier": dict(mmp=f"{CUI}/uifw/AknGlobalUI/AknCapServer/group/EComPluginNotifier.mmp"),
    "aknlistloadertfx": dict(mmp=f"{CUI}/uifw/AvKon/AknListLoaderTfx/group/aknlistloadertfx.mmp"),
    "widgetregistryclient": dict(mmp="oss.FCL.sf.mw.web/webengine/widgetregistry/group/WidgetRegistryClient.mmp"),
    # rom_data: the installed-language list SysLangUtil reads (AknCapServer publishes it at start-up)
    "akncapserver": dict(mmp=f"{CUI}/uifw/AknGlobalUI/AknCapServer/group/AknCapServer.mmp", rtti=True,
                         rom_data=["resource\\bootdata\\languages.txt="
                                   "src/oss.FCL.sf.os.deviceplatformrelease/Version/data/Languages.txt",
                                   "private\\10202be9\\101F876D.txt=emitter/cenrep/101F876D.txt",
                                   "private\\10202be9\\200159ED.txt=emitter/cenrep/200159ED.txt",
                                   "private\\10202be9\\10282CB2.txt=emitter/cenrep/10282CB2.txt",
                                   "private\\10202be9\\10205CA7.txt=emitter/cenrep/10205CA7.txt",
                                   "private\\10202be9\\101F8798.txt=emitter/cenrep/101F8798.txt"]),
    "aknanimdll": dict(mmp=f"{CUI}/uifw/AvKon/animdllsrc/AknAnimDll.mmp"),
    "akntransitionutils": dict(mmp=f"{CUI}/uifw/AvKon/AknTransitionUtils/group/akntransitionutils.mmp"),
    "aknphysics": dict(mmp=f"{CUI}/uifw/AvKon/aknphysics/group/aknphysics.mmp"),
    "gfxtrans": dict(mmp=f"{CUI}/lafagnosticuifoundation/graphicseffects/group/GfxTransEffect.mmp"),
    # Symbian's own null transition-effects adapter (no effects engine on this board)
    "gfxtransadapter": dict(mmp=f"{CUI}/lafagnosticuifoundation/graphicseffects/group/GfxTransAdapterStub.mmp",
                            rom="gfxtransadapter.dll"),
    "servicehandler": dict(mmp=f"{CUI}/applicationinterworkingfw/ServiceHandler/group/ServiceHandler.mmp"),
    "hlplch": dict(mmp=f"{CUI}/uiutils/hlplch/group/hlplch.mmp"),
    "findutil": dict(mmp=f"{CUI}/uiutils/Findutil/group/FindUtil.mmp", flags=FINDUTIL_INC),
    "cdlengine": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/cdl/CdlEngine/group/CdlEngine.mmp"),
    "cdlserver": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/cdl/CdlServer/group/CdlServer.mmp"),
    "aknlayout2": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/aknlayout2/group/AknLayout2.mmp"),
    "aknlayout2scalable": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/aknlayout2/group/AknLayout2Scalable.mmp"),
    "layoutinst": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/aknlayout2/group/../generated/layoutinst.mmp", rom="101fe2aa.dll"),
    # one resolution's layout data: nHD (360x640 / 640x360) -- boot with SCREEN=360x640
    "layoutinst_nhd4": dict(mmp="oss.FCL.sf.mw.uiresources/layouts/aknlayout2/generated/Nhd4_akn_app/layoutinst_nhd4.mmp",
                            rom="2001cb93.dll"),
    "aknskins": dict(mmp="oss.FCL.sf.mw.uiresources/skins/AknSkins/group/AknSkins.mmp"),
    "aknskinsrv": dict(mmp="oss.FCL.sf.mw.uiresources/skins/AknSkins/group/AknSkinSrv.mmp", defs=["AKNS_TRACING_ENABLED_ERROR"], rom_data=[   # Personalisation (skins), Themes (from its .crml)
        "private\\10202be9\\101F876F.txt=src/oss.FCL.sf.mw.uiresources/skins/AknSkins/CenRep/101F876F.txt",
        "private\\10202be9\\102818E8.txt=emitter/cenrep/102818E8.txt",
        "private\\10202be9\\20021192.txt=emitter/cenrep/20021192.txt",   # Avkon's kinetic scrolling: off
        "private\\10202be9\\102818EB.txt=emitter/cenrep/102818EB.txt"]),
    # X-plore (an old .sis app) loads it at start
    "aknswallpaperutils": dict(mmp="oss.FCL.sf.mw.uiresources/skins/AknSkins/group/AknsWallpaperUtils.mmp"),
    "aknskinsrvexe": dict(mmp="oss.FCL.sf.mw.uiresources/skins/AknSkins/group/AknSkinSrvMain.mmp", rom="aknskinsrv.exe",
                          rom_data=[r"private\10207114\import\101f84b9\series60skin.skn=emitter/skin/series60skin.skn",
                                    r"private\10207114\import\101f84b9\series60skin.mif=emitter/skin/series60skin.mif",
                                    r"private\10207114\import\101f84b9\series60skin.mbm=emitter/skin/series60skin.mbm"]),
    "aknskinrenderlib": dict(mmp="oss.FCL.sf.mw.uiresources/skins/AknSkins/group/AknSkinRenderLib.mmp", flags=["-U__MARM_ARMV5__"]),
    "aknicon": dict(mmp="oss.FCL.sf.mw.uiresources/uigraphics/AknIcon/group/AknIcon.mmp", rtti=True),
    "akniconsrv": dict(mmp="oss.FCL.sf.mw.uiresources/uigraphics/AknIcon/group/AknIconSrvMain.mmp"),
    "fontprovider": dict(mmp="oss.FCL.sf.mw.uiresources/fontsupport/fontprovider/Group/FontProvider.mmp"),
    "fontutils": dict(mmp="oss.FCL.sf.mw.uiresources/fontsupport/fontutils/FontUtils/group/FontUtils.mmp"),
    "aknpictograph": dict(mmp="oss.FCL.sf.mw.uiresources/pictographs/AknPictograph/group/AknPictograph.mmp"),
    "ptiengine": dict(mmp="oss.FCL.sf.mw.inputmethods/textinput/ptienginev2/group/ptienginev2.mmp"),
    # its key-map data (an ECom plugin per language; English only for now): without one CPtiEngine::NewL leaves
    # KErrCorrupt ("cannot function without keymapping data"), which killed X-plore at start-up
    "ptikeymappings_itut_01": dict(
        mmp="oss.FCL.sf.mw.inputmethods/textinput/ptienginev2/languages/PtiKeymappings_ITUT_01.mmp"),
    "touchfeedback": dict(mmp="oss.FCL.sf.mw.hapticsservices/tactilefeedback/tactilefeedbackclient/group/touchfeedback.mmp"),
    # the window server click plugin: Avkon's key-sound anim asks it for MTactileFeedbackServer (NULL unless loaded)
    "tactileclickplugin": dict(mmp="oss.FCL.sf.mw.hapticsservices/tactilefeedback/tactileclickplugin/group/tactileclickplugin.mmp"),
    "aknnotifyplugin2": dict(mmp=f"{CUI}/uifw/AknGlobalUI/group/AknNotifyPlugin2.mmp"),
    "aknnotifyplugin": dict(mmp=f"{CUI}/uifw/AvKon/AknNotifyPlugin_dll/group/AknNotifyPlugin.mmp"),   # eiksrvui's first call
    "aknnotifierwrapper": dict(mmp=f"{CUI}/uifw/AknGlobalUI/NotifierWrapper/group/aknnotifierwrapper.mmp"),
    "findutilimpl": dict(mmp=f"{CUI}/uiutils/Findutil/group/FindUtilImpl.mmp", flags=FINDUTIL_INC),
    # the FEP's resources only (avkon's character map includes aknfep.rsg); the FEP itself is not built
    "aknfep": dict(mmp="oss.FCL.sf.mw.inputmethods/fep/aknfep/group/AknFep.mmp", rsc_only=True),
    # --- the legacy GUI apps ---
    # ours: a 2010 GUI program inside neoui -- the guest window server bridge (host/legacy.cpp)
    "legacy": dict(mmp="legacy/host/legacy.mmp"),
    # the C++ run time RVCT-built binaries (old .sis apps) import: ours from GCC's runtime under RVCT's ordinals
    # (kernel compsupp/gcce), and the release's operator new/delete
    "drtaeabi": {"mmp": f"{KCS}/gcce/drtaeabi.mmp", "def": f"{KCS}/eabi/drtaeabiu.def", "rtti": True,
                 "allow_wsd": True, "export_archives": True, "unwinder": True},
    "dfpaeabi": {"mmp": f"{KCS}/gcce/dfpaeabi.mmp", "def": f"{KCS}/eabi/dfpaeabiu.def", "export_archives": True},
    # zlib (ezlib's compressor/decompressor classes over libzcore): old apps decompress their data with it
    "libzcore": dict(mmp=f"{OSSRV}/compressionlibs/ziplib/group/libz/libzcore.mmp"),
    "ezlib": dict(mmp=f"{OSSRV}/compressionlibs/ziplib/group/ezlib.mmp"),
    # what X-plore (an old .sis app) loads besides: real sources where the release has them ...
    "featdiscovery": dict(mmp="oss.FCL.sf.os.persistentdata/featuremgmt/featuremgr/group/featdiscovery.mmp"),
    "securesocket": dict(mmp="oss.FCL.sf.os.networkingsrv/networksecurity/tls/group/SecureSocket.mmp"),
    # the ETel client (CTelephony's RTelServer): with no ETel server its Connect fails cleanly (our cellularsrv fork)
    "etel": dict(mmp="oss.FCL.sf.os.cellularsrv/telephonyserver/etelserverandcore/group/ETEL.MMP"),
    "etelmm": dict(mmp="oss.FCL.sf.os.cellularsrv/telephonyserver/etelmultimode/group/etelmm.mmp"),
    "etel3rdparty": dict(mmp="oss.FCL.sf.os.cellularsrv/telephonyutils/etel3rdpartyapi/Group/Etel3rdParty.mmp"),
    "mediaclientaudio": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/client/MediaClientAudio.mmp"),
    # CMdaAudioOutputStream (Frozen Bubble imports it) and the DevSound client it opens. There is no audio server
    # (mmfaudioserver.exe) yet: opening DevSound fails with KErrNotFound, so apps run silent.
    "mediaclientaudiostream": dict(
        mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/client/MediaClientAudioStream.mmp"),
    "mediaclient": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/client/MediaClient.mmp"),
    "mmfdevsound": dict(mmp="oss.FCL.sf.os.mmaudio/devsound/a3fdevsound/mmpfiles/mmfdevsound.mmp"),
    "mmfdevsoundproxy": dict(mmp="oss.FCL.sf.os.mmaudio/devsound/a3fdevsound/mmpfiles/mmfdevsoundproxy.mmp"),
    "mmfaudioserverproxy": dict(mmp="oss.FCL.sf.os.mmaudio/devsound/a3fdevsound/mmpfiles/mmfaudioserverproxy.mmp"),
    "mmcommonutils": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmutilitylib/mmpfiles/MMCommonUtils.mmp"),
    # the MMF message types DevSound's proxy is made of (RMMFDevSoundProxy's constructor builds a
    # TMMFMessageDestination: a panic stub killed Frozen Bubble's mixer thread)
    "mmfcontrollerframework": dict(
        mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/ControllerFramework/MmfControllerFramework.mmp"),
    "mmfserverbaseclasses": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/server/mmfserverbaseclasses.mmp"),
    "mediaclientvideo": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmfw/MMPFiles/client/MediaClientVideo.mmp"),
    # ... and stubs for the libraries it has none of (Bluetooth, IrDA/OBEX, contacts, calendar, messaging): the program
    # loads them at start, and calls them only for those features
    "bluetooth": {"stub": 0}, "btextnotifiers": {"stub": 0x1000A5FA}, "sdpagent": {"stub": 0x10009222},
    "sdpdatabase": {"stub": 0x10009220}, "irda": {"stub": 0x10003D54}, "irobex": {"stub": 0x10201EF5},
    "cntmodel": {"stub": 0x10003A71}, "calinterimapi": {"stub": 0x102075E4}, "msgs": {"stub": 0x10004E66},
    # remote control (headset keys): the release's sources need the Bluetooth logger header it does not export
    "remconcoreapi": {"stub": 0x101F9067}, "remconinterfacebase": {"stub": 0x101F9067},
    "scppnwdl": dict(mmp=f"{KCS}/symcpp/scppnwdl.mmp",      # std::nothrow_t: RVCT's prefix header included this
                     flags=["-include", os.path.join(SRC, KCS, "symcpp", "symcpp.h")], no_gcchelp=True),
    # --- SVG icons (an app's MIF icon is SVG): Avkon's icon server draws them with the SVG-T engine (mw.svgt) over the
    # software OpenVG reference implementation (os.graphics sfopenvg); the engine parses with the XML interface and
    # links the C libraries (the old one, estlib, and Open C under sfopenvg) ---
    "xmlinterface": dict(mmp=f"{XML}/xml/xmlparser/group/XMLInterface.mmp"),
    "cxmlparser": dict(mmp=f"{XML}/xml/cxmllibrary/group/cXmlParser.mmp"),
    "estlib": dict(mmp=f"{OSSRV}/genericopenlibs/cstdlib/BMMP/ESTLIB.MMP", c_sources=True, defs=OPENC_DEFS),
    "redircli": dict(mmp=f"{OSSRV}/genericopenlibs/cstdlib/RedirCli/REDIRCLI.MMP"),
    "c32": dict(mmp="oss.FCL.sf.os.commsfw/serialserver/c32serialserver/group/C32.MMP"),
    "libc": dict(mmp=f"{OPENC}/libc/group/libc.mmp", c_sources=True, defs=OPENC_DEFS),
    "libm": dict(mmp=f"{OPENC}/libm/group/libm.mmp", c_sources=True, defs=OPENC_DEFS),
    "libpthread": dict(mmp=f"{OPENC}/libpthread/group/libpthread.mmp", c_sources=True, defs=OPENC_DEFS),
    "backend": dict(mmp=f"{OPENC}/backend/group/backend.mmp", c_sources=True, defs=OPENC_DEFS),
    "libstdcpp": dict(defs=OPENC_DEFS, mmp=f"{OSSRV}/stdcpp/group/libstdcpp.mmp", rtti=True, cxx_runtime=True,
                      no_gcchelp=True,   # it defines operator new/delete itself
                      flags=["-fexceptions", *SYMCPP]),
    "rpipe": dict(mmp="oss.FCL.sf.os.kernelhwsrv/kernel/eka/euser/rpipe.mmp"),
    "tzclient": dict(mmp="oss.FCL.sf.mw.appsupport/tzservices/tzserver/group/TimeZoneClient.mmp"),
    "timezonelocalization": dict(mmp="oss.FCL.sf.mw.appsupport/tzservices/tzloc/group/TimeZoneLocalization.mmp"),
    "libopenvg_sw": dict(defs=OPENC_DEFS, mmp=f"{SFVG}/group/openvg.mmp", **{"def": f"{G}/openvg/openvginterface/eabi/libopenvg11u.def"}),
    "libopenvgu_sw": dict(defs=OPENC_DEFS, mmp=f"{SFVG}/group/openvgu.mmp", **{"def": f"{G}/openvg/openvginterface/eabi/libopenvgu11u.def"}),
    "libegl_sw": dict(defs=OPENC_DEFS, mmp=f"{SFVG}/group/egl.mmp", **{"def": f"{G}/egl/eglinterface/eabi/libegl13U.def"}),
    "sflibopenvgrefimpl": dict(defs=OPENC_DEFS, mmp=f"{SFVG}/group/sfopenvg.mmp", rtti=True, cxx_runtime=True,
                               flags=["-fexceptions", *SYMCPP]),
    "libvgi": dict(defs=OPENC_DEFS, mmp=f"{SFVG}/group/vgi.mmp"),
    # the image decoders' framework (the engine's <image> elements; an icon of paths never calls it)
    "imageprocessorperf": dict(mmp="oss.FCL.sf.os.mmimaging/imaging/imagingfws/group/ImageProcessorPerf.mmp"),
    "imageconversion": dict(mmp="oss.FCL.sf.os.mmimaging/imaging/imagingfws/group/ImageConversion.mmp"),
    "imageconversionresolver": dict(mmp="oss.FCL.sf.os.mmimaging/imaging/imagingfws/group/ImageConversionResolver.mmp"),
    "mmcommon": dict(mmp="oss.FCL.sf.os.mmaudio/mmlibs/mmutilitylib/mmpfiles/MMCommon.mmp"),
    "svgengine": dict(mmp=f"{SVGT}/svgtopt/SVG/SVGEngine/group/SVGEngine.mmp",
                      flags=["-D__int64=long long"],   # Gfxmath.h's RVCT/MSVC keyword
                      c_sources=True,                  # GfxMath.c: C linkage, as its callers declare it
                      **{"def": f"{SVGT}/svgtopt/SVG/SVGEngine/eabi/SVGENGINE-OpenVGU.def"}),
    "vgrenderer": dict(mmp=f"{SVGT}/svgtopt/VGRenderer/group/VGRenderer.mmp"),
    "swvg": dict(mmp=f"{SVGT}/svgtopt/VGRenderer/SWVG/group/SWVG.mmp",
                 **{"def": f"{SVGT}/svgtopt/VGRenderer/SWVG/eabi/SWVG.def"}),
}
