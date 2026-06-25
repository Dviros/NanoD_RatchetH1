#include <Arduino.h>
#include "lcd_thread.h"
#include "sprite_store.h"    // SpriteStore::pathFor, SpriteStore::exists
#include "DeviceSettings.h"  // DeviceSettings::getInstance().activeSprite

// TODO: See if can do it more elegantly from LVGL tfteSPI driver
#include <TFT_eSPI.h>
TFT_eSPI tft;

// lv_mem_add_pool is declared in lvgl.h (included transitively), but include
// the stdlib header explicitly to ensure it is available.
#include <lvgl.h>

// PSRAM pool size for LVGL GIF decode (240x240 RGB565 = 112 KB per frame,
// allocate 256 KB to accommodate multi-frame GIF decode + overhead).
static constexpr size_t LV_PSRAM_POOL_SIZE = 256 * 1024U;

// GIF widget — only one active at a time; recreated when sprite changes.
static lv_obj_t* s_gif_obj = nullptr;
// Full-screen opaque layer that hosts the active sprite ON TOP of everything,
// so a sprite REPLACES the dial view instead of overlaying it. Null = dial shown.
static lv_obj_t* s_sprite_layer = nullptr;


// TODO: Move to PIO Build Flags
static const uint8_t LEDC_CH_LCD_BKL = 0; // LEDC Channel for LCD Backlight
static uint16_t LEDC_MAX_BLK = 3200; // Maximum Brightness for Active Mode
static uint16_t LEDC_MIN_BLK = LEDC_MAX_BLK / 10; // Minimum Brightness for Idle Mode

static uint8_t last_orientation = -1;

#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8)) // 240*240/10*2 = 11520 bytes for 1/10 screen size
uint32_t draw_buf[DRAW_BUF_SIZE / 4];   // Declare a buffer for drawing


LcdThread::LcdThread(const uint8_t task_core) : Thread("LCD", 8192, 1, task_core) {
    _q_lcd_in = xQueueCreate(2, sizeof( LcdCommand ));
    last_command.type = LCD_LAYOUT_DEFAULT;
    last_command.title = nullptr;
    last_command.data1 = nullptr;
    last_command.data2 = nullptr;
    last_command.data3 = nullptr;
    last_command.data4 = nullptr;
};

LcdThread::~LcdThread() {}


void LcdThread::put_lcd_command(LcdCommand& cmd) {
    xQueueSend(_q_lcd_in, &cmd, (TickType_t)0);
};


void LcdThread::handleLcdCommand() {
    LcdCommand cmd;
    if (xQueueReceive(_q_lcd_in, &cmd, (TickType_t)0)) {
        // TODO Implement LCD Command Handling
        last_command = cmd;
    }
};

/* 
    Tasker for Profile data update - updates every 200ms (5Hz)
    Screen: ui_valueScreen
    Checks for last_command and updates Profile Name and Description
*/
static void lcd_manager(lv_timer_t * lcd_cmd_timer) {
    
    /* 
    Listen for Orientation change 
    */
    
    uint8_t device_orientation = DeviceSettings::getInstance().deviceOrientation;
    if (last_orientation != device_orientation) {
        if (device_orientation == 0) {
            tft.setRotation(2);
        } else if (device_orientation == 1) {
            tft.setRotation(3);   
        } else if (device_orientation == 2) {
            tft.setRotation(0);
        } else if (device_orientation == 3) {
            tft.setRotation(1);
        }
    last_orientation = device_orientation;
    lv_obj_invalidate(lv_scr_act());
    };

    /*
        Handle LCD Command 
    */

    lcd_thread.handleLcdCommand();

    if (lv_scr_act()==ui_valueScreen){

        if (lcd_thread.last_command.type == LCD_LAYOUT_DEFAULT){
            if (lcd_thread.last_command.title==nullptr || lcd_thread.last_command.title->length()==0) {
                lv_obj_add_flag(ui_profileName, LV_OBJ_FLAG_HIDDEN); // Hide Profile Name
            } else {
                lv_obj_remove_flag(ui_profileName, LV_OBJ_FLAG_HIDDEN); // Show Profile Name
                lv_label_set_text_fmt(ui_profileName, "%s", lcd_thread.last_command.title->c_str()); // Set Profile Name
            }
            if (lcd_thread.last_command.data1==nullptr  || lcd_thread.last_command.data1->length()==0) {
                lv_obj_add_flag(ui_profileDesc, LV_OBJ_FLAG_HIDDEN);    // Hide Profile Description
            } else {
                lv_obj_remove_flag(ui_profileDesc, LV_OBJ_FLAG_HIDDEN); // Show Profile Description
                lv_label_set_text_fmt(ui_profileDesc, "%s", lcd_thread.last_command.data1->c_str()); // Set Profile Description
            }
            if (lcd_thread.last_command.data3==nullptr || lcd_thread.last_command.data3->length()==0) {
                lv_obj_add_flag(ui_msgModal2, LV_OBJ_FLAG_HIDDEN); // Hide Modal
            } else {
                static uint32_t startTime = 0;
                static const uint32_t interval = 5000; // 5 seconds

                if (startTime == 0) {
                    startTime = millis();
                    
                    lv_obj_remove_flag(ui_msgModal2, LV_OBJ_FLAG_HIDDEN); // Show Modal
                } else {
                    if (millis() - startTime >= interval) {
                        lv_obj_add_flag(ui_msgModal2, LV_OBJ_FLAG_HIDDEN); // Hide Modal
                        lcd_thread.last_command.data3 = nullptr; // Reset Command
                        startTime = 0; // Reset the start time
                    }
                }
            }
        }    
    }
}

/* 
    Tasker for sprite-sheet animation update - updates every 2 seconds - 0.5Hz
    Screen: ui_valueScreen
    Screen: ui_profSelectScreen
    Updates Idle Cat Animation and Profile Selection Arrow Animation
*/
static void idle_anim_handler(lv_timer_t * animtimer) {
    if(com_thread.global_sleep_flag) { // Dont run animation rendering in background if in sleep mode
    if (lv_scr_act()==ui_valueScreen) // Value Screen  
        {
    static uint8_t fps = 0;
    switch(fps) {
        case 0:
            lv_label_set_text(ui_IdleCat, "A");
            lv_label_set_text(ui_IdleCatShadow, "E");
            break;
        case 1:
            lv_label_set_text(ui_IdleCat, "B");
            lv_label_set_text(ui_IdleCatShadow, "E");
            break;
        case 2:
            lv_label_set_text(ui_IdleCat, "C");
            lv_label_set_text(ui_IdleCatShadow, "D");
            lv_obj_set_x(ui_IdleCatShadow, -4);
            break;
        case 3:
            lv_label_set_text(ui_IdleCat, "B");
            lv_label_set_text(ui_IdleCatShadow, "E");
            lv_obj_set_x(ui_IdleCatShadow, 0);
            break;
    }
    fps = (fps + 1) % 4; // 4 Frames every 2 seconds
}
if (lv_scr_act()==ui_profSelectScreen) // Profile Selection Screen
{
    static uint8_t fps = 0;
    switch(fps) {
        case 0:
            lv_obj_set_x( ui_arrInd, 17 );
            break;
        case 1:
            lv_obj_set_x( ui_arrInd, 12 );
            break;
        
    }
    fps = (fps + 1) % 2; // 2 Frames every 2 seconds
}
    }
}

/*
    Tasker for Knob Position tracking - Updates every 16ms (60Hz)
    Screen: ui_valueScreen
    Screen: ui_profSelectScreen
*/


static void counter_handler(lv_timer_t * postimer) {
    static uint16_t last_pos = -1;      // Default Last Position
    static uint16_t last_end_pos = 0;   // Init to 0 to avoid spurious arc range update on first call
    static bool overlay_toggle = false; // Default Overlay Toggle
    uint16_t pos = foc_thread.pass_cur_pos(); // Get Current Position from FOC Thread
    uint16_t end_pos = foc_thread.pass_end_pos(); // Get End Position from FOC Thread
    
    if (pos != last_pos) {
       
       if (lv_scr_act()==ui_valueScreen){
           lv_label_set_text_fmt(ui_posind, "%d", pos);
           lv_label_set_text_fmt(ui_posindSha, "%d", pos);
           if (end_pos != last_end_pos) {
               lv_arc_set_range(ui_Arc1, 0, end_pos);
               last_end_pos = end_pos;
               // Don't update arc range if end_pos is same as last_end_pos
           }
           lv_arc_set_value(ui_Arc1, pos);
           last_pos = pos; // Update Last Position
       }
       if (lv_scr_act()==ui_profSelectScreen){
           lv_label_set_text_fmt(ui_pCount, "%d", pos); // Set Position Indicator
           if(last_pos != pos){
           lv_roller_set_selected(ui_profList, pos, LV_ANIM_ON); // Set Roller to Current Position - Animate ON
           last_pos = pos; // Update Last Position
           }
       }
    }
        
    if (com_thread.global_sleep_flag && overlay_toggle) {
        
        lv_obj_remove_flag( ui_IdleCat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag( ui_IdleCatShadow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag( ui_dataScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag( ui_msgModal2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_arc_color(ui_Arc1, lv_color_hex(0x565656), LV_PART_INDICATOR | LV_STATE_DEFAULT );
        ledcWrite(0, LEDC_MIN_BLK); // Set Backlight to Min Brightness
        overlay_toggle = !overlay_toggle;
    }
    if (!com_thread.global_sleep_flag && !overlay_toggle){
        
        lv_obj_add_flag( ui_IdleCat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag( ui_IdleCatShadow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag( ui_dataScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_arc_color(ui_Arc1, lv_color_hex(0xFF7D00), LV_PART_INDICATOR | LV_STATE_DEFAULT );
        ledcWrite(0, LEDC_MAX_BLK); // Set Backlight to Max Brightness
        overlay_toggle = !overlay_toggle;          
    }
}


/* ---------------------------------------------------------------------------
 * lcd_show_sprite() — call ONLY from within the LVGL task (lcd_manager timer
 * or the LcdThread::run loop) so we don't need an external mutex.
 *
 * For static images (.bmp, .png): sets lv_img source to "L:<path>".
 * For animated GIFs (.gif):       creates/replaces an lv_gif widget.
 * Hides the widget when name is empty or the sprite doesn't exist.
 *
 * 'L' is the LittleFS LVGL driver letter registered by SpriteStore::begin().
 * -------------------------------------------------------------------------*/
void lcd_show_sprite(const String& name) {
    // --- tear down the previous full-screen sprite layer (and its GIF) ---
    if (s_sprite_layer) {
        lv_obj_del(s_sprite_layer);   // deletes children (GIF/img) too
        s_sprite_layer = nullptr;
        s_gif_obj      = nullptr;
    }
    // Hide the legacy in-screen sprite widget — we no longer draw onto the dial.
    if (ui_spriteImg) lv_obj_add_flag(ui_spriteImg, LV_OBJ_FLAG_HIDDEN);

    // Empty / missing sprite -> nothing on top -> the dial view is shown.
    if (name.length() == 0 || !SpriteStore::exists(name)) return;

    String lvPath = "L:" + SpriteStore::pathFor(name);
    bool is_gif = name.length() >= 4 &&
                  name.substring(name.length() - 4).equalsIgnoreCase(".gif");

    // Full-screen opaque black layer on lv_layer_top() — sits ABOVE every screen,
    // so the sprite fully replaces the dial instead of overlaying it. A smaller
    // sprite is centred on black; a 240x240 sprite fills the round display.
    s_sprite_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_sprite_layer);
    lv_obj_set_size(s_sprite_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_sprite_layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sprite_layer, LV_OPA_COVER, 0);
    lv_obj_center(s_sprite_layer);
    lv_obj_clear_flag(s_sprite_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    if (is_gif) {
#if LV_USE_GIF
        s_gif_obj = lv_gif_create(s_sprite_layer);
        if (s_gif_obj) {
            lv_gif_set_src(s_gif_obj, lvPath.c_str());
            lv_obj_center(s_gif_obj);
            lv_obj_clear_flag(s_gif_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        } else {
            // PSRAM OOM — drop the layer so we fall back to the dial rather than a black screen.
            lv_obj_del(s_sprite_layer);
            s_sprite_layer = nullptr;
        }
#endif
    } else {
        lv_obj_t* img = lv_img_create(s_sprite_layer);
        lv_img_set_src(img, lvPath.c_str());
        lv_obj_center(img);
    }
}

/* ---------------------------------------------------------------------------
 * Internal timer: reload sprite whenever DeviceSettings.activeSprite changes.
 * -------------------------------------------------------------------------*/
static void sprite_refresh_handler(lv_timer_t* /*t*/) {
    static String last_sprite;
    const String& active = DeviceSettings::getInstance().activeSprite;
    if (active != last_sprite) {
        last_sprite = active;
        lcd_show_sprite(active);
    }
}

void LcdThread::run() {
    // Setup LedC
    ledcSetup(0, 5000, 12); // 4096 steps @ 5Khz
    ledcAttachPin(5, 0); // LEDC on Pin 5
    lv_init(); // Initialize LVGL

    // Add a PSRAM pool to the LVGL allocator so that lv_gif frame decode
    // (up to 112 KB per 240x240 RGB565 frame) can use external SPIRAM rather
    // than the 48 KB internal-SRAM pool.  Safe even if PSRAM is absent —
    // ps_malloc returns NULL and lv_mem_add_pool is a no-op for NULL.
    void* psram_buf = ps_malloc(LV_PSRAM_POOL_SIZE);
    if (psram_buf) {
        lv_mem_add_pool(psram_buf, LV_PSRAM_POOL_SIZE);
        Serial.println("[LCD] LVGL PSRAM pool 256 KB registered");
    } else {
        Serial.println("[LCD] PSRAM pool alloc failed — GIF decode will use SRAM (may OOM for large GIFs)");
    }

    // Register the sprite 'L:' filesystem driver now that the LVGL heap exists.
    // (Must NOT happen in setup()/SpriteStore::begin() — would crash before lv_init.)
    SpriteStore::registerLvglDriver();

    /*
        Create Display Object
    */

    lv_display_t * disp;
    disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, draw_buf, sizeof(draw_buf));
    
    /*
        Set timers for LCD Data Handler, Idle Animation Handler and Counter Handler
    */

    lv_timer_t * animtimer = lv_timer_create(idle_anim_handler, 1500, NULL); // 0.5Hz
    lv_timer_t * postimer = lv_timer_create(counter_handler, 33, NULL); // ~30Hz
    lv_timer_t * lcd_cmd_timer = lv_timer_create(lcd_manager, 1000, NULL); // 1Hz
    // Poll DeviceSettings.activeSprite for changes and update the sprite widget.
    lv_timer_t * sprite_timer = lv_timer_create(sprite_refresh_handler, 500, NULL); // 2Hz

    /* 
        Start Timers
    */

    lv_timer_ready(animtimer);
    lv_timer_ready(postimer);
    lv_timer_ready(lcd_cmd_timer);
    lv_timer_ready(sprite_timer);


    ui_init(); // Initialize UI
    ledcWrite(0, LEDC_MAX_BLK); // Initialize Backlight to Max Brightness
    
    /*s
        Main Loop for LVGL
        Consist only of LVGL Timer Handler and LVGL Tick Increment
    */

    // { "settings": { "deviceOrientation": 2 }}

    while (1) {        
     

        lv_timer_handler();
        lv_tick_inc(10);
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
};

