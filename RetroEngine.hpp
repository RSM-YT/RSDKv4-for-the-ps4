#ifndef RETROENGINE_H
#define RETROENGINE_H
#include <stdint.h>
// Disables POSIX use c++ name blah blah stuff
#pragma warning(disable : 4996)

// Setting this to true removes (almost) ALL changes from the original code, the trade off is that a playable game cannot be built, it is advised to
// be set to true only for preservation purposes
#ifndef RETRO_USE_ORIGINAL_CODE
#define RETRO_USE_ORIGINAL_CODE (0)
#endif

typedef enum OrbisSysModuleInternal : uint32_t
{
    ORBIS_SYSMODULE_INTERNAL_AUDIOOUT = 0x80000001, // libSceAudioOut
    ORBIS_SYSMODULE_INTERNAL_AUDIOIN = 0x80000002, // libSceAudioIn
    ORBIS_SYSMODULE_INTERNAL_AV_CAPTURE = 0x80000003, // libSceAvcap
    ORBIS_SYSMODULE_INTERNAL_SYSCORE = 0x80000004, // libSceSysCore
    ORBIS_SYSMODULE_INTERNAL_CDLG_UTILITY_SERVER = 0x80000007, // libSceCdlgUtilServer
    ORBIS_SYSMODULE_INTERNAL_KEYBOARD = 0x80000008, // libSceKeyboard
    ORBIS_SYSMODULE_INTERNAL_NETCTL = 0x80000009, // libSceNetCtl
    ORBIS_SYSMODULE_INTERNAL_HTTP = 0x8000000A, // libSceHttp
    ORBIS_SYSMODULE_INTERNAL_SSL = 0x8000000B, // libSceSsl
    ORBIS_SYSMODULE_INTERNAL_NP_COMMON = 0x8000000C, // libSceNpCommon
    ORBIS_SYSMODULE_INTERNAL_NP_MANAGER = 0x8000000D, // libSceNpManager
    ORBIS_SYSMODULE_INTERNAL_NP_WEB_API = 0x8000000E, // libSceNpWebApi
    ORBIS_SYSMODULE_INTERNAL_SAVE_DATA = 0x8000000F, // libSceSaveData
    ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE = 0x80000010, // libSceSystemService
    ORBIS_SYSMODULE_INTERNAL_USER_SERVICE = 0x80000011, // libSceUserService
    ORBIS_SYSMODULE_INTERNAL_VISION_MANAGER = 0x80000012, // libSceVisionManager
    ORBIS_SYSMODULE_INTERNAL_AC3_ENC = 0x80000013, // libSceAc3Enc
    ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL = 0x80000014, // libSceAppInstUtil
    ORBIS_SYSMODULE_INTERNAL_VDECCORE = 0x80000015, // libSceVdecCore
    ORBIS_SYSMODULE_INTERNAL_VENCCORE = 0x80000016, // libSceVencCore (file not found)
    ORBIS_SYSMODULE_INTERNAL_HID_CONTROL = 0x80000017, // libSceHidControl
    ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG = 0x80000018, // libSceCommonDialog
    ORBIS_SYSMODULE_INTERNAL_PERF = 0x80000019, // libScePerf
    ORBIS_SYSMODULE_INTERNAL_CAMERA = 0x8000001A, // libSceCamera
    ORBIS_SYSMODULE_INTERNAL_NET = 0x8000001C, // libSceNet
    ORBIS_SYSMODULE_INTERNAL_IPMI = 0x8000001D, // libSceIpmi
    ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT = 0x80000022, // libSceVideoOut
    ORBIS_SYSMODULE_INTERNAL_BGFT = 0x8000002A, // libSceBgft
    ORBIS_SYSMODULE_INTERNAL_PRECOMPILED_SHADERS = 0x80000064, // libScePrecompiledShaders
    ORBIS_SYSMODULE_INTERNAL_PAD = 0x80000024, // libScePad
} OrbisSysModuleInternal;
typedef enum OrbisSysModule : uint16_t
{
    ORBIS_SYSMODULE_INVALID = 0x0000,
    ORBIS_SYSMODULE_FIBER = 0x0006, // libSceFiber.sprx
    ORBIS_SYSMODULE_ULT = 0x0007, // libSceUlt.sprx
    ORBIS_SYSMODULE_NGS2 = 0x000B, // libSceNgs2.sprx
    ORBIS_SYSMODULE_XML = 0x0017, // libSceXml.sprx
    ORBIS_SYSMODULE_NP_UTILITY = 0x0019, // libSceNpUtility.sprx
    ORBIS_SYSMODULE_VOICE = 0x001A, // libSceVoice.sprx
    ORBIS_SYSMODULE_VOICEQOS = 0x001B, // libSceVoiceQos.sprx
    ORBIS_SYSMODULE_NP_MATCHING2 = 0x001C, // libSceNpMatching2.sprx
    ORBIS_SYSMODULE_NP_SCORE_RANKING = 0x001E, // libSceNpScoreRanking.sprx
    ORBIS_SYSMODULE_RUDP = 0x0021, // libSceRudp.sprx
    ORBIS_SYSMODULE_NP_TUS = 0x002C, // libSceNpTus.sprx
    ORBIS_SYSMODULE_FACE = 0x0038,
    ORBIS_SYSMODULE_SMART = 0x0039,
    ORBIS_SYSMODULE_JSON = 0x0080,
    ORBIS_SYSMODULE_GAME_LIVE_STREAMING = 0x0081, // libSceGameLiveStreaming.sprx
    ORBIS_SYSMODULE_COMPANION_UTIL = 0x0082, // libSceCompanionUtil.sprx
    ORBIS_SYSMODULE_PLAYGO = 0x0083, // libScePlayGo.sprx
    ORBIS_SYSMODULE_FONT = 0x0084, // libSceFont.sprx
    ORBIS_SYSMODULE_VIDEO_RECORDING = 0x0085, // libSceVideoRecording.sprx
    ORBIS_SYSMODULE_S3DCONVERSION = 0x0086,
    ORBIS_SYSMODULE_AUDIODEC = 0x0088, // libSceAudiodec.sprx
    ORBIS_SYSMODULE_JPEG_DEC = 0x008A, // libSceJpegDec.sprx
    ORBIS_SYSMODULE_JPEG_ENC = 0x008B, // libSceJpegEnc.sprx
    ORBIS_SYSMODULE_PNG_DEC = 0x008C, // libScePngDec.sprx
    ORBIS_SYSMODULE_PNG_ENC = 0x008D, // libScePngEnc.sprx
    ORBIS_SYSMODULE_VIDEODEC = 0x008E, // libSceVideodec.sprx
    ORBIS_SYSMODULE_MOVE = 0x008F, // libSceMove.sprx
    ORBIS_SYSMODULE_PAD_TRACKER = 0x0091, // libScePadTracker.sprx
    ORBIS_SYSMODULE_DEPTH = 0x0092, // libSceDepth.sprx
    ORBIS_SYSMODULE_HAND = 0x0093,
    ORBIS_SYSMODULE_LIBIME = 0x0095, // libSceIme.sprx
    ORBIS_SYSMODULE_IME_DIALOG = 0x0096, // libSceImeDialog.sprx
    ORBIS_SYSMODULE_NP_PARTY = 0x0097, // libSceNpParty.sprx
    ORBIS_SYSMODULE_FONT_FT = 0x0098, // libSceFontFt.sprx
    ORBIS_SYSMODULE_FREETYPE_OT = 0x0099, // libSceFreeTypeOt.sprx
    ORBIS_SYSMODULE_FREETYPE_OL = 0x009A, // libSceFreeTypeOl.sprx
    ORBIS_SYSMODULE_FREETYPE_OPT_OL = 0x009B, // libSceFreeTypeOptOl.sprx
    ORBIS_SYSMODULE_SCREEN_SHOT = 0x009C, // libSceScreenShot.sprx
    ORBIS_SYSMODULE_NP_AUTH = 0x009D, // libSceNpAuth.sprx
    ORBIS_SYSMODULE_SULPHA = 0x009F,
    ORBIS_SYSMODULE_SAVE_DATA_DIALOG = 0x00A0, // libSceSaveDataDialog.sprx
    ORBIS_SYSMODULE_INVITATION_DIALOG = 0x00A2, // libSceInvitationDialog.sprx
    ORBIS_SYSMODULE_DEBUG_KEYBOARD = 0x00A3,
    ORBIS_SYSMODULE_MESSAGE_DIALOG = 0x00A4, // libSceMsgDialog.sprx
    ORBIS_SYSMODULE_AV_PLAYER = 0x00A5, // libSceAvPlayer.sprx
    ORBIS_SYSMODULE_CONTENT_EXPORT = 0x00A6, // libSceContentExport.sprx
    ORBIS_SYSMODULE_AUDIO_3D = 0x00A7, // libSceAudio3d.sprx
    ORBIS_SYSMODULE_NP_COMMERCE = 0x00A8, // libSceNpCommerce.sprx
    ORBIS_SYSMODULE_MOUSE = 0x00A9, // libSceMouse.sprx
    ORBIS_SYSMODULE_COMPANION_HTTPD = 0x00AA, // libSceCompanionHttpd.sprx
    ORBIS_SYSMODULE_WEB_BROWSER_DIALOG = 0x00AB, // libSceWebBrowserDialog.sprx
    ORBIS_SYSMODULE_ERROR_DIALOG = 0x00AC, // libSceErrorDialog.sprx
    ORBIS_SYSMODULE_NP_TROPHY = 0x00AD, // libSceNpTrophy.sprx
    ORBIS_SYSMODULE_VIDEO_CORE_IF = 0x00AE, // libSceVideoCoreInterface.sprx
    ORBIS_SYSMODULE_VIDEO_CORE_SERVER_IF = 0x00AF, // libSceVideoCoreServerInterface.sprx
    ORBIS_SYSMODULE_NP_SNS_FACEBOOK = 0x00B0, // libSceNpSnsFacebookDialog.sprx
    ORBIS_SYSMODULE_MOVE_TRACKER = 0x00B1, // libSceMoveTracker.sprx
    ORBIS_SYSMODULE_NP_PROFILE_DIALOG = 0x00B2, // libSceNpProfileDialog.sprx
    ORBIS_SYSMODULE_NP_FRIEND_LIST_DIALOG = 0x00B3, // libSceNpFriendListDialog.sprx
    ORBIS_SYSMODULE_APP_CONTENT = 0x00B4, // libSceAppContent.sprx
    ORBIS_SYSMODULE_NP_SIGNALING = 0x00B5, // libSceNpSignaling.sprx
    ORBIS_SYSMODULE_REMOTE_PLAY = 0x00B6, // libSceRemoteplay.sprx
    ORBIS_SYSMODULE_USBD = 0x00B7, // libSceUsbd.sprx
    ORBIS_SYSMODULE_GAME_CUSTOM_DATA_DIALOG = 0x00B8, // libSceGameCustomDataDialog.sprx
    ORBIS_SYSMODULE_NP_EULA_DIALOG = 0x00B9, // libSceNpEulaDialog.sprx
    ORBIS_SYSMODULE_RANDOM = 0x00BA, // libSceRandom.sprx
    ORBIS_SYSMODULE_RESERVED2 = 0x00BB,
    ORBIS_SYSMODULE_M4AAC_ENC = 0x00BC, // libSceM4aacEnc.sprx
    ORBIS_SYSMODULE_AUDIODEC_CPU = 0x00BD, // libSceAudiodecCpu.sprx
    ORBIS_SYSMODULE_AUDIODEC_CPU_DDP = 0x00BE, // libSceAudiodecCpuDdp.sprx
    ORBIS_SYSMODULE_AUDIODEC_CPU_M4AAC = 0x00C0, // libSceAudiodecCpuM4aac.sprx
    ORBIS_SYSMODULE_BEMP2_SYS = 0x00C1, // libSceBemp2sys.sprx
    ORBIS_SYSMODULE_BEISOBMF = 0x00C2, // libSceBeisobmf.sprx
    ORBIS_SYSMODULE_PLAY_READY = 0x00C3, // libScePlayReady.sprx
    ORBIS_SYSMODULE_VIDEO_NATIVE_EXT_ESSENTIAL = 0x00C4, // libSceVideoNativeExtEssential.sprx
    ORBIS_SYSMODULE_ZLIB = 0x00C5, // libSceZlib.sprx
    ORBIS_SYSMODULE_DTCP_IP = 0x00C6, // libSceDtcpIp.sprx
    ORBIS_SYSMODULE_CONTENT_SEARCH = 0x00C7, // libSceContentSearch.sprx
    ORBIS_SYSMODULE_SHARE_UTILITY = 0x00C8, // libSceShareUtility.sprx
    ORBIS_SYSMODULE_AUDIODEC_CPU_DTS_HD_LBR = 0x00C9, // libSceAudiodecCpuDtsHdLbr.sprx
    ORBIS_SYSMODULE_DECI4H = 0x00CA,
    ORBIS_SYSMODULE_HEAD_TRACKER = 0x00CB,
    ORBIS_SYSMODULE_GAME_UPDATE = 0x00CC, // libSceGameUpdate.sprx
    ORBIS_SYSMODULE_AUTO_MOUNTER_CLIENT = 0x00CD, // libSceAutoMounterClient.sprx
    ORBIS_SYSMODULE_SYSTEM_GESTURE = 0x00CE, // libSceSystemGesture.sprx
    ORBIS_SYSMODULE_VIDEODEC2 = 0x00CF,
    ORBIS_SYSMODULE_VDECWRAP = 0x00D0, // libSceVdecwrap.sprx
    ORBIS_SYSMODULE_AT9_ENC = 0x00D1, // libSceAt9Enc.sprx
    ORBIS_SYSMODULE_CONVERT_KEYCODE = 0x00D2, // libSceConvertKeycode.sprx
    ORBIS_SYSMODULE_SHARE_PLAY = 0x00D3, // libSceSharePlay.sprx
    ORBIS_SYSMODULE_HMD = 0x00D4, // libSceHmd.sprx
    ORBIS_SYSMODULE_USB_STORAGE = 0x00D5, // libSceUsbStorage.sprx
    ORBIS_SYSMODULE_USB_STORAGE_DIALOG = 0x00D6, // libSceUsbStorageDialog.sprx
    ORBIS_SYSMODULE_DISC_MAP = 0x00D7, // libSceDiscMap.sprx
    ORBIS_SYSMODULE_FACE_TRACKER = 0x00D8,
    ORBIS_SYSMODULE_HAND_TRACKER = 0x00D9,
    ORBIS_SYSMODULE_NP_SNS_YOUTUBE_DIALOG = 0x00DA, // libSceNpSnsYouTubeDialog.sprx
    ORBIS_SYSMODULE_PROFILE_CACHE_EXTERNAL = 0x00DC, // libSceProfileCacheExternal.sprx
    ORBIS_SYSMODULE_MUSIC_PLAYER_SERVICE = 0x00DD, // libSceMusicPlayerService.sprx
    ORBIS_SYSMODULE_SP_SYS_CALL_WRAPPER = 0x00DE, // libSceSpSysCallWrapper.sprx
    ORBIS_SYSMODULE_PS2_EMU_MENU_DIALOG = 0x00DF, // libScePs2EmuMenuDialog.sprx
    ORBIS_SYSMODULE_NP_SNS_DIALYMOTION_DIALOG = 0x00E0, // libSceNpSnsDailyMotionDialog.sprx
    ORBIS_SYSMODULE_AUDIODEC_CPU_HEVAG = 0x00E1, // libSceAudiodecCpuHevag.sprx
    ORBIS_SYSMODULE_LOGIN_DIALOG = 0x00E2, // libSceLoginDialog.sprx
    ORBIS_SYSMODULE_LOGIN_SERVICE = 0x00E3, // libSceLoginService.sprx
    ORBIS_SYSMODULE_SIGNIN_DIALOG = 0x00E4, // libSceSigninDialog.sprx
    ORBIS_SYSMODULE_VDECSW = 0x00E5, // libSceVdecsw.sprx
    ORBIS_SYSMODULE_CUSTOM_MUSIC_CORE = 0x00E6, // libSceCustomMusicCore.sprx
    ORBIS_SYSMODULE_JSON2 = 0x00E7, // libSceJson2.sprx
    ORBIS_SYSMODULE_AUDIO_LATENCY_ESTIMATION = 0x00E8,
    ORBIS_SYSMODULE_WK_FONT_CONFIG = 0x00E9, // libSceWkFontConfig.sprx
    ORBIS_SYSMODULE_RESERVED27 = 0x00EA,
    ORBIS_SYSMODULE_HMD_SETUP_DIALOG = 0x00EB, // libSceHmdSetupDialog.sprx
    ORBIS_SYSMODULE_RESERVED28 = 0x00EC,
    ORBIS_SYSMODULE_VR_TRACKER = 0x00ED, // libSceVrTracker.sprx
    ORBIS_SYSMODULE_CONTENT_DELETE = 0x00EE, // libSceContentDelete.sprx
    ORBIS_SYSMODULE_IME_BACKEND = 0x00EF, // libSceImeBackend.sprx
    ORBIS_SYSMODULE_NET_CTL_AP_DIALOG = 0x00F0, // libSceNetCtlApDialog.sprx
    ORBIS_SYSMODULE_PLAYGO_DIALOG = 0x00F1, // libScePlayGoDialog.sprx
    ORBIS_SYSMODULE_SOCIAL_SCREEN = 0x00F2, // libSceSocialScreen.sprx
    ORBIS_SYSMODULE_EDIT_MP4 = 0x00F3, // libSceEditMp4.sprx
    ORBIS_SYSMODULE_PSM_KIT_SYSTEM = 0x00F5, // libScePsmKitSystem.sprx
    ORBIS_SYSMODULE_TEXT_TO_SPEECH = 0x00F6, // libSceTextToSpeech.sprx
    ORBIS_SYSMODULE_NP_TOOLKIT = 0x00F7,
    ORBIS_SYSMODULE_CUSTOM_MUSIC_SERVICE = 0x00F8, // libSceCustomMusicService.sprx
    ORBIS_SYSMODULE_CL_SYS_CALL_WRAPPER = 0x00F9, // libSceClSysCallWrapper.sprx
    ORBIS_SYSMODULE_SYSTEM_LOGGER = 0x00FA, // libSceSystemLogger.sprx
    ORBIS_SYSMODULE_BLUETOOTH_HID = 0x00FB, // libSceBluetoothHid.sprx
    ORBIS_SYSMODULE_VIDEO_DECODER_ARBITRATION = 0x00FC, // libSceVideoDecoderArbitration.sprx
    ORBIS_SYSMODULE_VR_SERVICE_DIALOG = 0x00FD, // libSceVrServiceDialog.sprx
    ORBIS_SYSMODULE_JOB_MANAGER = 0x00FE,
    ORBIS_SYSMODULE_KEYBOARD = 0x0106,
} OrbisSysModule;
#ifdef __cplusplus 
extern "C" {
#endif
 
/**
  * Sysmodule function to test if a module is loaded or not.
  * @param id is the internal module id. see list of module ids.
  * @return 0 = Success/Valid or Negatives values are error codes.
  */
int32_t sceSysmoduleIsLoaded(enum OrbisSysModule moduleId);

/**
  * Sysmodule function to load a module.
  * @param id is the internal module id. see list of module ids.
  * @return 0 = Success/Valid or Negatives values are error codes.
  */
int32_t sceSysmoduleLoadModule(enum OrbisSysModule moduleId);

/**
  * Sysmodule function to unload a module.
  * @param id is the internal module id. see list of module ids.
  * @return 0 = Success/Valid or Negatives values are error codes.
  */
int32_t sceSysmoduleUnloadModule(enum OrbisSysModule moduleId);
 
// Undocumented Function... Need to reverse
void sceSysmoduleGetModuleHandleInternal();
// Undocumented Function... Need to reverse
void sceSysmoduleIsLoadedInternal();
// Undocumented Function... Need to reverse
void sceSysmoduleLoadModuleByNameInternal();
// Undocumented Function... Need to reverse
uint32_t sceSysmoduleLoadModuleInternal(enum OrbisSysModuleInternal moduleId);
// Undocumented Function... Need to reverse
void sceSysmoduleLoadModuleInternalWithArg();
// Undocumented Function... Need to reverse
void sceSysmodulePreloadModuleForLibkernel();
// Undocumented Function... Need to reverse
void sceSysmoduleUnloadModuleByNameInternal();
// Undocumented Function... Need to reverse
int32_t sceSysmoduleUnloadModuleInternal(enum OrbisSysModuleInternal moduleId);
// Undocumented Function... Need to reverse
void sceSysmoduleUnloadModuleInternalWithArg();

#ifdef __cplusplus
}
#endif
#ifndef RETRO_USE_MOD_LOADER
#define RETRO_USE_MOD_LOADER (!RETRO_USE_ORIGINAL_CODE && 1)
#endif

#ifndef RETRO_USE_NETWORKING
#define RETRO_USE_NETWORKING (!RETRO_USE_ORIGINAL_CODE && 1)
#endif

// Forces all DLC flags to be disabled, this should be enabled in any public releases
#ifndef RSDK_AUTOBUILD
#define RSDK_AUTOBUILD (0)
#endif

// ================
// STANDARD LIBS
// ================
#include <stdio.h>
#include <string.h>
#include <cmath>
#if RETRO_USE_MOD_LOADER
#include <regex>
#endif

// ================
// STANDARD TYPES
// ================
typedef unsigned char byte;
typedef signed char sbyte;
typedef unsigned short ushort;
typedef unsigned int uint;
// typedef unsigned long long ulong;

// Platforms (RSDKv4 only defines these 7 (I assume), but feel free to add your own custom platform define for easier platform code changes)
#define RETRO_WIN      (0)
#define RETRO_OSX      (1)
#define RETRO_XBOX_360 (2)
#define RETRO_PS3      (3)
#define RETRO_iOS      (4)
#define RETRO_ANDROID  (5)
#define RETRO_WP7      (6)
// Custom Platforms start here
#define RETRO_UWP   (7)
#define RETRO_LINUX (8)
#define RETRO_PS4   (9)
// Platform types (Game manages platform-specific code such as HUD position using this rather than the above)
#define RETRO_STANDARD (0)
#define RETRO_MOBILE   (1)

#if defined(RETRO_PS4_BUILD)
#define RETRO_PLATFORM   (RETRO_PS4)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#elif defined _WIN32

#if defined WINAPI_FAMILY
#if WINAPI_FAMILY != WINAPI_FAMILY_APP
#define RETRO_PLATFORM   (RETRO_WIN)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#else
#include <WInRTIncludes.hpp>

#define RETRO_PLATFORM   (RETRO_UWP)
#define RETRO_DEVICETYPE (UAP_GetRetroGamePlatform())
#endif
#else
#define RETRO_PLATFORM   (RETRO_WIN)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#endif

#elif defined __APPLE__
#if __IPHONEOS__
#define RETRO_PLATFORM   (RETRO_iOS)
#define RETRO_DEVICETYPE (RETRO_MOBILE)
#else
#define RETRO_PLATFORM   (RETRO_OSX)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#endif
#elif defined __ANDROID__
#define RETRO_PLATFORM   (RETRO_ANDROID)
#define RETRO_DEVICETYPE (RETRO_MOBILE)
#include <jni.h>
#elif defined(__linux__)
#define RETRO_PLATFORM   (RETRO_LINUX)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#else
//#error "No Platform was defined"
#define RETRO_PLATFORM   (RETRO_LINUX)
#define RETRO_DEVICETYPE (RETRO_STANDARD)
#endif

#define DEFAULT_SCREEN_XSIZE 424
#define DEFAULT_FULLSCREEN   false
#define RETRO_USING_MOUSE
#define RETRO_USING_TOUCH

#ifndef BASE_PATH
#define BASE_PATH "/data/RSDKv4/"
#define MOD_PATH "/data/RSDKv4/"
#endif

#if !defined(RETRO_USE_SDL2) && !defined(RETRO_USE_SDL1)
#define RETRO_USE_SDL2 (1)
#endif

#if RETRO_PLATFORM == RETRO_WIN || RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_LINUX || RETRO_PLATFORM == RETRO_UWP                       \
    || RETRO_PLATFORM == RETRO_ANDROID || RETRO_PLATFORM == RETRO_PS4
#ifdef RETRO_USE_SDL2
#define RETRO_USING_SDL1 (0)
#define RETRO_USING_SDL2 (1)
#elif defined(RETRO_USE_SDL1)
#define RETRO_USING_SDL1 (1)
#define RETRO_USING_SDL2 (0)
#endif
#else // Since its an else & not an elif these platforms probably aren't supported yet
#define RETRO_USING_SDL1 (0)
#define RETRO_USING_SDL2 (0)
#endif

#if RETRO_PLATFORM == RETRO_iOS || RETRO_PLATFORM == RETRO_ANDROID || RETRO_PLATFORM == RETRO_WP7
#define RETRO_GAMEPLATFORM (RETRO_MOBILE)
#elif RETRO_PLATFORM == RETRO_UWP
#define RETRO_GAMEPLATFORM (UAP_GetRetroGamePlatform())
#else
#define RETRO_GAMEPLATFORM (RETRO_STANDARD)
#endif

#define RETRO_SW_RENDER (0)
#define RETRO_HW_RENDER (1)

#if RETRO_PLATFORM == RETRO_PS4
#ifdef RETRO_RENDERTYPE
#undef RETRO_RENDERTYPE
#endif
#define RETRO_RENDERTYPE (RETRO_SW_RENDER)
#elif defined(USE_SW_REN)
#define RETRO_RENDERTYPE (RETRO_SW_RENDER)
#elif defined(USE_HW_REN)
#define RETRO_RENDERTYPE (RETRO_HW_RENDER)
#elif !defined(RETRO_RENDERTYPE)
#define RETRO_RENDERTYPE (RETRO_SW_RENDER)
#endif

#if RETRO_PLATFORM == RETRO_PS4
#ifdef RETRO_USING_OPENGL
#undef RETRO_USING_OPENGL
#endif
#define RETRO_USING_OPENGL (1)
#elif !defined(RETRO_USING_OPENGL)
#define RETRO_USING_OPENGL (1)
#endif

#define RETRO_SOFTWARE_RENDER (RETRO_RENDERTYPE == RETRO_SW_RENDER)
//#define RETRO_HARDWARE_RENDER (RETRO_RENDERTYPE == RETRO_HW_RENDER)

#if RETRO_USING_OPENGL
#if RETRO_PLATFORM == RETRO_ANDROID
#define GL_GLEXT_PROTOTYPES

#include <GLES/gl.h>
#include <GLES/glext.h>

#undef glGenFramebuffers
#undef glBindFramebuffer
#undef glFramebufferTexture2D
#undef glDeleteFramebuffers

#undef GL_FRAMEBUFFER
#undef GL_COLOR_ATTACHMENT0
#undef GL_FRAMEBUFFER_BINDING

#define glGenFramebuffers      glGenFramebuffersOES
#define glBindFramebuffer      glBindFramebufferOES
#define glFramebufferTexture2D glFramebufferTexture2DOES
#define glDeleteFramebuffers   glDeleteFramebuffersOES

#define GL_FRAMEBUFFER         GL_FRAMEBUFFER_OES
#define GL_COLOR_ATTACHMENT0   GL_COLOR_ATTACHMENT0_OES
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_OES
#elif RETRO_PLATFORM == RETRO_OSX
#define GL_GLEXT_PROTOTYPES
#define GL_SILENCE_DEPRECATION

#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#undef glGenFramebuffers
#undef glBindFramebuffer
#undef glFramebufferTexture2D
#undef glDeleteFramebuffers

#undef GL_FRAMEBUFFER
#undef GL_COLOR_ATTACHMENT0
#undef GL_FRAMEBUFFER_BINDING

#define glGenFramebuffers      glGenFramebuffersEXT
#define glBindFramebuffer      glBindFramebufferEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glDeleteFramebuffers   glDeleteFramebuffersEXT

#define GL_FRAMEBUFFER         GL_FRAMEBUFFER_EXT
#define GL_COLOR_ATTACHMENT0   GL_COLOR_ATTACHMENT0_EXT
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_EXT
#elif RETRO_PLATFORM == RETRO_PS4
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#endif
#endif

#ifndef RETRO_USE_HAPTICS
#if RETRO_PLATFORM == RETRO_PS4
#define RETRO_USE_HAPTICS (0)
#else
#define RETRO_USE_HAPTICS (1)
#endif
#endif

// NOTE: This is only used for rev00 stuff, it was removed in rev01 and later builds
#if RETRO_PLATFORM <= RETRO_WP7
#define RETRO_GAMEPLATFORMID (RETRO_PLATFORM)
#else

// use *this* macro to determine what platform the game thinks its running on (since only the first 7 platforms are supported natively by scripts)
#if RETRO_PLATFORM == RETRO_LINUX
#define RETRO_GAMEPLATFORMID (RETRO_WIN)
#elif RETRO_PLATFORM == RETRO_UWP
#define RETRO_GAMEPLATFORMID (UAP_GetRetroGamePlatformId())
#elif RETRO_PLATFORM == RETRO_PS4
#define RETRO_GAMEPLATFORMID (RETRO_WIN)
#else
#error Unspecified RETRO_GAMEPLATFORMID
#endif

#endif

// Determines which revision to use (see defines below for specifics). Datafiles from REV00 and REV01 builds will not work on later revisions and vice versa.
#ifndef RSDK_REVISION
#define RSDK_REVISION (3)
#endif

// Revision from early versions of Sonic 1
#define RETRO_REV00 (RSDK_REVISION == 0)

// Revision from early versions of Sonic 2
#define RETRO_REV01 (RSDK_REVISION >= 1)

// Revision from the S3&K POC, this is also used in the Sega Forever versions of S1 & S2
#define RETRO_REV02 (RSDK_REVISION >= 2)

// Revision included as part of RSDKv5U (Sonic Origins)
#define RETRO_REV03 (RSDK_REVISION >= 3)

enum RetroLanguages {
    RETRO_EN = 0,
    RETRO_FR = 1,
    RETRO_IT = 2,
    RETRO_DE = 3,
    RETRO_ES = 4,
    RETRO_JP = 5,
    RETRO_PT = 6,
    RETRO_RU = 7,
    RETRO_KO = 8,
    RETRO_ZH = 9,
    RETRO_ZS = 10,
};

#if RETRO_REV00
enum RetroEngineMessages {
    MESSAGE_NONE      = 0,
    MESSAGE_MESSAGE_1 = 1,
    MESSAGE_LOSTFOCUS = 2,
    MESSAGE_MESSAGE_3 = 3,
    MESSAGE_MESSAGE_4 = 4,
};
#endif

enum RetroStates {
    ENGINE_DEVMENU     = 0,
    ENGINE_MAINGAME    = 1,
    ENGINE_INITDEVMENU = 2,
    ENGINE_WAIT        = 3,
    ENGINE_SCRIPTERROR = 4,
    ENGINE_INITPAUSE   = 5,
    ENGINE_EXITPAUSE   = 6,
    ENGINE_ENDGAME     = 7,
    ENGINE_RESETGAME   = 8,

#if !RETRO_USE_ORIGINAL_CODE && RETRO_USE_NETWORKING
    // Custom GameModes (required to make some features work)
    ENGINE_CONNECT2PVS = 0x80,
    ENGINE_WAIT2PVS    = 0x81,
#endif
#if RETRO_USE_MOD_LOADER
    ENGINE_INITMODMENU = 0x82,
#endif
};

enum RetroGameType {
    GAME_UNKNOWN = 0,
    GAME_SONIC1  = 1,
    GAME_SONIC2  = 2,
};

// General Defines
#define SCREEN_YSIZE   (240)
#define SCREEN_CENTERY (SCREEN_YSIZE / 2)

#if RETRO_PLATFORM == RETRO_WIN || RETRO_PLATFORM == RETRO_UWP || RETRO_PLATFORM == RETRO_ANDROID || RETRO_PLATFORM == RETRO_LINUX
#if RETRO_USING_SDL2
#include <SDL2/SDL.h>
#elif RETRO_USING_SDL1
#include <SDL.h>
#endif
#include <vorbis/vorbis/vorbisfile.h>
#elif RETRO_PLATFORM == RETRO_OSX
#include <SDL2/SDL.h>
#include <Vorbis/vorbisfile.h>

#include "cocoaHelpers.hpp"

#elif RETRO_USING_SDL2
#include <SDL2/SDL.h>
#include <vorbis/vorbis/vorbisfile.h>
#else

#endif

#if !RETRO_USE_ORIGINAL_CODE
extern bool usingCWD;
extern bool engineDebugMode;
#endif

// Utils
#if !RETRO_USE_ORIGINAL_CODE
#include "Ini.hpp"
#endif

#include "Math.hpp"
#include "Reader.hpp"
#include "String.hpp"
#include "Animation.hpp"
#include <Audio.hpp>
#include "Input.hpp"
#include "Object.hpp"
#include "Palette.hpp"
#include "Drawing.hpp"
#include "Scene3D.hpp"
#include "Collision.hpp"
#include "Scene.hpp"
#include "Script.hpp"
#include "Sprite.hpp"
#include "Text.hpp"
#include "Networking.hpp"
#include "Renderer.hpp"
#include "Userdata.hpp"
#include "Debug.hpp"
#include "ModAPI.hpp"

// Native Entities
#include "NativeObjects.hpp"

class RetroEngine
{
public:
    RetroEngine()
    {
        if (RETRO_GAMEPLATFORM == RETRO_STANDARD) {
            gamePlatform   = "STANDARD";
            gameDeviceType = RETRO_STANDARD;
        }
        else {
            gamePlatform   = "MOBILE";
            gameDeviceType = RETRO_MOBILE;
        }
    }

#if !RETRO_USE_ORIGINAL_CODE
    bool usingDataFile_Config = false;
#endif
    bool usingDataFile = false;
    bool usingBytecode = false;
#if RETRO_REV03 && !RETRO_USE_ORIGINAL_CODE
    bool usingOrigins = false;
#endif

    char dataFile[RETRO_PACKFILE_COUNT][0x80];

    bool initialised = false;
    bool running     = false;
    double deltaTime = 0;

    int gameMode = ENGINE_MAINGAME;
    int language = RETRO_EN;
#if RETRO_REV00
    int message = 0;
#endif
    int gameDeviceType    = RETRO_STANDARD;
    int globalBoxRegion   = REGION_JP;
    bool nativeMenuFadeIn = false;

    bool trialMode        = false;
    bool onlineActive     = true;
    bool useHighResAssets = false;
#if RETRO_USE_HAPTICS
    bool hapticsEnabled = true;
#endif

    int frameSkipSetting = 0;
    int frameSkipTimer   = 0;

#if !RETRO_USE_ORIGINAL_CODE
    // Ported from RSDKv5
    int startList_Game  = -1;
    int startStage_Game = -1;

    bool consoleEnabled  = false;
    bool devMenu         = false;
    int startList        = -1;
    int startStage       = -1;
    int startPlayer      = -1;
    int startSave        = -1;
    int gameSpeed        = 1;
    int fastForwardSpeed = 8;
    bool masterPaused    = false;
    bool frameStep       = false;
    int dimTimer         = 0;
    int dimLimit         = 0;
    float dimPercent     = 1.0;
    float dimMax         = 1.0;

    char startSceneFolder[0x10];
    char startSceneID[0x10];

    bool showPaletteOverlay = false;
    bool useHQModes         = true;

    bool hasFocus  = true;
    int focusState = 0;
#endif

    void Init();
    void Run();

    bool LoadGameConfig(const char *filepath);
#if RETRO_USE_MOD_LOADER
    void LoadXMLWindowText();
    void LoadXMLVariables();
    void LoadXMLPalettes();
    void LoadXMLObjects();
    void LoadXMLSoundFX();
    void LoadXMLPlayers(TextMenu *menu);
    void LoadXMLStages(TextMenu *menu, int listNo);
#endif

    char gameWindowText[0x40];
    char gameDescriptionText[0x100];
#ifdef DECOMP_VERSION
    const char *gameVersion = DECOMP_VERSION;
#else
    const char *gameVersion  = "1.3.3";
#endif
    const char *gamePlatform = nullptr;

    int gameTypeID       = 0;
    const char *releaseType = "USE_STANDALONE";

#if RETRO_RENDERTYPE == RETRO_SW_RENDER
    const char *gameRenderType = "SW_RENDERING";
#elif RETRO_RENDERTYPE == RETRO_HW_RENDER
    const char *gameRenderType = "HW_RENDERING";
#endif

#if RETRO_USE_HAPTICS
    const char *gameHapticSetting = "USE_F_FEEDBACK"; // None is default, but people with controllers exist
#else
    const char *gameHapticSetting = "NO_F_FEEDBACK";
#endif

#if !RETRO_USE_ORIGINAL_CODE
    byte gameType = GAME_UNKNOWN;
#if RETRO_USE_MOD_LOADER
    bool modMenuCalled = false;
    bool forceSonic1   = false;
#endif
#endif

    ushort *frameBuffer   = nullptr;
#if RETRO_SOFTWARE_RENDER
    ushort *frameBuffer2x = nullptr;
#endif
    uint *texBuffer = nullptr;

#if !RETRO_USE_ORIGINAL_CODE
    bool isFullScreen = false;

    bool startFullScreen  = false; // if should start as fullscreen
    bool borderless       = false;
    bool vsync            = false;
    int scalingMode       = 0;
    int windowScale       = 2;
    int refreshRate       = 60; // user-picked screen update rate
    int screenRefreshRate = 60; // hardware screen update rate
    int targetRefreshRate = 60; // game logic update rate

    int renderFrameIndex = 0;
    int skipFrameIndex   = 0;

    int windowXSize; // width of window/screen in the previous frame
    int windowYSize; // height of window/screen in the previous frame
#endif

#if !RETRO_USE_ORIGINAL_CODE
#if RETRO_USING_SDL2
    SDL_Window *window = nullptr;
#if !RETRO_USING_OPENGL
    SDL_Renderer *renderer = nullptr;
#if RETRO_SOFTWARE_RENDER
    SDL_Texture *screenBuffer   = nullptr;
    SDL_Texture *screenBuffer2x = nullptr;
#endif // RETRO_SOFTWARE_RENDERER
#endif

    SDL_Event sdlEvents;

#if RETRO_USING_OPENGL
    SDL_GLContext glContext; // OpenGL context
#endif // RETRO_USING_OPENGL
#endif // RETRO_USING_SDL2

#if RETRO_USING_SDL1
    SDL_Surface *windowSurface = nullptr;

    SDL_Surface *screenBuffer   = nullptr;
    SDL_Surface *screenBuffer2x = nullptr;

    SDL_Event sdlEvents;
#endif // RETRO_USING_SDL1
#endif //! RETRO_USE_ORIGINAL_CODE
};

extern RetroEngine Engine;
#endif // !RETROENGINE_H
