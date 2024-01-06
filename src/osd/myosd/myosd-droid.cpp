// license:BSD-3-Clause
//============================================================
//
//  myosd-droid.cpp - Implementation of osd droid stuff
//
//  MAME4DROID  by David Valdeita (Seleuco)
//
//============================================================

#include "myosd-droid.h"
#include "opensl_snd.h"

#include <unistd.h>
#include <fcntl.h>

#include <android/log.h>
#include <android/keycodes.h>

#include <pthread.h>

#include <sys/stat.h>

#include <string>
#include <chrono>

#include "myosd_core.h"

#include "../../../android-MAME4droid/app/src/main/jni/com_seleuco_mame4droid_Emulator.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)<(b) ? (b) : (a))

static int lib_inited = 0;

static int myosd_droid_inMenu = 1;
static int myosd_droid_inGame = 0;
static int myosd_droid_running = 0;

//video
static int myosd_droid_video_width = 1;
static int myosd_droid_video_height = 1;
static int myosd_droid_resolution = 1;
static int myosd_droid_resolution_osd = 1;
static int myosd_droid_res_width = 1;
static int myosd_droid_res_height = 1;
static int myosd_droid_res_width_osd = 1;
static int myosd_droid_res_height_osd = 1;
static int myosd_droid_res_width_native = 1;
static int myosd_droid_res_height_native = 1;
static int myosd_droid_dbl_buffer = 1;

static unsigned char *screenbuffer1 = nullptr;
static unsigned char *screenbuffer2 = nullptr;

//input - save
static int myosd_droid_num_buttons = 0;
static int myosd_droid_light_gun = 0;;
static int myosd_droid_mouse = 0;
static int myosd_droid_num_of_joys = 1;
static int myosd_droid_num_ways = 8;
static int myosd_droid_pxasp1 = 0;
//static int myosd_droid_service = 0;
static int myosd_droid_exitGame = 0;
static int myosd_droid_mouse_enable = 1;
static int myosd_droid_keyboard_enable = 1;

//input data
static unsigned long joy_status[MYOSD_NUM_JOY];

static float joy_analog_x[MYOSD_NUM_JOY];
static float joy_analog_y[MYOSD_NUM_JOY];
static float joy_analog_trigger_x[MYOSD_NUM_JOY];
static float joy_analog_trigger_y[MYOSD_NUM_JOY];

static float lightgun_x[MYOSD_NUM_GUN];
static float lightgun_y[MYOSD_NUM_GUN];

static float mouse_x[MYOSD_NUM_MICE];
static float mouse_y[MYOSD_NUM_MICE];
static int mouse_status[MYOSD_NUM_MICE];
static float cur_x_mouse = 0;
static float cur_y_mouse = 0;

#define ANDROID_NUM_KEYS 256
static uint8_t keyboard[MYOSD_NUM_KEYS];
static int android_to_mame_key[ANDROID_NUM_KEYS];

static pthread_mutex_t mouse_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pause_mutex = PTHREAD_MUTEX_INITIALIZER;

//saveload
static int myosd_droid_savestate = 0;
static int myosd_droid_loadstate = 0;
static int myosd_droid_autosave = 0;
static std::string myosd_droid_statepath;

//misc
static int myosd_droid_warn_on_exit = 1;
static int myosd_droid_show_fps = 1;
static int myosd_droid_zoom_to_window = 1;
static std::string myosd_droid_selected_game;
static std::string myosd_droid_rom_name;
static std::string myosd_droid_overlay_effect;
static int myosd_droid_auto_frameskip = 0;
static int myosd_droid_cheats = 0;
static int myosd_droid_skip_gameinfo = 0;
static int myosd_droid_disable_drc = 1;
static int myosd_droid_enable_drc_use_c = 1;
static int myosd_droid_simple_ui = 0;
static int myosd_droid_prev_pause = 0;
static int myosd_droid_pause = 0;
static int myosd_droid_do_pause = 0;
static int myosd_droid_do_resume = 0;

static int myosd_one_processor = 0;
static int myosd_droid_no_dzsat = 0;

//vector options
static int myosd_droid_vector_beam2x = 1;
static int myosd_droid_vector_flicker = 0;

//SAF
int myosd_droid_using_saf = 0;
//int myosd_droid_reload = 1;
int myosd_droid_savestatesinrompath = 0;
std::string myosd_droid_safpath;

//sound
static int soundInit = 0;
static int sound_engine = 1;
static int myosd_droid_sound_value = 48000;
static int myosd_droid_sound_frames = 1024;
static OPENSL_SND *opensl_snd_ptr  = nullptr;

//Android callbacks
static void (*dumpVideo_callback)(void) = nullptr;

static void (*initVideo_callback)(void *buffer, int width, int height, int pitch) = nullptr;

static void
(*changeVideo_callback)(int newWidth, int newHeight) = nullptr;

static void (*openSound_callback)(int rate, int stereo) = nullptr;

static void (*dumpSound_callback)(void *buffer, int size) = nullptr;

static void (*closeSound_callback)(void) = nullptr;

static void (*initInput_callback)(void) = nullptr;

static int (*safOpenFile_callback)(const char *pathName,const char *mode) = nullptr;

static int (*safReadDir_callback)(const char *dirName, int reload) = nullptr;

static char *(*safGetNextDirEntry_callback)(int dirId) = nullptr;

static void (*safCloseDir_callback)(int dirId) = nullptr;
//end android callbacks

#define PIXEL_PITCH  4

void myosd_droid_setVideoCallbacks(
        void (*init_video_java)(void *, int, int, int),
        void (*dump_video_java)(void),
        void (*change_video_java)(int, int)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setVideoCallbacks");

    initVideo_callback = init_video_java;
    dumpVideo_callback = dump_video_java;
    changeVideo_callback = change_video_java;
}

void myosd_droid_setAudioCallbacks(
        void (*open_sound_java)(int, int),
        void (*dump_sound_java)(void *, int),
        void (*close_sound_java)(void)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setAudioCallbacks");

    openSound_callback = open_sound_java;
    dumpSound_callback = dump_sound_java;
    closeSound_callback = close_sound_java;
}

void myosd_droid_setInputCallbacks(
        void (*init_input_java)(void)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setInputCallbacks");

    initInput_callback = init_input_java;
}

void myosd_droid_setSAFCallbacks(
        int (*safOpenFile_java)(const char *, const char *),
        int (*safReadDir_java)(const char *, int reload),
        char *(*safGetNextDirEntry_java)(int),
        void (*safCloseDir_java)(int)
) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setSAFCallbacks");

    safOpenFile_callback = safOpenFile_java;
    safReadDir_callback = safReadDir_java;
    safGetNextDirEntry_callback = safGetNextDirEntry_java;
    safCloseDir_callback = safCloseDir_java;
}


void myosd_droid_initMyOSD(const char *path, int nativeWidth, int nativeHeight) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "initMyOSD path: %s nativeWidth: %d nativeHeight: %d", path,
                        nativeWidth, nativeHeight);

    /*ret = */chdir(path);

    myosd_droid_res_width_native = nativeWidth;
    myosd_droid_res_height_native = nativeHeight;
}

static void droid_pause(int doPause){

    pthread_mutex_lock(&pause_mutex);

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause %d",doPause);
    int isPaused = 0;

    if(myosd_droid_inGame)
      isPaused = (myosd_is_paused() ? 1 : 0);

    if(myosd_droid_pause && doPause) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause already paused...");
    }
    else if(!myosd_droid_pause && !doPause) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause already resumed...");
    }else if (isPaused && doPause ) {
        myosd_droid_prev_pause = 1;
        myosd_droid_pause = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause previous paused %d...",doPause);
    } else if (myosd_droid_prev_pause && !doPause) {
        myosd_droid_prev_pause = 0;
        myosd_droid_pause = 0;
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause previous paused %d...",doPause);
    } else {
        if(myosd_droid_inGame && myosd_droid_running) {

            //keyboard[MYOSD_KEY_PAUSE] = 0x80;

            if(doPause)
            {
                myosd_droid_do_pause = 1;
                while(!myosd_is_paused()){usleep(1);};
                //usleep(60*1000);
            }
            else
            {
                myosd_droid_do_resume = 1;
                while(myosd_is_paused()){usleep(1);};
                //usleep(60*1000);
            }

            //keyboard[MYOSD_KEY_PAUSE] = 0x00;

            //myosd_pause(doPause ? true : false);//this way makes LUA engine sig fault
        }
        myosd_droid_pause = doPause;
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause pausing %d...",doPause);
    }

    pthread_mutex_unlock(&pause_mutex);
}

void myosd_droid_setMyValue(int key, int i, int value) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setMyValue  %d,%d:%d",key,i,value);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_SHOW_FPS:
            myosd_droid_show_fps = value;
            break;
        case com_seleuco_mame4droid_Emulator_ZOOM_TO_WINDOW:
            myosd_droid_zoom_to_window = value;
            break;
        case com_seleuco_mame4droid_Emulator_AUTO_FRAMESKIP:
            myosd_droid_auto_frameskip = value;
            break;
        case com_seleuco_mame4droid_Emulator_CHEATS:
            myosd_droid_cheats = value;
            break;
        case com_seleuco_mame4droid_Emulator_SKIP_GAMEINFO:
            myosd_droid_skip_gameinfo = value;
            break;
        case com_seleuco_mame4droid_Emulator_DISABLE_DRC:
            myosd_droid_disable_drc = value;
            break;
        case com_seleuco_mame4droid_Emulator_DRC_USE_C:
            myosd_droid_enable_drc_use_c = value;
            break;
        case com_seleuco_mame4droid_Emulator_SIMPLE_UI:
            myosd_droid_simple_ui = value;
            break;
        case com_seleuco_mame4droid_Emulator_EXIT_GAME:
            myosd_droid_exitGame = value;
            keyboard[MYOSD_KEY_ESC] = value ? 0x80: 0;
            //keyboard[MYOSD_KEY_UIMODE] = value ? 0x80: 0;
            break;
        case com_seleuco_mame4droid_Emulator_PAUSE: {
            droid_pause(value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_SOUND_VALUE:
            myosd_droid_sound_value = value;
            break;
        case com_seleuco_mame4droid_Emulator_AUTOSAVE:
            myosd_droid_autosave = value;
            break;
        case com_seleuco_mame4droid_Emulator_SAVESTATE:
            keyboard[MYOSD_KEY_LOADSAVE] = value ? 0x80: 0;
            keyboard[MYOSD_KEY_LSHIFT] = value ? 0x80: 0;
            myosd_droid_savestate = value;
            break;
        case com_seleuco_mame4droid_Emulator_LOADSTATE:
            keyboard[MYOSD_KEY_LOADSAVE] = value ? 0x80: 0;
            myosd_droid_loadstate = value;
            break;
        case com_seleuco_mame4droid_Emulator_EMU_RESOLUTION:
            myosd_droid_resolution = value;
            break;
        case com_seleuco_mame4droid_Emulator_OSD_RESOLUTION:
            myosd_droid_resolution_osd = value;
            break;
        case com_seleuco_mame4droid_Emulator_DOUBLE_BUFFER:
            myosd_droid_dbl_buffer = value;
            break;
        case com_seleuco_mame4droid_Emulator_PXASP1:
            myosd_droid_pxasp1 = value;
            break;
        case com_seleuco_mame4droid_Emulator_VBEAM2X:
            myosd_droid_vector_beam2x = value;
            break;
        case com_seleuco_mame4droid_Emulator_VFLICKER:
            myosd_droid_vector_flicker = value;
            break;
        case com_seleuco_mame4droid_Emulator_SOUND_OPTIMAL_FRAMES:
            myosd_droid_sound_frames = value;
            break;
        case com_seleuco_mame4droid_Emulator_SOUND_OPTIMAL_SAMPLERATE: {
            if (myosd_droid_sound_value != -1 && sound_engine == 2) //en el caso de opensl siempre optimal frames
                myosd_droid_sound_value = value;
            break;
        }
        case com_seleuco_mame4droid_Emulator_SOUND_ENGINE:
            sound_engine = value;
            break;
        case com_seleuco_mame4droid_Emulator_USING_SAF:
            myosd_droid_using_saf = value;
            break;
        case com_seleuco_mame4droid_Emulator_SAVESATES_IN_ROM_PATH:
            myosd_droid_savestatesinrompath = value;
            break;
        case com_seleuco_mame4droid_Emulator_WARN_ON_EXIT:
            myosd_droid_warn_on_exit = value;
            break;
        case com_seleuco_mame4droid_Emulator_MOUSE:
            myosd_droid_mouse_enable = value;
            break;
        case com_seleuco_mame4droid_Emulator_KEYBOARD:
            myosd_droid_keyboard_enable = value;
            break;
        case com_seleuco_mame4droid_Emulator_ONE_PROCESSOR:
            myosd_one_processor = value;
            break;
        case com_seleuco_mame4droid_Emulator_NODEADZONEANDSAT:
            myosd_droid_no_dzsat = value;
            break;
    }
}

int myosd_droid_getMyValue(int key, int i) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "getMyValue  %d,%d",key,i);

    if (i == 0) {
        switch (key) {
            case com_seleuco_mame4droid_Emulator_IN_MENU :
                return myosd_droid_inMenu;
            case com_seleuco_mame4droid_Emulator_IN_GAME:
                return myosd_droid_inGame;
            case com_seleuco_mame4droid_Emulator_NUMBTNS:
                return myosd_droid_num_buttons;
            case com_seleuco_mame4droid_Emulator_NUMWAYS:
                return myosd_droid_num_ways;
            case com_seleuco_mame4droid_Emulator_IS_LIGHTGUN:
                return myosd_droid_light_gun;
            case com_seleuco_mame4droid_Emulator_IS_MOUSE:
                return myosd_droid_mouse;
            case com_seleuco_mame4droid_Emulator_PAUSE:
                return myosd_is_paused() ? 1 : 0;
            default :
                return -1;
        }
    }
    return -1;
}

void myosd_droid_setMyValueStr(int key, int i, const char *value) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setMyValueStr  %d,%d:%s",key,i,value);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_SAF_PATH: {
            myosd_droid_safpath = value;
            break;
        }
        case com_seleuco_mame4droid_Emulator_ROM_NAME: {
            myosd_droid_rom_name = value;
            break;
        }
        case com_seleuco_mame4droid_Emulator_VERSION: {
            myosd_set(MYOSD_VERSION, (intptr_t)(void*)value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_OVERLAY_EFECT: {
            myosd_droid_overlay_effect = value;
            break;
        }
        default:;
    }
}

char *myosd_droid_getMyValueStr(int key, int i) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "getMyValueStr  %d,%d",key,i);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_ROM_NAME:
            return (char*)myosd_droid_rom_name.c_str();
        case com_seleuco_mame4droid_Emulator_MAME_VERSION:
            return (char*)myosd_get(MYOSD_MAME_VERSION_STRING);
        default:
            return nullptr;
    }
    return nullptr;
}

void myosd_droid_setDigitalData(int i, unsigned long value) {

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setDigitalData  %d,%ld",i,value);

    if (i == 1 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 2)
        myosd_droid_num_of_joys = 2;
    else if (i == 2 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 3)
        myosd_droid_num_of_joys = 3;
    else if (i == 3 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 4)
        myosd_droid_num_of_joys = 4;

    if(myosd_droid_pxasp1 && !myosd_droid_inMenu && myosd_droid_num_of_joys<=1)
    {
        for(int j=0; j<MYOSD_NUM_JOY;j++)
            joy_status[j] = value;
    }
    else
    {
        joy_status[i] = value;
    }

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "set_pad %ld",value);
}

void myosd_droid_setAnalogData(int i, float v1, float v2) {

    if(i == 8) {
        lightgun_x[i] = v1;
        lightgun_y[i] = v2;
    } else {
        if(myosd_droid_pxasp1 && !myosd_droid_inMenu && myosd_droid_num_of_joys<=1)
        {
            for(int j=0; j<MYOSD_NUM_JOY;j++)
            {
                joy_analog_x[j] = v1;
                joy_analog_y[j] = v2;
                joy_analog_trigger_x[j-4]=v1;
                joy_analog_trigger_y[j-4]=v2;
            };
        }
        else if(i<4)
        {
            joy_analog_x[i] = v1;
            joy_analog_y[i] = v2;
        }
        else
        {
            joy_analog_trigger_x[i-4]=v1;
            joy_analog_trigger_y[i-4]=v2;
        }
    }
    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "set analog %d %f %f",i,v1,v2);
}

int myosd_droid_setKeyData(int keyCode,int keyAction,char keyChar) {

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "set keyData %d %d %c",keyCode,keyAction,keyChar);

    if(keyAction == com_seleuco_mame4droid_Emulator_KEY_DOWN) {
        myosd_inputevent ev;
        ev.type = ev.MYOSD_KEY_EVENT;
        switch (keyCode) {
            case AKEYCODE_DEL: keyChar = 8;break;
            case AKEYCODE_ENTER : keyChar = 13;break;
            case AKEYCODE_TAB : keyChar = 9;break;
            //TODO Escape
        }
        ev.data.key_char = keyChar;
        myosd_pushEvent(ev);
    }

    int mame_key = -1;

    if(keyCode < ANDROID_NUM_KEYS)
        mame_key = android_to_mame_key[keyCode];

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "mame_key %d",mame_key);

    if(mame_key!=-1)
        keyboard[mame_key] = keyAction == com_seleuco_mame4droid_Emulator_KEY_DOWN ? 0x80 : 0x00;

    return mame_key != -1;
}

int myosd_droid_setMouseData(int i, int mouseAction, int button, float cx, float cy) {

    /*
    __android_log_print(ANDROID_LOG_DEBUG,
                        "MAME4droid.so", "set mouseData %d %d %d %f %f -> %d %d",i, mouseAction,
                        button, cx, cy, (int)cur_x_mouse, (int)cur_y_mouse);
*/
    if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_MOVE) {
        cur_x_mouse = MAX(0, MIN(cx + cur_x_mouse, myosd_droid_video_width));
        cur_y_mouse = MAX(0, MIN(cy + cur_y_mouse, myosd_droid_video_height));

        myosd_inputevent ev;
        ev.type = ev.MYOSD_MOUSE_MOVE_EVENT;
        ev.data.mouse_data.x = cur_x_mouse;
        ev.data.mouse_data.y = cur_y_mouse;
        myosd_pushEvent(ev);

        if(!myosd_droid_inMenu) {
            pthread_mutex_lock(&mouse_mutex);

            mouse_x[i] += cx;
            mouse_y[i] += cy;

            pthread_mutex_unlock(&mouse_mutex);
       }
    }
    else if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_BTN_DOWN)
    {
        myosd_inputevent ev;
        ev.data.mouse_data.x = cur_x_mouse;
        ev.data.mouse_data.y = cur_y_mouse;

        if(button==1) {

            ev.type = ev.MYOSD_MOUSE_BT1_DOWN;
            myosd_pushEvent(ev);

            static float last_click_x = 0;
            static float last_click_y = 0;
            static std::chrono::steady_clock::time_point last_click_time = std::chrono::steady_clock::time_point::min();

            auto const double_click_speed = std::chrono::milliseconds(250);
            auto const click = std::chrono::steady_clock::now();

            if (click < (last_click_time + double_click_speed)
                && (cur_x_mouse >= (last_click_x - 4) && cur_x_mouse <= (last_click_x + 4))
                && (cur_y_mouse >= (last_click_y - 4) && cur_y_mouse <= (last_click_y + 4)))
            {
                last_click_time = std::chrono::time_point<std::chrono::steady_clock>::min();
                ev.type = ev.MYOSD_MOUSE_BT1_DBLCLK;
                myosd_pushEvent(ev);
                __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "MOUSEB PULSO BT1 DBLCLK!!!!");
            }
            else
            {
                last_click_time = click;
                last_click_x = cur_x_mouse;
                last_click_y = cur_y_mouse;
            }

            mouse_status[i] |= MYOSD_A;
            __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "MOUSEB PULSO BT1!");
        }
        else if(button == 2)
        {
            ev.type = ev.MYOSD_MOUSE_BT2_DOWN;
            myosd_pushEvent(ev);
            mouse_status[i] |= MYOSD_B;
        }
        else if(button == 3)
        {
            mouse_status[i] |= MYOSD_C;
        }
    }
    else if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_BTN_UP)
    {
        myosd_inputevent ev;
        ev.data.mouse_data.x = cur_x_mouse;
        ev.data.mouse_data.y = cur_y_mouse;

        if(button==1)
        {
            ev.type = ev.MYOSD_MOUSE_BT1_UP;
            myosd_pushEvent(ev);
            mouse_status[i] &= ~MYOSD_A;
        }
        else if(button==2)
        {
            ev.type = ev.MYOSD_MOUSE_BT2_UP;
            myosd_pushEvent(ev);
            mouse_status[i] &= ~MYOSD_B;
        }
        else if(button==3)
        {
            mouse_status[i] &= ~MYOSD_C;
        }
    }

    return 1;
}

static void droid_dump_video(void) {

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "droid_dump_video");

    if (myosd_droid_dbl_buffer)
            memcpy(screenbuffer1, screenbuffer2,
                   myosd_droid_video_width * myosd_droid_video_height * PIXEL_PITCH);

    if (dumpVideo_callback != nullptr)
        dumpVideo_callback();

}

static void droid_set_video_mode(int width, int height) {

    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "droid_set_video_mode: %d %d ", width, height);

    if (width == 0)width = 1;
    if (height == 0)height = 1;

    myosd_droid_video_width = width;
    myosd_droid_video_height = height;

    if (changeVideo_callback != nullptr)
        changeVideo_callback(myosd_droid_video_width,
                             myosd_droid_video_height);

    droid_dump_video();
}


static void droid_init_input(){
    memset(keyboard,0, sizeof(keyboard));

    memset(mouse_x,0, sizeof(mouse_x));
    memset(mouse_y,0, sizeof(mouse_y));
    memset(mouse_status,0, sizeof(mouse_status));

    memset(lightgun_x,0, sizeof(lightgun_x));
    memset(lightgun_y,0, sizeof(lightgun_y));

    memset(joy_analog_x,0, sizeof(joy_analog_x));
    memset(joy_analog_y,0, sizeof(joy_analog_y));

    memset(joy_status,0, sizeof(joy_status));//si reseteo se inicia la rom ??

    cur_x_mouse = 0;
    cur_y_mouse = 0;
}

static void droid_map_keyboard(){

    memset(android_to_mame_key, -1, sizeof(android_to_mame_key));

    for(int i = 0; i<= AKEYCODE_9 - AKEYCODE_0;i++ )
        android_to_mame_key[AKEYCODE_0 + i] = MYOSD_KEY_0 + i;

    for(int i = 0; i<= AKEYCODE_Z - AKEYCODE_A;i++ )
        android_to_mame_key[AKEYCODE_A + i] = MYOSD_KEY_A + i;

    for(int i = 0; i<= AKEYCODE_F12 - AKEYCODE_F1;i++ )
        android_to_mame_key[AKEYCODE_F1 + i] = MYOSD_KEY_F1 + i;

    android_to_mame_key[AKEYCODE_ENTER] = MYOSD_KEY_ENTER;
    android_to_mame_key[AKEYCODE_ESCAPE] = MYOSD_KEY_ESC;
    android_to_mame_key[AKEYCODE_DEL] = MYOSD_KEY_BACKSPACE;
    android_to_mame_key[AKEYCODE_TAB] = MYOSD_KEY_TAB;
    android_to_mame_key[AKEYCODE_SPACE] = MYOSD_KEY_SPACE;
    android_to_mame_key[AKEYCODE_MINUS] = MYOSD_KEY_MINUS;
    android_to_mame_key[AKEYCODE_EQUALS] = MYOSD_KEY_EQUALS;
    android_to_mame_key[AKEYCODE_LEFT_BRACKET] = MYOSD_KEY_OPENBRACE;
    android_to_mame_key[AKEYCODE_RIGHT_BRACKET] = MYOSD_KEY_CLOSEBRACE;
    android_to_mame_key[AKEYCODE_BACKSLASH] = MYOSD_KEY_BACKSLASH;
    //android_to_mame_key[????] = MYOSD_KEY_BACKSLASH2;   // TODO: check
    android_to_mame_key[AKEYCODE_SEMICOLON] = MYOSD_KEY_COLON;
    android_to_mame_key[AKEYCODE_APOSTROPHE] = MYOSD_KEY_QUOTE;
    android_to_mame_key[AKEYCODE_GRAVE] = MYOSD_KEY_TILDE;
    android_to_mame_key[AKEYCODE_COMMA] = MYOSD_KEY_COMMA;
    android_to_mame_key[AKEYCODE_PERIOD] = MYOSD_KEY_STOP; //full stop or period key
    android_to_mame_key[AKEYCODE_SLASH] = MYOSD_KEY_SLASH;
    android_to_mame_key[AKEYCODE_CAPS_LOCK] = MYOSD_KEY_CAPSLOCK;
    //android_to_mame_key[?????] = MYOSD_KEY_BACKSLASH2;   // TODO: check

    /* special keys */
    android_to_mame_key[AKEYCODE_SYSRQ] = MYOSD_KEY_PRTSCR;
    android_to_mame_key[AKEYCODE_SCROLL_LOCK] = MYOSD_KEY_SCRLOCK;
    android_to_mame_key[AKEYCODE_BREAK] = MYOSD_KEY_PAUSE;
    android_to_mame_key[AKEYCODE_INSERT] = MYOSD_KEY_INSERT;
    android_to_mame_key[AKEYCODE_MOVE_HOME] = MYOSD_KEY_HOME;
    android_to_mame_key[AKEYCODE_PAGE_UP] = MYOSD_KEY_PGUP;
    android_to_mame_key[AKEYCODE_FORWARD_DEL] = MYOSD_KEY_DEL;
    android_to_mame_key[AKEYCODE_MOVE_END] = MYOSD_KEY_END;
    android_to_mame_key[AKEYCODE_PAGE_DOWN] = MYOSD_KEY_PGDN;
    android_to_mame_key[AKEYCODE_DPAD_RIGHT] = MYOSD_KEY_RIGHT;
    android_to_mame_key[AKEYCODE_DPAD_LEFT] = MYOSD_KEY_LEFT;
    android_to_mame_key[AKEYCODE_DPAD_DOWN] = MYOSD_KEY_DOWN;
    android_to_mame_key[AKEYCODE_DPAD_UP] = MYOSD_KEY_UP;

    /* modifier keys */
    android_to_mame_key[AKEYCODE_CTRL_LEFT] = MYOSD_KEY_LCONTROL;
    android_to_mame_key[AKEYCODE_SHIFT_LEFT] = MYOSD_KEY_LSHIFT;
    android_to_mame_key[AKEYCODE_ALT_LEFT] = MYOSD_KEY_LALT;
    android_to_mame_key[AKEYCODE_CTRL_RIGHT] = MYOSD_KEY_RCONTROL;
    android_to_mame_key[AKEYCODE_SHIFT_RIGHT ] = MYOSD_KEY_RSHIFT;
    android_to_mame_key[AKEYCODE_ALT_RIGHT] = MYOSD_KEY_RALT;

    /* command keys */
    android_to_mame_key[AKEYCODE_META_LEFT] = MYOSD_KEY_LCMD;
    android_to_mame_key[AKEYCODE_META_RIGHT] = MYOSD_KEY_RCMD;

    /* Keypad (numpad) keys */
    android_to_mame_key[AKEYCODE_NUM_LOCK] = MYOSD_KEY_NUMLOCK;
    android_to_mame_key[AKEYCODE_NUMPAD_DIVIDE] = MYOSD_KEY_SLASH;
    android_to_mame_key[AKEYCODE_NUMPAD_MULTIPLY] = MYOSD_KEY_ASTERISK;
    android_to_mame_key[AKEYCODE_NUMPAD_SUBTRACT] = MYOSD_KEY_MINUS_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_ADD] = MYOSD_KEY_PLUS_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_ENTER] = MYOSD_KEY_ENTER_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_DOT] = MYOSD_KEY_STOP;
    android_to_mame_key[AKEYCODE_NUMPAD_EQUALS] = MYOSD_KEY_EQUALS;

    for(int i = 0; i<= AKEYCODE_NUMPAD_9 - AKEYCODE_NUMPAD_0;i++ )
        android_to_mame_key[AKEYCODE_NUMPAD_0 + i] =  MYOSD_KEY_0_PAD + i;

}

static void droid_init(void) {
    if (!lib_inited) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "init");

        int reswidth = 640, resheight = 480;
        int reswidth_osd = 640, resheight_osd = 480;

        switch (myosd_droid_resolution)
        {
            case 0:{reswidth = 400;resheight = 300;break;}//400x300 (4/3)
            case 1:{reswidth = 640;resheight = 480;break;}//640x480 (4/3)
            case 2:{reswidth = 800;resheight = 600;break;}//800x600 (4/3)
            case 3:{reswidth = 1024;resheight = 768;break;}//1024x768 (4/3)
            case 4:{reswidth = 1280;resheight = 720;break;}//1280x720 (16/9)
            case 5:{reswidth = 1440;resheight = 1080;break;}//1440x1080 (4/3)
            case 6:{reswidth = 1920;resheight = 1080;break;}//1920x1080 (16/9)
            case 7:{reswidth = (myosd_droid_res_height_native/2) * 4/3.0f ;resheight = myosd_droid_res_height_native/2;break;}//fullscreen/2 (4/3)
            case 8:{reswidth = myosd_droid_res_width_native/2;resheight = myosd_droid_res_height_native/2;break;}//fullscreen/2
            case 9:{reswidth = myosd_droid_res_height_native * 4/3.0f ;resheight = myosd_droid_res_height_native;break;}//fullscreen (4/3)
            case 10:{reswidth = myosd_droid_res_width_native;resheight = myosd_droid_res_height_native;break;}//fullscreen
        }

        switch (myosd_droid_resolution_osd)
        {
            case 0:{reswidth_osd = reswidth;resheight_osd = resheight;break;}
            case 1:{reswidth_osd = 640;resheight_osd = 480;break;}//640x480 (4/3)
            case 2:{reswidth_osd = 800;resheight_osd = 600;break;}//800x600 (4/3)
            case 3:{reswidth_osd = 1024;resheight_osd = 768;break;}//1024x768 (4/3)
            case 4:{reswidth_osd = 1280;resheight_osd = 720;break;}//1280x720 (16/9)
            case 5:{reswidth_osd = 1440;resheight_osd = 1080;break;}//1440x1080 (4/3)
            case 6:{reswidth_osd = 1920;resheight_osd = 1080;break;}//1920x1080 (16/9)
            case 7:{reswidth_osd = (myosd_droid_res_height_native/2) * 4/3.0f ;resheight_osd = myosd_droid_res_height_native/2;break;}//fullscreen/2 (4/3)
            case 8:{reswidth_osd = myosd_droid_res_width_native/2;resheight_osd = myosd_droid_res_height_native/2;break;}//fullscreen/2
            case 9:{reswidth_osd = myosd_droid_res_height_native * 4/3.0f ;resheight_osd = myosd_droid_res_height_native;break;}//fullscreen (4/3)
            case 10:{reswidth_osd = myosd_droid_res_width_native;resheight_osd = myosd_droid_res_height_native;break;}//fullscreen
        }

        myosd_droid_res_width = reswidth;
        myosd_droid_res_height = resheight;
        myosd_droid_res_width_osd = reswidth_osd;
        myosd_droid_res_height_osd = resheight_osd;

        int efective_reswidth = MAX(MAX(reswidth,reswidth_osd),640);
        int efective_resheight = MAX(MAX(resheight,resheight_osd),480);

        if (screenbuffer1 == nullptr)
            screenbuffer1 = (unsigned char *) malloc(efective_reswidth * efective_resheight * PIXEL_PITCH);

        if(myosd_droid_dbl_buffer && screenbuffer2==NULL)
            screenbuffer2 = (unsigned char *)malloc(efective_reswidth * efective_resheight * PIXEL_PITCH);

        myosd_screen_ptr  = myosd_droid_dbl_buffer ? screenbuffer2 : screenbuffer1;

        if (initVideo_callback != nullptr)
               initVideo_callback((void *) screenbuffer1, efective_reswidth, efective_resheight, PIXEL_PITCH);

        droid_set_video_mode(myosd_droid_res_width_osd, myosd_droid_res_height_osd);

        droid_init_input();

        droid_map_keyboard();

        lib_inited = 1;
    }
}

static void droid_deinit(void) {
    if (lib_inited) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "deinit");

        if (screenbuffer1 != nullptr)
            free(screenbuffer1);
        if(screenbuffer2 != nullptr)
            free(screenbuffer2);

        lib_inited = 0;
    }
}

int myosd_safOpenFile(const char *pathName,const char *mode) {
    if (safOpenFile_callback != nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safOpenFile %s %s",pathName, mode);
        return safOpenFile_callback(pathName, mode);
    }
    return -1;
}

int *myosd_safReadDir(const char *dirName, int reload) {
    if (safReadDir_callback != nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safReadDir %s",dirName);
        int res = safReadDir_callback((char *) dirName, reload);
        if(res!=0) {
            int *value= (int*)malloc(sizeof (int));
            *value = res;
            return value;
        }
    }
    return nullptr;
}

std::string *myosd_safGetNextDirEntry(int *id) {
    std::string *entry = nullptr;
    if (safGetNextDirEntry_callback != nullptr && id!= nullptr) {
        char *s = safGetNextDirEntry_callback(*id);
        if(s!= nullptr) {
            entry = new std::string(s);
            free(s);
        }
    }
    return entry;
}

void myosd_safCloseDir(int *id) {
    if (safCloseDir_callback != nullptr && id!= nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safCloseDir %d",*id);
        safCloseDir_callback(*id);
        free(id);
    }
}

//myosd core callbacks

static void droid_output_cb(int channel, const char *text) {
    //TODO: capture any error/warning output for later use.
    if (channel == MYOSD_OUTPUT_ERROR) {
        __android_log_print(ANDROID_LOG_ERROR, "libMAME4droid.so", "%s", text);
    } else if (channel == MYOSD_OUTPUT_WARNING) {
        __android_log_print(ANDROID_LOG_WARN, "libMAME4droid.so", "%s", text);
    } else if (channel == MYOSD_OUTPUT_INFO) {
        __android_log_print(ANDROID_LOG_INFO, "libMAME4droid.so", "%s", text);
    }
}

static void droid_video_init_cb(int min_width, int min_height) {
    droid_set_video_mode(min_width, min_height);
}

static void droid_video_draw_cb(int in_game, int in_menu, int running) {

    myosd_set(MYOSD_FPS, myosd_droid_show_fps);
    myosd_set(MYOSD_ZOOM_TO_WINDOW, myosd_droid_zoom_to_window);

    myosd_droid_inGame = in_game;
    myosd_droid_inMenu = in_menu;
    myosd_droid_running = running;

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "inGame %d inMenu %d running %d",in_game,in_menu,running);

    droid_dump_video();
}

static void droid_video_exit_cb() {

}

static void droid_input_init_cb(myosd_input_state *input, size_t state_size) {

    //droid_init_input();

    cur_x_mouse = 0;
    cur_y_mouse = 0;

    myosd_droid_num_buttons   = input->num_buttons;
    myosd_droid_num_ways      = input->num_ways;
    myosd_droid_light_gun     = input->num_lightgun != 0;
    myosd_droid_mouse     = input->num_mouse != 0;


    /*
    myosd_droid_num_players   = myosd->num_players;
    myosd_droid_num_coins     = myosd->num_coins;
    myosd_droid_num_inputs    = myosd->num_inputs;
    myosd_droid_has_keyboard  = myosd->num_keyboard != 0;
    */

    if (initInput_callback != nullptr)
        initInput_callback();

}

static void droid_input_poll_cb(bool relative_reset,
                                myosd_input_state *input, size_t state_size) {

    for(int i=0; i<MYOSD_NUM_JOY; i++) {
        input->joy_status[i] = joy_status[i];
        input->joy_analog[i][MYOSD_AXIS_RX] = joy_analog_x[i];
        input->joy_analog[i][MYOSD_AXIS_RY] = joy_analog_y[i];
        input->joy_analog[i][MYOSD_AXIS_RZ] = joy_analog_trigger_x[i];
        input->joy_analog[i][MYOSD_AXIS_LZ] = joy_analog_trigger_y[i];
    }

    for(int i=0; i<MYOSD_NUM_GUN; i++) {
        input->lightgun_x[i] = lightgun_x[i];
        input->lightgun_y[i] = lightgun_y[i];
    }

    if(myosd_droid_mouse_enable) {
        if(relative_reset) {
            for (int i = 0; i < MYOSD_NUM_MICE; i++) {
                input->mouse_status[i] = mouse_status[i];

                input->mouse_x[i] = mouse_x[i] * 512;
                input->mouse_y[i] = mouse_y[i] * 512;

                pthread_mutex_lock(&mouse_mutex);

                mouse_x[i] = 0;
                mouse_y[i] = 0;

                pthread_mutex_unlock(&mouse_mutex);
            }
        }
    }

    if(myosd_droid_keyboard_enable) {
        for (int i = 0; i < MYOSD_NUM_KEYS; i++) {
            input->keyboard[i] = keyboard[i];
        }
    }

    if(myosd_droid_do_pause){
        myosd_pause(true);
        myosd_droid_do_pause = 0;
    }

    if(myosd_droid_do_resume){
        myosd_pause(false);
        myosd_droid_do_resume = 0;
    }
}

void droid_sound_init_cb(int rate, int stereo){
    if (soundInit == 0 && myosd_droid_sound_value != -1) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "openSound rate:%d stereo:%d",rate,stereo);

        if (sound_engine == 1) {
            __android_log_print(ANDROID_LOG_DEBUG, "SOUND", "Open audioTrack");
            if (openSound_callback != nullptr)
                openSound_callback(rate, stereo);
        } else {
            __android_log_print(ANDROID_LOG_DEBUG, "SOUND", "Open openSL %d %d", myosd_droid_sound_value, myosd_droid_sound_frames);
            opensl_snd_ptr  = opensl_open(myosd_droid_sound_value, 2, myosd_droid_sound_frames);
        }

        soundInit = 1;
    }
}
void droid_sound_play_cb(void *buff, int len){
    //__android_log_print(ANDROID_LOG_DEBUG, "PIS", "BUF %d",len);
    if(sound_engine==1)
    {
        if(dumpSound_callback!=nullptr)
            dumpSound_callback(buff,len);
    }
    else
    {
        if(opensl_snd_ptr !=nullptr)
            opensl_write(opensl_snd_ptr ,(short *)buff, len / 2);
    }
}
void droid_sound_exit_cb(){
    if (soundInit == 1) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "closeSound");

        if (sound_engine == 1) {
            if (closeSound_callback != nullptr)
                closeSound_callback();
        } else {
            if (opensl_snd_ptr  != nullptr)
                opensl_close(opensl_snd_ptr );
        }

        soundInit = 0;
    }
}

//main entry point to MAME

int myosd_droid_main(int argc, char **argv) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "*********** ANDROID MAIN ********");

    droid_init();

    myosd_callbacks callbacks = {
            .output_text= droid_output_cb,
            .video_init = droid_video_init_cb,
            .video_draw = droid_video_draw_cb,
            .video_exit = droid_video_exit_cb,
            .input_init = droid_input_init_cb,
            .input_poll = droid_input_poll_cb,
            .sound_init = droid_sound_init_cb,
            .sound_play = droid_sound_play_cb,
            .sound_exit = droid_sound_exit_cb,
/*
            .game_list = m4i_game_list,
            .game_init = m4i_game_start,
            .game_exit = m4i_game_stop,
*/
    };

    myosd_set(MYOSD_DISPLAY_WIDTH, myosd_droid_res_width);
    myosd_set(MYOSD_DISPLAY_HEIGHT, myosd_droid_res_height);
    myosd_set(MYOSD_DISPLAY_WIDTH_OSD, myosd_droid_res_width_osd);
    myosd_set(MYOSD_DISPLAY_HEIGHT_OSD, myosd_droid_res_height_osd);

    static char *args[255];
    int n = 0;
    args[n] = (char *) "mame4x";
    n++;

    if(!myosd_droid_rom_name.empty())
    {
        args[n]=(char *)myosd_droid_rom_name.c_str(); n++;
    }

    if(myosd_droid_simple_ui) {
        args[n] = (char *) "-ui";
        n++;
        args[n] = (char *) "simple";
        n++;
    }

    if(myosd_droid_warn_on_exit) {
        args[n] = (char *) "-confirm_quit";n++;
    }
    else
    {
        args[n] = (char *) "-noconfirm_quit";n++;
    }

    if(myosd_droid_using_saf && !myosd_droid_safpath.empty())
    {
        std::string rp = myosd_droid_safpath+std::string(";./roms");
        args[n]= (char *)"-rompath"; n++;args[n]=(char *)rp.c_str(); n++;

        std::string ap = myosd_droid_safpath+std::string("/artwork;./artwork");
        args[n]= (char *)"-artpath"; n++;args[n]=(char *)ap.c_str(); n++;

        std::string sp = myosd_droid_safpath+std::string("/samples;./samples");
        args[n]= (char *)"-samplepath"; n++;args[n]=(char *)sp.c_str(); n++;

        std::string swp = myosd_droid_safpath+std::string("/sofware;./sofware");
        args[n]= (char *)"-swpath"; n++;args[n]=(char *)swp.c_str(); n++;

        if(myosd_droid_savestatesinrompath)
        {
            std::string sta = myosd_droid_safpath+std::string("/sta");
            args[n]= (char *)"-state_directory"; n++;args[n]=(char *)sta.c_str(); n++;
        }
    }

    if(!myosd_droid_overlay_effect.empty()) {
        args[n] = (char *) "-effect";
        n++;
        args[n] = (char *) myosd_droid_overlay_effect.c_str();
        n++;
    }

    if(myosd_one_processor) {
        args[n] = (char *) "-numprocessors";
        n++;
        args[n] = (char *) "1"; //thats way dkong works
        n++;
    }

    args[n] = (char *) "-ui_active";
    n++;

    args[n] = (char *) "-natural";
    n++;

    args[n] = (char *) "-nocoin_lockout";
    n++;

    if(myosd_droid_cheats) {
        args[n] = (char *) "-cheat";
        n++;
    }

    if(myosd_droid_skip_gameinfo) {
        args[n] = (char *) "-skip_gameinfo";
        n++;
    }

    if(myosd_droid_disable_drc) {
        args[n] = (char *) "-nodrc";
        n++;
    }

    if(!myosd_droid_disable_drc && myosd_droid_enable_drc_use_c) {
        args[n] = (char *) "-drc_use_c";
        n++;
    }

    if(myosd_droid_vector_beam2x) {

        //-beam_width_min 0.75 -beam_width_max 2.5 -beam_intensity_weight 1.75
        args[n] = (char *) "-beam_width_min";n++;
        args[n] = (char *) "2.0";n++;
        args[n] = (char *) "-beam_width_max";n++;
        args[n] = (char *) "3.9";n++;
        args[n] = (char *) "-beam_intensity_weight";n++;
        args[n] = (char *) "0.75";n++;
    }

    if(myosd_droid_vector_flicker) {
        args[n] = (char *) "-flicker";n++;
        args[n] = (char *) "0.4";n++;
    }

    if(myosd_droid_sound_value==-1)
    {
        args[n] = (char *) "-sound";n++;
        args[n] = (char *) "none";n++;
    }
    else
    {
        std::string value = std::to_string(myosd_droid_sound_value);
        args[n] = (char *) "-samplerate";n++;
        args[n] = (char *) value.c_str();n++;
    }

    if(myosd_droid_auto_frameskip)
    {
        args[n] = (char *) "-afs";n++;
    }

    if(myosd_droid_no_dzsat)
    {
        args[n] = (char *) "-jdz";n++;
        args[n] = (char *) "0.0";n++;
        args[n] = (char *) "-jsat";n++;
        args[n] = (char *) "1.0";n++;
    }

    if(0)
    {
        args[n] = (char *) "-v";n++;
    }

    myosd_main(n, args, &callbacks, sizeof(callbacks));

    droid_deinit();

    return 0;
}



