// After five long months of waiting... 
// countless hours of on-and-off coding... 
// endless bugs...
// and too many school nights spent up at 3 A.M. while finishing college apps...
// The update is finally here. 
// I present to you my GREATEST PROJECT AS OF Nov 2025
// ANKI REMOTE 1.3

#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <gui/icon_i.h>

#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>

#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>

#include "anki_remote_icons.h"

// -----------------------------------------------------------------------------
// Basic Defines & Paths
// -----------------------------------------------------------------------------

#define TAG "AnkiRemoteApp"

#define KEYMAP_PATH APP_DATA_PATH("keymaps.dat") // Legacy single-mapping file (for migration)
#define PRESETS_PATH APP_DATA_PATH("presets.dat") // New presets file

#define NUM_FLIPPER_BUTTONS 6
#define MENU_OPTIONS_COUNT 3 // Start, Manage Presets, Credits

// This is so it can use the same bluetooth keys as other HID apps
#define HID_BT_KEYS_STORAGE_PATH EXT_PATH("apps_data/hid_ble/.bt_hid.keys")

// Preset constraints
#define MAX_PRESETS 12
#define PRESET_NAME_MAX_LEN 9 // Character limit for names (fits cleanly on screen)

// Modifier bit flags (internal to our mapping struct)
#define MOD_CTRL_BIT  (1 << 0)
#define MOD_SHIFT_BIT (1 << 1)
#define MOD_ALT_BIT   (1 << 2)
#define MOD_GUI_BIT   (1 << 3)

// -----------------------------------------------------------------------------
// Enums & Small Types
// -----------------------------------------------------------------------------

typedef enum {
    KeyActionNormal = 0,         // normal key
    KeyActionToggleModifier = 1, // toggles picker state (Shift/Ctrl/Alt/Cmd)
    KeyActionTapModifier = 2,    // "tap" mod key mapping (no modifier bitmask)
} KeyAction;

typedef enum {
    FlipperButtonUp = 0,
    FlipperButtonDown,
    FlipperButtonLeft,
    FlipperButtonRight,
    FlipperButtonOk,
    FlipperButtonBack,
} FlipperButton;

typedef enum {
    SceneMenu = 0,
    SceneController,
    ScenePresetManager,
    ScenePresetEditMenu,
    SceneKeyPicker,
    SceneCredits,
} AppScene;

typedef enum {
    EventTypeInput = 0,
    EventTypeTick, // currently unused but here in case you wanna get fancy later (no one is reading this what am I doing)
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} AnkiRemoteEvent;

// Key combination: a single HID key + our modifier bitmask
// Now supports both short and long press mappings
typedef struct {
    uint8_t keycode;       // short press key
    uint8_t modifiers;     // short press modifiers
    uint8_t long_keycode;  // long press key
    uint8_t long_modifiers;// long press modifiers
} KeyMapping;

// A single preset: name + one mapping per Flipper button
typedef struct {
    char name[PRESET_NAME_MAX_LEN + 1];
    KeyMapping keymap[NUM_FLIPPER_BUTTONS];
} Preset;

typedef struct AnkiRemoteApp AnkiRemoteApp; // Forward declaration

// -----------------------------------------------------------------------------
// View Models
// -----------------------------------------------------------------------------
// NOTE: draw callbacks receive the View *model* (not the View context).
// We store the app pointer in each view model to keep rendering safe.
typedef struct {
    AnkiRemoteApp* app;
} AnkiRemoteViewModel;

// -----------------------------------------------------------------------------
// HID Keyboard UI Data (mostly lifted from stock BLE Remote, just cleaner)
// -----------------------------------------------------------------------------

typedef struct {
    uint8_t width;              // key width in keyboard grid units
    char key;                   // unshifted char
    char shift_key;             // shifted char
    const Icon* icon;           // normal icon
    const Icon* icon_shift;     // icon when shift "pressed"
    const Icon* icon_toggled;   // icon when this key's toggle state is active
    uint8_t value;              // HID keycode
    const char* label;          // small text label (for special/system keys)
    uint8_t action;             // KeyAction enum
} HidKeyboardKey;

#define KEYBOARD_ROW_COUNT 10
#define KEYBOARD_COLUMN_COUNT 12

// If you change this table, U ARE COOKED
const HidKeyboardKey hid_keyboard_keyset[KEYBOARD_ROW_COUNT][KEYBOARD_COLUMN_COUNT] = {
    // Row 0: F-keys
    {{.width = 1, .icon = &I_ButtonF1_5x8, .value = HID_KEYBOARD_F1}, {.width = 1, .icon = &I_ButtonF2_5x8, .value = HID_KEYBOARD_F2}, {.width = 1, .icon = &I_ButtonF3_5x8, .value = HID_KEYBOARD_F3}, {.width = 1, .icon = &I_ButtonF4_5x8, .value = HID_KEYBOARD_F4}, {.width = 1, .icon = &I_ButtonF5_5x8, .value = HID_KEYBOARD_F5}, {.width = 1, .icon = &I_ButtonF6_5x8, .value = HID_KEYBOARD_F6}, {.width = 1, .icon = &I_ButtonF7_5x8, .value = HID_KEYBOARD_F7}, {.width = 1, .icon = &I_ButtonF8_5x8, .value = HID_KEYBOARD_F8}, {.width = 1, .icon = &I_ButtonF9_5x8, .value = HID_KEYBOARD_F9}, {.width = 1, .icon = &I_ButtonF10_5x8, .value = HID_KEYBOARD_F10}, {.width = 1, .icon = &I_ButtonF11_5x8, .value = HID_KEYBOARD_F11}, {.width = 1, .icon = &I_ButtonF12_5x8, .value = HID_KEYBOARD_F12}},
    // Row 1: Number row
    {{.width = 1, .key = '1', .shift_key = '!', .value = HID_KEYBOARD_1}, {.width = 1, .key = '2', .shift_key = '@', .value = HID_KEYBOARD_2}, {.width = 1, .icon_shift = &I_hash_button_9x11, .key = '3', .shift_key = '#', .value = HID_KEYBOARD_3}, {.width = 1, .key = '4', .shift_key = '$', .value = HID_KEYBOARD_4}, {.width = 1, .icon_shift = &I_percent_button_9x11, .key = '5', .shift_key = '%', .value = HID_KEYBOARD_5}, {.width = 1, .key = '6', .shift_key = '^', .value = HID_KEYBOARD_6}, {.width = 1, .key = '7', .shift_key = '&', .value = HID_KEYBOARD_7}, {.width = 1, .key = '8', .shift_key = '*', .value = HID_KEYBOARD_8}, {.width = 1, .key = '9', .shift_key = '(', .value = HID_KEYBOARD_9}, {.width = 1, .key = '0', .shift_key = ')', .value = HID_KEYBOARD_0}, {.width = 2, .icon = &I_backspace_19x11, .value = HID_KEYBOARD_DELETE}},
    // Row 2: QWERTY
    {{.width = 1, .key = 'q', .shift_key = 'Q', .value = HID_KEYBOARD_Q}, {.width = 1, .key = 'w', .shift_key = 'W', .value = HID_KEYBOARD_W}, {.width = 1, .key = 'e', .shift_key = 'E', .value = HID_KEYBOARD_E}, {.width = 1, .key = 'r', .shift_key = 'R', .value = HID_KEYBOARD_R}, {.width = 1, .key = 't', .shift_key = 'T', .value = HID_KEYBOARD_T}, {.width = 1, .key = 'y', .shift_key = 'Y', .value = HID_KEYBOARD_Y}, {.width = 1, .key = 'u', .shift_key = 'U', .value = HID_KEYBOARD_U}, {.width = 1, .key = 'i', .shift_key = 'I', .value = HID_KEYBOARD_I}, {.width = 1, .key = 'o', .shift_key = 'O', .value = HID_KEYBOARD_O}, {.width = 1, .key = 'p', .shift_key = 'P', .value = HID_KEYBOARD_P}, {.width = 1, .icon = &I_sq_bracket_left_button_9x11, .icon_shift = &I_brace_left_button_9x11, .value = HID_KEYBOARD_OPEN_BRACKET}, {.width = 1, .icon = &I_sq_bracket_right_button_9x11, .icon_shift = &I_brace_right_button_9x11, .value = HID_KEYBOARD_CLOSE_BRACKET}},
    // Row 3: ASDF
    {{.width = 1, .key = 'a', .shift_key = 'A', .value = HID_KEYBOARD_A}, {.width = 1, .key = 's', .shift_key = 'S', .value = HID_KEYBOARD_S}, {.width = 1, .key = 'd', .shift_key = 'D', .value = HID_KEYBOARD_D}, {.width = 1, .key = 'f', .shift_key = 'F', .value = HID_KEYBOARD_F}, {.width = 1, .key = 'g', .shift_key = 'G', .value = HID_KEYBOARD_G}, {.width = 1, .key = 'h', .shift_key = 'H', .value = HID_KEYBOARD_H}, {.width = 1, .key = 'j', .shift_key = 'J', .value = HID_KEYBOARD_J}, {.width = 1, .key = 'k', .shift_key = 'K', .value = HID_KEYBOARD_K}, {.width = 1, .key = 'l', .shift_key = 'L', .value = HID_KEYBOARD_L}, {.width = 1, .key = ';', .shift_key = ':', .value = HID_KEYBOARD_SEMICOLON}, {.width = 2, .icon = &I_Return_10x7, .value = HID_KEYBOARD_RETURN}},
    // Row 4: ZXCV stuff + arrows
    {{.width = 1, .key = 'z', .shift_key = 'Z', .value = HID_KEYBOARD_Z}, {.width = 1, .key = 'x', .shift_key = 'X', .value = HID_KEYBOARD_X}, {.width = 1, .key = 'c', .shift_key = 'C', .value = HID_KEYBOARD_C}, {.width = 1, .key = 'v', .shift_key = 'V', .value = HID_KEYBOARD_V}, {.width = 1, .key = 'b', .shift_key = 'B', .value = HID_KEYBOARD_B}, {.width = 1, .key = 'n', .shift_key = 'N', .value = HID_KEYBOARD_N}, {.width = 1, .key = 'm', .shift_key = 'M', .value = HID_KEYBOARD_M}, {.width = 1, .icon = &I_slash_button_9x11, .shift_key = '?', .value = HID_KEYBOARD_SLASH}, {.width = 1, .icon = &I_backslash_button_9x11, .shift_key = '|', .value = HID_KEYBOARD_BACKSLASH}, {.width = 1, .icon = &I_backtick_button_9x11, .shift_key = '~', .value = HID_KEYBOARD_GRAVE_ACCENT}, {.width = 1, .icon = &I_ButtonUp_7x4, .value = HID_KEYBOARD_UP_ARROW}, {.width = 1, .icon_shift = &I_underscore_button_9x11, .key = '-', .shift_key = '_', .value = HID_KEYBOARD_MINUS}},
    // Row 5: Bottom symbol row
    {{.width = 1, .icon = &I_Shift_inactive_7x9, .icon_toggled = &I_Shift_active_7x9, .value = HID_KEYBOARD_L_SHIFT, .action = KeyActionToggleModifier}, {.width = 1, .key = ',', .shift_key = '<', .value = HID_KEYBOARD_COMMA}, {.width = 1, .key = '.', .shift_key = '>', .value = HID_KEYBOARD_DOT}, {.width = 4, .value = HID_KEYBOARD_SPACEBAR}, {.width = 0}, {.width = 0}, {.width = 0},
    {.width = 1, .key = '\'', .shift_key = '"', .icon = &I_apostrophe_button_9x11, .icon_shift = &I_quote_button_9x11, .value = HID_KEYBOARD_APOSTROPHE},
    {.width = 1, .icon = &I_equals_button_9x11, .shift_key = '+', .value = HID_KEYBOARD_EQUAL_SIGN}, {.width = 1, .icon = &I_ButtonLeft_4x7, .value = HID_KEYBOARD_LEFT_ARROW}, {.width = 1, .icon = &I_ButtonDown_7x4, .value = HID_KEYBOARD_DOWN_ARROW}, {.width = 1, .icon = &I_ButtonRight_4x7, .value = HID_KEYBOARD_RIGHT_ARROW}},
    // Row 6: Modifier keys row
    {{.width = 2, .icon = &I_Ctrl_17x10, .icon_toggled = &I_Ctrl_active_17x9, .value = HID_KEYBOARD_L_CTRL, .action = KeyActionToggleModifier}, {.width = 0}, {.width = 2, .icon = &I_Alt_17x10, .icon_toggled = &I_Alt_active_17x9, .value = HID_KEYBOARD_L_ALT, .action = KeyActionToggleModifier}, {.width = 0}, {.width = 2, .icon = &I_Cmd_17x10, .icon_toggled = &I_Cmd_active_17x9, .value = HID_KEYBOARD_L_GUI, .action = KeyActionToggleModifier}, {.width = 0}, {.width = 2, .icon = &I_Tab_17x10, .value = HID_KEYBOARD_TAB}, {.width = 0}, {.width = 2, .icon = &I_Esc_17x10, .value = HID_KEYBOARD_ESCAPE}, {.width = 0}, {.width = 2, .icon = &I_Del_17x10, .value = HID_KEYBOARD_DELETE_FORWARD}},
    // Row 7: Navigation row
    {
        {.width = 2, .label = "Ins",   .value = HID_KEYBOARD_INSERT},
        {.width = 2, .label = "Hm",  .value = HID_KEYBOARD_HOME},
        {.width = 3, .label = "PgUp",   .value = HID_KEYBOARD_PAGE_UP},
        {.width = 2, .label = "End",   .value = HID_KEYBOARD_END},
        {.width = 3, .label = "PgDn",  .value = HID_KEYBOARD_PAGE_DOWN},
        {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}
    },
    // Row 8: System row
    {
        {.width = 3, .label = "PrtSc", .value = HID_KEYBOARD_PRINT_SCREEN},
        {.width = 3, .label = "ScrL",  .value = HID_KEYBOARD_SCROLL_LOCK},
        {.width = 3, .label = "PBrk",  .value = HID_KEYBOARD_PAUSE},
        {.width = 3, .label = "Sel",   .value = HID_KEYBOARD_SELECT},
        {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0},
        {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}
    },
    // Row 9: Modifier taps row
    {
        {.width = 3, .label = "Ctrl",  .value = HID_KEYBOARD_L_CTRL,  .action = KeyActionTapModifier},
        {.width = 3, .label = "Shft",  .value = HID_KEYBOARD_L_SHIFT, .action = KeyActionTapModifier},
        {.width = 3, .label = "Alt",   .value = HID_KEYBOARD_L_ALT,   .action = KeyActionTapModifier},
        {.width = 3, .label = "Cmd",   .value = HID_KEYBOARD_L_GUI,   .action = KeyActionTapModifier},
        {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0},
        {.width = 0}, {.width = 0}, {.width = 0}, {.width = 0}
    }
};

// -----------------------------------------------------------------------------
// App State
// -----------------------------------------------------------------------------

// View IDs for ViewDispatcher
typedef enum {
    AnkiRemoteViewMenu = 0,
    AnkiRemoteViewController,
    AnkiRemoteViewPresetManager,
    AnkiRemoteViewPresetEditMenu,
    AnkiRemoteViewKeyPicker,
    AnkiRemoteViewTextInput,  // For rename dialog
    AnkiRemoteViewCredits,
} AnkiRemoteView;

struct AnkiRemoteApp {
    // OS / system handles
    Gui* gui;
    FuriMessageQueue* event_queue;
    NotificationApp* notifications;
    Bt* bt;
    FuriHalBleProfileBase* ble_hid_profile;

    // View system
    ViewDispatcher* view_dispatcher;
    View* views[7];  // Menu, Controller, PresetManager, PresetEditMenu, KeyPicker, TextInput, Credits
    TextInput* text_input;

    // Global app state
    AppScene current_scene;
    bool running;
    bool connected;

    // Presets
    Preset presets[MAX_PRESETS];
    uint8_t preset_count;
    uint8_t active_preset;      // which preset is live
    uint8_t highlighted_preset; // which preset row is highlighted in manager
    uint8_t led_brightness;     // one global LED brightness

    // Menu state
    struct {
        uint8_t selected_item;
    } menu_state;

    // Preset manager state
    struct {
        uint8_t selected_item;
        uint8_t scroll_offset;
    } preset_manager_state;

    // Preset edit menu state
    struct {
        uint8_t selected_item;
        uint8_t scroll_offset;
        uint8_t editing_preset_index;
    } preset_edit_menu_state;

    // Controller state (button visuals)
    struct {
        bool up_pressed;
        bool down_pressed;
        bool left_pressed;
        bool right_pressed;
        bool ok_pressed;
        bool back_pressed;
        bool ignore_next_back_release;
        // Track if long press was already sent (to avoid sending short press too)
        bool up_long_sent;
        bool down_long_sent;
        bool left_long_sent;
        bool right_long_sent;
        bool ok_long_sent;
        bool back_long_sent;
        // Track the long press mapping so we can release it properly
        KeyMapping up_long_mapping;
        KeyMapping down_long_mapping;
        KeyMapping left_long_mapping;
        KeyMapping right_long_mapping;
        KeyMapping ok_long_mapping;
        KeyMapping back_long_mapping;
    } controller_state;

    // Key picker state
    struct {
        uint8_t x, y;        // cursor in keyboard grid
        bool shift_pressed;
        bool ctrl_pressed;
        bool alt_pressed;
        bool gui_pressed;
        FlipperButton configuring_button;
        uint8_t editing_preset_index;
        bool editing_long_press; // true = editing long press, false = editing short press
    } settings_state;

    // Rename buffer
    char tmp_name[PRESET_NAME_MAX_LEN + 1];
    uint8_t rename_preset_index;

    // Flag to prevent the OK button release from being processed by the next screen
    bool ignore_next_ok_release;
};

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------

static void anki_remote_view_menu_draw(Canvas* canvas, void* context);
static bool anki_remote_view_menu_input(InputEvent* event, void* context);

static void anki_remote_view_controller_draw(Canvas* canvas, void* context);
static bool anki_remote_view_controller_input(InputEvent* event, void* context);

static void anki_remote_view_preset_manager_draw(Canvas* canvas, void* context);
static bool anki_remote_view_preset_manager_input(InputEvent* event, void* context);

static void anki_remote_view_preset_edit_menu_draw(Canvas* canvas, void* context);
static bool anki_remote_view_preset_edit_menu_input(InputEvent* event, void* context);

static void anki_remote_view_key_picker_draw(Canvas* canvas, void* context);
static bool anki_remote_view_key_picker_input(InputEvent* event, void* context);

static void anki_remote_view_credits_draw(Canvas* canvas, void* context);
static bool anki_remote_view_credits_input(InputEvent* event, void* context);

static void anki_remote_connection_status_callback(BtStatus status, void* context);
static void anki_remote_switch_to_view(AnkiRemoteApp* app, AnkiRemoteView view);
static void anki_remote_send_key_combo(AnkiRemoteApp* app, KeyMapping mapping, bool is_press);
static const char* get_button_name_oriented(const AnkiRemoteApp* app, FlipperButton button);
static void get_key_combo_name(KeyMapping mapping, char* buffer, size_t buffer_size);
static void anki_remote_reset_keymap_only_for_preset(Preset* preset);
static void preset_rename_result_cb(void* context);
static bool anki_remote_exit_callback(void* context);

// -----------------------------------------------------------------------------
// Persistent Preset Storage
// -----------------------------------------------------------------------------

typedef struct {
    uint32_t magic;        // 'ARPM'
    uint16_t version;      // 0x0002
    uint8_t preset_count;
    uint8_t active_index;
    uint8_t led_brightness;
    uint8_t reserved[9];   // pad to 16 bytes
} PresetFileHeader;

#define PRESET_FILE_MAGIC 0x4152504DUL // 'ARPM'
#define PRESET_FILE_VERSION 0x0002 // Long press support

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

static void keymap_clear(KeyMapping* km) {
    km->keycode = 0;
    km->modifiers = 0;
    km->long_keycode = 0;
    km->long_modifiers = 0;
}

static void preset_init_empty(Preset* p, const char* name) {
    if(!p) return;
    if(name) {
        strlcpy(p->name, name, sizeof(p->name));
    } else {
        strlcpy(p->name, "Preset", sizeof(p->name));
    }
    for(uint8_t i = 0; i < NUM_FLIPPER_BUTTONS; ++i) {
        keymap_clear(&p->keymap[i]);
    }
}

static void anki_remote_set_default_presets(AnkiRemoteApp* app) {
    app->preset_count = 1;
    app->active_preset = 0;
    app->led_brightness = 255;
    preset_init_empty(&app->presets[0], "Default");

    // Base defaults
    KeyMapping base_up = {
        .keycode = HID_KEYBOARD_Z,
        .modifiers = MOD_GUI_BIT,
        .long_keycode = HID_KEYBOARD_2,
        .long_modifiers = MOD_SHIFT_BIT,
    };
    KeyMapping base_right = {
        .keycode = HID_KEYBOARD_MINUS,
        .modifiers = 0,
        .long_keycode = HID_KEYBOARD_UP_ARROW,
        .long_modifiers = 0,
    };
    KeyMapping base_down = {
        .keycode = HID_KEYBOARD_N,
        .modifiers = 0,
        .long_keycode = HID_KEYBOARD_D,
        .long_modifiers = 0,
    };
    KeyMapping base_left = {
        .keycode = HID_KEYBOARD_1,
        .modifiers = 0,
        .long_keycode = HID_KEYBOARD_DOWN_ARROW,
        .long_modifiers = 0,
    };
    KeyMapping base_ok = {
        .keycode = HID_KEYBOARD_RETURN,
        .modifiers = 0,
        .long_keycode = HID_KEYBOARD_Y,
        .long_modifiers = 0,
    };
    KeyMapping base_back = {
        .keycode = HID_KEYBOARD_S,
        .modifiers = 0,
        .long_keycode = 0,
        .long_modifiers = 0,
    };

    // Rotate defaults so: bottom -> right, right -> top, top -> left, left -> bottom.
    app->presets[0].keymap[FlipperButtonUp] = base_right;
    app->presets[0].keymap[FlipperButtonRight] = base_down;
    app->presets[0].keymap[FlipperButtonDown] = base_left;
    app->presets[0].keymap[FlipperButtonLeft] = base_up;
    app->presets[0].keymap[FlipperButtonOk] = base_ok;
    app->presets[0].keymap[FlipperButtonBack] = base_back;
}

// We save everything in one file
static void anki_remote_save_presets(AnkiRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PRESETS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        PresetFileHeader hdr = {0};
        hdr.magic = PRESET_FILE_MAGIC;
        hdr.version = PRESET_FILE_VERSION;
        hdr.preset_count = app->preset_count;
        hdr.active_index = app->active_preset;
        hdr.led_brightness = app->led_brightness;
        storage_file_write(file, &hdr, sizeof(hdr));
        // Write presets
        for(uint8_t i = 0; i < app->preset_count; ++i) {
            storage_file_write(file, app->presets[i].name, PRESET_NAME_MAX_LEN + 1);
            storage_file_write(file, app->presets[i].keymap, sizeof(KeyMapping) * NUM_FLIPPER_BUTTONS);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

// Migration path from older single-keymap file into presets file.
// Reads once, converts to preset 0, and saves in new format.
// Handles migration from v0x0001 (short press only) to v0x0002 (short + long press).
static bool anki_remote_load_presets(AnkiRemoteApp* app) {
    bool loaded = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PRESETS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        PresetFileHeader hdr = {0};
        if(storage_file_read(file, &hdr, sizeof(hdr)) == sizeof(hdr) &&
           hdr.magic == PRESET_FILE_MAGIC && hdr.preset_count >= 1 && hdr.preset_count <= MAX_PRESETS) {
            app->preset_count = hdr.preset_count;
            app->active_preset = (hdr.active_index < app->preset_count) ? hdr.active_index : 0;
            app->led_brightness = hdr.led_brightness ? hdr.led_brightness : 255;

            // Check version for backward compatibility
            bool needs_save = false;
            if(hdr.version == 0x0001) {
                // Old format: KeyMapping was 2 bytes (keycode + modifiers)
                // Need to migrate to 4 bytes (adding long_keycode + long_modifiers)
                typedef struct {
                    uint8_t keycode;
                    uint8_t modifiers;
                } OldKeyMapping;
                OldKeyMapping old_keymap[NUM_FLIPPER_BUTTONS];

                for(uint8_t i = 0; i < app->preset_count; ++i) {
                    storage_file_read(file, app->presets[i].name, PRESET_NAME_MAX_LEN + 1);
                    app->presets[i].name[PRESET_NAME_MAX_LEN] = '\0';
                    storage_file_read(file, old_keymap, sizeof(OldKeyMapping) * NUM_FLIPPER_BUTTONS);
                    // Migrate to new format: copy short press, clear long press
                    for(uint8_t j = 0; j < NUM_FLIPPER_BUTTONS; ++j) {
                        app->presets[i].keymap[j].keycode = old_keymap[j].keycode;
                        app->presets[i].keymap[j].modifiers = old_keymap[j].modifiers;
                        app->presets[i].keymap[j].long_keycode = 0;
                        app->presets[i].keymap[j].long_modifiers = 0;
                    }
                }
                needs_save = true;
            } else if(hdr.version == 0x0002) {
                // Current format: v2 with long press support
                for(uint8_t i = 0; i < app->preset_count; ++i) {
                    storage_file_read(file, app->presets[i].name, PRESET_NAME_MAX_LEN + 1);
                    storage_file_read(file, app->presets[i].keymap, sizeof(KeyMapping) * NUM_FLIPPER_BUTTONS);
                    app->presets[i].name[PRESET_NAME_MAX_LEN] = '\0';
                }
            } else {
                // Unknown version - treat as corrupt and use defaults
                loaded = false;
            }
            if(loaded || needs_save) {
                loaded = true;
                if(needs_save) anki_remote_save_presets(app);
            }
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(loaded) return true;

    // Fallback: legacy file, turn it into preset 0 and then save to new format.
    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    if(storage_file_open(file, KEYMAP_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        typedef struct {
            uint8_t keycode;
            uint8_t modifiers;
        } OldKeyMapping;
        OldKeyMapping legacy_map[NUM_FLIPPER_BUTTONS];
        uint8_t brightness = 255;
        if(storage_file_read(file, legacy_map, sizeof(legacy_map)) == sizeof(legacy_map)) {
            // brightness might be present
            if(storage_file_read(file, &brightness, sizeof(uint8_t)) != sizeof(uint8_t)) {
                brightness = 255;
            }
            anki_remote_set_default_presets(app);
            // Migrate old keymap to new format
            for(uint8_t j = 0; j < NUM_FLIPPER_BUTTONS; ++j) {
                app->presets[0].keymap[j].keycode = legacy_map[j].keycode;
                app->presets[0].keymap[j].modifiers = legacy_map[j].modifiers;
                app->presets[0].keymap[j].long_keycode = 0;
                app->presets[0].keymap[j].long_modifiers = 0;
            }
            app->led_brightness = brightness;
            loaded = true;
            // Save into new format
            anki_remote_save_presets(app);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(!loaded) {
        // We tried. Now we just create a default and move on.
        anki_remote_set_default_presets(app);
        anki_remote_save_presets(app);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Label & Name Helpers
// -----------------------------------------------------------------------------

static const char* get_button_name_oriented(const AnkiRemoteApp* app, FlipperButton button) {
    const bool use_vertical_names = app != NULL;
    if(use_vertical_names) {
        // ViewOrientationVerticalFlip remaps physical -> logical:
        // physical Up -> logical Left, physical Down -> logical Right,
        // physical Right -> logical Up, physical Left -> logical Down.
        // Invert that mapping so labels match the vertical holding orientation.
        switch(button) {
        case FlipperButtonUp:
            return "Right";
        case FlipperButtonDown:
            return "Left";
        case FlipperButtonLeft:
            return "Up";
        case FlipperButtonRight:
            return "Down";
        case FlipperButtonOk:
            return "OK";
        case FlipperButtonBack:
            return "Back";
        default:
            return "??";
        }
    }

    switch(button) {
    case FlipperButtonUp:
        return "Up";
    case FlipperButtonDown:
        return "Down";
    case FlipperButtonLeft:
        return "Left";
    case FlipperButtonRight:
        return "Right";
    case FlipperButtonOk:
        return "OK";
    case FlipperButtonBack:
        return "Back";
    default:
        return "??";
    }
}

// Builds a string name for a key combo (e.g. "Ctrl+Shift+A").
void get_key_combo_name(KeyMapping mapping, char* buffer, size_t buffer_size) {
    buffer[0] = '\0';

    if(mapping.keycode == 0) {
        strlcpy(buffer, "Not mapped", buffer_size);
        return;
    }

    if(mapping.modifiers & MOD_CTRL_BIT) strlcat(buffer, "Ctrl+", buffer_size);
    if(mapping.modifiers & MOD_SHIFT_BIT) strlcat(buffer, "Shift+", buffer_size);
    if(mapping.modifiers & MOD_ALT_BIT) strlcat(buffer, "Alt+", buffer_size);
    if(mapping.modifiers & MOD_GUI_BIT) strlcat(buffer, "Cmd+", buffer_size);

    const char* base_key_name = NULL;
    bool found = false;

    // Try to find a matching keyboard key definition first
    for(uint8_t y = 0; y < KEYBOARD_ROW_COUNT && !found; ++y) {
        for(uint8_t x = 0; x < KEYBOARD_COLUMN_COUNT && !found; ++x) {
            const HidKeyboardKey* key = &hid_keyboard_keyset[y][x];
            if(key->value == mapping.keycode) {
                found = true;
                if((mapping.modifiers & MOD_SHIFT_BIT) && key->shift_key != 0) {
                    char s[2] = {key->shift_key, '\0'};
                    strlcat(buffer, s, buffer_size);
                } else if(key->key != 0) {
                    char s[2] = {key->key, '\0'};
                    strlcat(buffer, s, buffer_size);
                } else {
                    // No char? fine, try to figure out something reasonable.
                    if(key->value >= HID_KEYBOARD_A && key->value <= HID_KEYBOARD_Z) {
                        char s[2] = {'A' + (key->value - HID_KEYBOARD_A), '\0'};
                        strlcat(buffer, s, buffer_size);
                    } else if(key->value >= HID_KEYBOARD_1 && key->value <= HID_KEYBOARD_9) {
                        char s[2] = {'1' + (key->value - HID_KEYBOARD_1), '\0'};
                        strlcat(buffer, s, buffer_size);
                    } else if(key->value == HID_KEYBOARD_0) {
                        strlcat(buffer, "0", buffer_size);
                    } else {
                        // Map special codes to names
                        switch(key->value) {
                        case HID_KEYBOARD_UP_ARROW: base_key_name = "Up"; break;
                        case HID_KEYBOARD_DOWN_ARROW: base_key_name = "Down"; break;
                        case HID_KEYBOARD_LEFT_ARROW: base_key_name = "Left"; break;
                        case HID_KEYBOARD_RIGHT_ARROW: base_key_name = "Right"; break;
                        case HID_KEYBOARD_SPACEBAR: base_key_name = "Space"; break;
                        case HID_KEYBOARD_RETURN: base_key_name = "Enter"; break;
                        case HID_KEYBOARD_ESCAPE: base_key_name = "Esc"; break;
                        case HID_KEYBOARD_DELETE: base_key_name = "Bsp"; break;
                        case HID_KEYBOARD_DELETE_FORWARD: base_key_name = "Del"; break;
                        case HID_KEYBOARD_TAB: base_key_name = "Tab"; break;
                        case HID_KEYBOARD_F1: base_key_name = "F1"; break;
                        case HID_KEYBOARD_F2: base_key_name = "F2"; break;
                        case HID_KEYBOARD_F3: base_key_name = "F3"; break;
                        case HID_KEYBOARD_F4: base_key_name = "F4"; break;
                        case HID_KEYBOARD_F5: base_key_name = "F5"; break;
                        case HID_KEYBOARD_F6: base_key_name = "F6"; break;
                        case HID_KEYBOARD_F7: base_key_name = "F7"; break;
                        case HID_KEYBOARD_F8: base_key_name = "F8"; break;
                        case HID_KEYBOARD_F9: base_key_name = "F9"; break;
                        case HID_KEYBOARD_F10: base_key_name = "F10"; break;
                        case HID_KEYBOARD_F11: base_key_name = "F11"; break;
                        case HID_KEYBOARD_F12: base_key_name = "F12"; break;
                        case HID_KEYBOARD_OPEN_BRACKET: base_key_name = "["; break;
                        case HID_KEYBOARD_CLOSE_BRACKET: base_key_name = "]"; break;
                        case HID_KEYBOARD_BACKSLASH: base_key_name = "\\"; break;
                        case HID_KEYBOARD_SLASH: base_key_name = "/"; break;
                        case HID_KEYBOARD_APOSTROPHE: base_key_name = "'"; break;
                        case HID_KEYBOARD_GRAVE_ACCENT: base_key_name = "`"; break;
                        case HID_KEYBOARD_EQUAL_SIGN: base_key_name = "="; break;
                        case HID_KEYBOARD_L_SHIFT: base_key_name = "Shift"; break;
                        case HID_KEYBOARD_L_CTRL: base_key_name = "Ctrl"; break;
                        case HID_KEYBOARD_L_ALT: base_key_name = "Alt"; break;
                        case HID_KEYBOARD_L_GUI: base_key_name = "Cmd"; break;
                        case HID_KEYBOARD_INSERT: base_key_name = "Ins"; break;
                        case HID_KEYBOARD_HOME: base_key_name = "Home"; break;
                        case HID_KEYBOARD_PAGE_UP: base_key_name = "PgUp"; break;
                        case HID_KEYBOARD_END: base_key_name = "End"; break;
                        case HID_KEYBOARD_PAGE_DOWN: base_key_name = "PgDn"; break;
                        case HID_KEYBOARD_PRINT_SCREEN: base_key_name = "PrtSc"; break;
                        case HID_KEYBOARD_SCROLL_LOCK: base_key_name = "ScrLk"; break;
                        case HID_KEYBOARD_PAUSE: base_key_name = "PBrk"; break;
                        case HID_KEYBOARD_SELECT: base_key_name = "Select"; break;
                        default: base_key_name = "Key"; break;
                        }
                    }
                }
            }
        }
    }

    if(base_key_name != NULL) {
        strlcat(buffer, base_key_name, buffer_size);
    }
}

// LED brightness labels
static const char* get_led_brightness_label(uint8_t brightness) {
    if(brightness == 0) return "0%";
    else if(brightness <= 64) return "25%";
    else if(brightness <= 128) return "50%";
    else if(brightness <= 192) return "75%";
    else return "100%";
}

// -----------------------------------------------------------------------------
// Preset Manager Helpers
// -----------------------------------------------------------------------------

static uint8_t preset_manager_total_items(AnkiRemoteApp* app) {
    // items: presets + Edit + Rename + Delete + Create + Save as Profile + LED brightness
    return app->preset_count + 6;
}

static void draw_preset_row(Canvas* canvas, const char* name, bool is_active, bool is_selected, uint8_t y_pos) {
    if(is_selected) {
        elements_slightly_rounded_box(canvas, 2, y_pos - 7, 124, 9);
        canvas_set_color(canvas, ColorWhite);
    }
    char line[40];
    snprintf(line, sizeof(line), "%c %s", is_active ? '*' : ' ', name);
    canvas_draw_str(canvas, 5, y_pos, line);
    canvas_set_color(canvas, ColorBlack);
}

static void delete_preset_by_index(AnkiRemoteApp* app, uint8_t idx) {
    // You cannot delete the last preset.
    if(app->preset_count <= 1) return;
    if(idx >= app->preset_count) return;

    // Shift presets down
    for(uint8_t i = idx; i + 1 < app->preset_count; ++i) {
        app->presets[i] = app->presets[i + 1];
    }
    app->preset_count--;

    // Adjust active preset
    if(app->active_preset == idx) {
        // If we deleted the active one, make preset 0 active (if it exists)
        app->active_preset = 0;
    } else if(app->active_preset > idx) {
        app->active_preset--;
    }

    // Adjust highlighted_preset
    if(app->highlighted_preset >= app->preset_count) {
        app->highlighted_preset = app->preset_count ? app->preset_count - 1 : 0;
    } else if(app->highlighted_preset > idx) {
        app->highlighted_preset--;
    }

    anki_remote_save_presets(app);
}

static void create_new_preset(AnkiRemoteApp* app) {
    if(app->preset_count >= MAX_PRESETS) return;
    char tmp_name[32];
    snprintf(tmp_name, sizeof(tmp_name), "Preset %u", (unsigned)(app->preset_count + 1));
    preset_init_empty(&app->presets[app->preset_count], tmp_name);
    app->preset_count++;
    anki_remote_save_presets(app);
}

// -----------------------------------------------------------------------------
// Rename via Text Input
// -----------------------------------------------------------------------------

static void start_text_input_for_rename(AnkiRemoteApp* app, uint8_t preset_index) {
    // Save which preset we are renaming
    app->rename_preset_index = preset_index;

    // Pre-fill buffer with current name
    strlcpy(app->tmp_name, app->presets[preset_index].name, sizeof(app->tmp_name));

    // Configure text input
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Rename Preset:");
    text_input_set_result_callback(
        app->text_input,
        preset_rename_result_cb,
        app,
        app->tmp_name,
        PRESET_NAME_MAX_LEN,
        true);

    // Switch to text input view
    view_dispatcher_switch_to_view(app->view_dispatcher, AnkiRemoteViewTextInput);
}

// Rename result callback
static void preset_rename_result_cb(void* context) {
    AnkiRemoteApp* app = context;
    uint8_t idx = app->rename_preset_index; // Use stored index (Fix 2.3 Step 3)

    // Ensure index is in range
    if(idx < app->preset_count) {
        // Empty name fallback to default
        if(app->tmp_name[0] == '\0') {
            strlcpy(app->tmp_name, "Preset", sizeof(app->tmp_name));
        }
        strlcpy(app->presets[idx].name, app->tmp_name, PRESET_NAME_MAX_LEN + 1);
        anki_remote_save_presets(app);
    }

    // Switch back to preset manager view
    view_dispatcher_switch_to_view(app->view_dispatcher, AnkiRemoteViewPresetManager);
}

// -----------------------------------------------------------------------------
// HID / BLE Helpers
// -----------------------------------------------------------------------------

// Gets called when bluetooth connects or disconnects
static void anki_remote_connection_status_callback(BtStatus status, void* context) {
    furi_assert(context);
    AnkiRemoteApp* app = context;
    app->connected = (status == BtStatusConnected);
    if(app->connected) {
        furi_hal_light_set(LightBlue, app->led_brightness);
    } else {
        furi_hal_light_set(LightBlue, 0); // Off
    }
    // ViewDispatcher will automatically trigger redraw
}

// Send or release one key combo over BLE HID
static void anki_remote_send_key_combo(AnkiRemoteApp* app, KeyMapping mapping, bool is_press) {
    if(!app || !app->ble_hid_profile) return;
    // Helper: detect "pure modifier" mapped as a tap
    bool is_pure_modifier =
        (mapping.modifiers == 0) &&
        (mapping.keycode == HID_KEYBOARD_L_CTRL ||
         mapping.keycode == HID_KEYBOARD_L_SHIFT ||
         mapping.keycode == HID_KEYBOARD_L_ALT  ||
         mapping.keycode == HID_KEYBOARD_L_GUI);

    if(is_press) {
        if(is_pure_modifier) {
            // Press only the modifier
            if(mapping.keycode == HID_KEYBOARD_L_CTRL)
                ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_CTRL);
            else if(mapping.keycode == HID_KEYBOARD_L_SHIFT)
                ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_SHIFT);
            else if(mapping.keycode == HID_KEYBOARD_L_ALT)
                ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_ALT);
            else if(mapping.keycode == HID_KEYBOARD_L_GUI)
                ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_GUI);
            return;
        }

        // Regular combo: press modifiers first
        if(mapping.modifiers & MOD_CTRL_BIT)  ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_CTRL);
        if(mapping.modifiers & MOD_SHIFT_BIT) ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_SHIFT);
        if(mapping.modifiers & MOD_ALT_BIT)   ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_ALT);
        if(mapping.modifiers & MOD_GUI_BIT)   ble_profile_hid_kb_press(app->ble_hid_profile, KEY_MOD_LEFT_GUI);

        // Then press the main key (skip if the "key" itself is a modifier)
        if(mapping.keycode != HID_KEYBOARD_L_CTRL &&
           mapping.keycode != HID_KEYBOARD_L_SHIFT &&
           mapping.keycode != HID_KEYBOARD_L_ALT  &&
           mapping.keycode != HID_KEYBOARD_L_GUI) {
            ble_profile_hid_kb_press(app->ble_hid_profile, mapping.keycode);
        }
    } else {
        // Release all keys.
        ble_profile_hid_kb_release_all(app->ble_hid_profile);
    }
}

// -----------------------------------------------------------------------------
// View: Menu
// -----------------------------------------------------------------------------

static void anki_remote_view_menu_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "drgo.re's anki remote");
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < MENU_OPTIONS_COUNT; ++i) {
        if(i == app->menu_state.selected_item) {
            elements_slightly_rounded_box(canvas, 10, 17 + (i * 15), 108, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        if(i == 0) canvas_draw_str(canvas, 15, 28, "Start");
        if(i == 1) canvas_draw_str(canvas, 15, 43, "Manage Presets");
        if(i == 2) canvas_draw_str(canvas, 15, 58, "Credits");
        canvas_set_color(canvas, ColorBlack);
    }
}

static bool anki_remote_view_menu_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool updated = false;
    if(event->key == InputKeyUp) {
        app->menu_state.selected_item =
            (app->menu_state.selected_item + MENU_OPTIONS_COUNT - 1) % MENU_OPTIONS_COUNT;
        updated = true;
    } else if(event->key == InputKeyDown) {
        app->menu_state.selected_item = (app->menu_state.selected_item + 1) % MENU_OPTIONS_COUNT;
        updated = true;
    } else if(event->key == InputKeyOk) {
        if(app->menu_state.selected_item == 0) {
            anki_remote_switch_to_view(app, AnkiRemoteViewController); // Start
        } else if(app->menu_state.selected_item == 1) {
            anki_remote_switch_to_view(app, AnkiRemoteViewPresetManager); // Manage Presets
        } else if(app->menu_state.selected_item == 2) {
            anki_remote_switch_to_view(app, AnkiRemoteViewCredits);
        }
    } else if(event->key == InputKeyBack && event->type == InputTypeShort) {
        view_dispatcher_stop(app->view_dispatcher);
    }
    if(updated) {
        view_commit_model(app->views[AnkiRemoteViewMenu], true);
    }
    return true;
}

// -----------------------------------------------------------------------------
// View: Controller
// -----------------------------------------------------------------------------

// D-pad arrow art - exact copy from Flipper Zero HID app
static void hid_keynote_draw_arrow(Canvas* canvas, uint8_t x, uint8_t y, CanvasDirection dir) {
    canvas_draw_triangle(canvas, x, y, 5, 3, dir);
    if(dir == CanvasDirectionBottomToTop) {
        canvas_draw_line(canvas, x, y + 6, x, y - 1);
    } else if(dir == CanvasDirectionTopToBottom) {
        canvas_draw_line(canvas, x, y - 6, x, y + 1);
    } else if(dir == CanvasDirectionRightToLeft) {
        canvas_draw_line(canvas, x + 6, y, x - 1, y);
    } else if(dir == CanvasDirectionLeftToRight) {
        canvas_draw_line(canvas, x - 6, y, x + 1, y);
    }
}

static void anki_remote_view_controller_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);

    // Defensive: some firmwares/builds may not swap the canvas dimensions as expected after
    // orientation changes. Choose a layout that fits the current canvas to prevent OOB draws.
    const size_t cw = canvas_width(canvas);
    const size_t ch = canvas_height(canvas);

    // Vertical canvas (Keynote Vertical, stock HID layout)
    if((cw < ch) && (cw >= 64) && (ch >= 128)) {
        canvas_draw_icon(
            canvas,
            0,
            0,
            app->connected ? &I_Ble_connected_15x15 : &I_Ble_disconnected_15x15);
        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_aligned(canvas, 20, 3, AlignLeft, AlignTop, "Anki");

        canvas_draw_icon(canvas, 2, 18, &I_Pin_back_arrow_10x8);
        canvas_set_font(canvas, FontSecondary);
        elements_multiline_text_aligned(canvas, 15, 19, AlignLeft, AlignTop, "Hold to exit");

        const uint8_t x_2 = 23;
        const uint8_t x_1 = 2;
        const uint8_t x_3 = 44;

        const uint8_t y_1 = 44;
        const uint8_t y_2 = 65;

        // Up
        canvas_draw_icon(canvas, x_2, y_1, &I_Button_18x18);
        if(app->controller_state.up_pressed) {
            elements_slightly_rounded_box(canvas, x_2 + 3, y_1 + 2, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, x_2 + 9, y_1 + 6, CanvasDirectionBottomToTop);
        canvas_set_color(canvas, ColorBlack);

        // Down
        canvas_draw_icon(canvas, x_2, y_2, &I_Button_18x18);
        if(app->controller_state.down_pressed) {
            elements_slightly_rounded_box(canvas, x_2 + 3, y_2 + 2, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, x_2 + 9, y_2 + 10, CanvasDirectionTopToBottom);
        canvas_set_color(canvas, ColorBlack);

        // Left
        canvas_draw_icon(canvas, x_1, y_2, &I_Button_18x18);
        if(app->controller_state.left_pressed) {
            elements_slightly_rounded_box(canvas, x_1 + 3, y_2 + 2, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, x_1 + 7, y_2 + 8, CanvasDirectionRightToLeft);
        canvas_set_color(canvas, ColorBlack);

        // Right
        canvas_draw_icon(canvas, x_3, y_2, &I_Button_18x18);
        if(app->controller_state.right_pressed) {
            elements_slightly_rounded_box(canvas, x_3 + 3, y_2 + 2, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, x_3 + 11, y_2 + 8, CanvasDirectionLeftToRight);
        canvas_set_color(canvas, ColorBlack);

        // Ok (wide button) - exact Keynote Vertical position
        const uint8_t ok_y = 86;
        canvas_draw_icon(canvas, 2, ok_y, &I_Space_60x18);
        if(app->controller_state.ok_pressed) {
            elements_slightly_rounded_box(canvas, 5, ok_y + 2, 55, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_icon(canvas, 11, ok_y + 4, &I_Ok_btn_9x9);
        elements_multiline_text_aligned(canvas, 26, ok_y + 12, AlignLeft, AlignBottom, "Space");
        canvas_set_color(canvas, ColorBlack);

        // Back (wide button) - exact Keynote Vertical position
        const uint8_t back_y = 107;
        canvas_draw_icon(canvas, 2, back_y, &I_Space_60x18);
        if(app->controller_state.back_pressed) {
            elements_slightly_rounded_box(canvas, 5, back_y + 2, 55, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_icon(canvas, 11, back_y + 4, &I_Pin_back_arrow_10x8);
        elements_multiline_text_aligned(canvas, 26, back_y + 12, AlignLeft, AlignBottom, "Back");
        canvas_set_color(canvas, ColorBlack);
    } else if((cw >= 128) && (ch >= 64)) {
        // Horizontal canvas (Keynote, stock HID layout)
        canvas_draw_icon(
            canvas,
            0,
            0,
            app->connected ? &I_Ble_connected_15x15 : &I_Ble_disconnected_15x15);

        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_aligned(canvas, 17, 3, AlignLeft, AlignTop, "Anki");

        canvas_draw_icon(canvas, 68, 2, &I_Pin_back_arrow_10x8);
        canvas_set_font(canvas, FontSecondary);
        elements_multiline_text_aligned(canvas, 127, 3, AlignRight, AlignTop, "Hold to exit");

        // Up
        canvas_draw_icon(canvas, 21, 24, &I_Button_18x18);
        if(app->controller_state.up_pressed) {
            elements_slightly_rounded_box(canvas, 24, 26, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, 30, 30, CanvasDirectionBottomToTop);
        canvas_set_color(canvas, ColorBlack);

        // Down
        canvas_draw_icon(canvas, 21, 45, &I_Button_18x18);
        if(app->controller_state.down_pressed) {
            elements_slightly_rounded_box(canvas, 24, 47, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, 30, 55, CanvasDirectionTopToBottom);
        canvas_set_color(canvas, ColorBlack);

        // Left
        canvas_draw_icon(canvas, 0, 45, &I_Button_18x18);
        if(app->controller_state.left_pressed) {
            elements_slightly_rounded_box(canvas, 3, 47, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, 7, 53, CanvasDirectionRightToLeft);
        canvas_set_color(canvas, ColorBlack);

        // Right
        canvas_draw_icon(canvas, 42, 45, &I_Button_18x18);
        if(app->controller_state.right_pressed) {
            elements_slightly_rounded_box(canvas, 45, 47, 13, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        hid_keynote_draw_arrow(canvas, 53, 53, CanvasDirectionLeftToRight);
        canvas_set_color(canvas, ColorBlack);

        // Ok
        canvas_draw_icon(canvas, 63, 24, &I_Space_65x18);
        if(app->controller_state.ok_pressed) {
            elements_slightly_rounded_box(canvas, 66, 26, 60, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_icon(canvas, 74, 28, &I_Ok_btn_9x9);
        elements_multiline_text_aligned(canvas, 91, 36, AlignLeft, AlignBottom, "Space");
        canvas_set_color(canvas, ColorBlack);

        // Back
        canvas_draw_icon(canvas, 63, 45, &I_Space_65x18);
        if(app->controller_state.back_pressed) {
            elements_slightly_rounded_box(canvas, 66, 47, 60, 13);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_icon(canvas, 74, 49, &I_Pin_back_arrow_10x8);
        elements_multiline_text_aligned(canvas, 91, 57, AlignLeft, AlignBottom, "Back");
        canvas_set_color(canvas, ColorBlack);
    } else {
        // Fallback: shouldn't happen, but keep it safe.
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 12, "Anki Remote");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, "Bad canvas size");
    }
}


static bool anki_remote_view_controller_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;

    // Back: long press always exits (no mapping).
    // Short press is mapped only when connected.
    if(event->key == InputKeyBack) {
        if(event->type == InputTypeLong || (!app->connected && event->type == InputTypeShort)) {
            app->controller_state.ignore_next_back_release = true;
            anki_remote_switch_to_view(app, AnkiRemoteViewMenu);
            return true;
        }
    }

    // Handle long press - send long press key press (hold) and store for release
    if(event->type == InputTypeLong) {
        KeyMapping long_mapping = (KeyMapping){0};
        bool* long_sent_ptr = NULL;
        KeyMapping* long_mapping_ptr = NULL;
        bool key_event = true;
        switch(event->key) {
        case InputKeyUp:
            long_mapping = app->presets[app->active_preset].keymap[FlipperButtonUp];
            long_sent_ptr = &app->controller_state.up_long_sent;
            long_mapping_ptr = &app->controller_state.up_long_mapping;
            break;
        case InputKeyDown:
            long_mapping = app->presets[app->active_preset].keymap[FlipperButtonDown];
            long_sent_ptr = &app->controller_state.down_long_sent;
            long_mapping_ptr = &app->controller_state.down_long_mapping;
            break;
        case InputKeyLeft:
            long_mapping = app->presets[app->active_preset].keymap[FlipperButtonLeft];
            long_sent_ptr = &app->controller_state.left_long_sent;
            long_mapping_ptr = &app->controller_state.left_long_mapping;
            break;
        case InputKeyRight:
            long_mapping = app->presets[app->active_preset].keymap[FlipperButtonRight];
            long_sent_ptr = &app->controller_state.right_long_sent;
            long_mapping_ptr = &app->controller_state.right_long_mapping;
            break;
        case InputKeyOk:
            long_mapping = app->presets[app->active_preset].keymap[FlipperButtonOk];
            long_sent_ptr = &app->controller_state.ok_long_sent;
            long_mapping_ptr = &app->controller_state.ok_long_mapping;
            break;
        case InputKeyBack:
            key_event = false; // Back long-press always exits; no mapping
            break;
        default:
            key_event = false;
            break;
        }
        // Send long press key if configured - press only (hold)
        if(key_event && long_mapping.long_keycode != 0) {
            if(long_sent_ptr) *long_sent_ptr = true;
            if(long_mapping_ptr) *long_mapping_ptr = long_mapping;
            KeyMapping temp = {
                .keycode = long_mapping.long_keycode,
                .modifiers = long_mapping.long_modifiers,
                .long_keycode = 0,
                .long_modifiers = 0
            };
            anki_remote_send_key_combo(app, temp, true);  // Press only - keep held
        }
        return true;
    }

    // Handle press event - update visual state
    if(event->type == InputTypePress) {
        switch(event->key) {
        case InputKeyUp:
            app->controller_state.up_pressed = true;
            app->controller_state.up_long_sent = false;
            break;
        case InputKeyDown:
            app->controller_state.down_pressed = true;
            app->controller_state.down_long_sent = false;
            break;
        case InputKeyLeft:
            app->controller_state.left_pressed = true;
            app->controller_state.left_long_sent = false;
            break;
        case InputKeyRight:
            app->controller_state.right_pressed = true;
            app->controller_state.right_long_sent = false;
            break;
        case InputKeyOk:
            app->controller_state.ok_pressed = true;
            app->controller_state.ok_long_sent = false;
            break;
        case InputKeyBack:
            app->controller_state.back_pressed = true;
            app->controller_state.back_long_sent = false;
            break;
        default:
            break;
        }
        view_commit_model(app->views[AnkiRemoteViewController], true);
        return true;
    }

    // Handle release event
    if(event->type == InputTypeRelease) {
        if(event->key == InputKeyBack && app->controller_state.ignore_next_back_release) {
            app->controller_state.ignore_next_back_release = false;
            return true;
        }
        KeyMapping mapping = (KeyMapping){0};
        bool* long_sent_ptr = NULL;
        KeyMapping* long_mapping_ptr = NULL;
        bool key_event = true;
        switch(event->key) {
        case InputKeyUp:
            app->controller_state.up_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonUp];
            long_sent_ptr = &app->controller_state.up_long_sent;
            long_mapping_ptr = &app->controller_state.up_long_mapping;
            break;
        case InputKeyDown:
            app->controller_state.down_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonDown];
            long_sent_ptr = &app->controller_state.down_long_sent;
            long_mapping_ptr = &app->controller_state.down_long_mapping;
            break;
        case InputKeyLeft:
            app->controller_state.left_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonLeft];
            long_sent_ptr = &app->controller_state.left_long_sent;
            long_mapping_ptr = &app->controller_state.left_long_mapping;
            break;
        case InputKeyRight:
            app->controller_state.right_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonRight];
            long_sent_ptr = &app->controller_state.right_long_sent;
            long_mapping_ptr = &app->controller_state.right_long_mapping;
            break;
        case InputKeyOk:
            app->controller_state.ok_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonOk];
            long_sent_ptr = &app->controller_state.ok_long_sent;
            long_mapping_ptr = &app->controller_state.ok_long_mapping;
            break;
        case InputKeyBack:
            app->controller_state.back_pressed = false;
            mapping = app->presets[app->active_preset].keymap[FlipperButtonBack];
            long_sent_ptr = &app->controller_state.back_long_sent;
            long_mapping_ptr = &app->controller_state.back_long_mapping;
            break;
        default:
            key_event = false;
            break;
        }
        bool long_was_sent = long_sent_ptr && *long_sent_ptr;
        if(key_event) {
            if(long_was_sent && long_mapping_ptr && long_mapping_ptr->long_keycode != 0) {
                // Release the long press key
                KeyMapping temp = {
                    .keycode = long_mapping_ptr->long_keycode,
                    .modifiers = long_mapping_ptr->long_modifiers,
                    .long_keycode = 0,
                    .long_modifiers = 0
                };
                anki_remote_send_key_combo(app, temp, false);  // Release
            } else if (!long_was_sent && mapping.keycode != 0) {
                // Short press - send press and release
                anki_remote_send_key_combo(app, mapping, true);
                anki_remote_send_key_combo(app, mapping, false);
            }
        }
        // Reset the long_sent flag and mapping
        if(long_sent_ptr) *long_sent_ptr = false;
        if(long_mapping_ptr) long_mapping_ptr->long_keycode = 0;
        view_commit_model(app->views[AnkiRemoteViewController], true);
    }
    return true;
}

// -----------------------------------------------------------------------------
// View: Preset Manager
// -----------------------------------------------------------------------------

static void anki_remote_view_preset_manager_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Manage Presets");
    canvas_set_font(canvas, FontSecondary);

    uint8_t total = preset_manager_total_items(app);
    uint8_t y_start = 19;
    uint8_t y_spacing = 10;
    // How many rows fit?
    const uint8_t visible_items = ((64 - y_start) / y_spacing) + 1;
    
    for(uint8_t i = 0; i < visible_items; ++i) {
        uint8_t item_index = app->preset_manager_state.scroll_offset + i;
        if(item_index >= total) break;
        uint8_t y_pos = y_start + (i * y_spacing);
        bool is_selected = (item_index == app->preset_manager_state.selected_item);

        if(item_index < app->preset_count) {
            bool is_active = (item_index == app->active_preset);
            draw_preset_row(canvas, app->presets[item_index].name, is_active, is_selected, y_pos);
        } else {
            // action lines
            if(is_selected) {
                elements_slightly_rounded_box(canvas, 2, y_pos - 7, 124, 9);
                canvas_set_color(canvas, ColorWhite);
            }
            if(item_index == app->preset_count) {
                canvas_draw_str(canvas, 5, y_pos, "Edit Selected");
            } else if(item_index == app->preset_count + 1) {
                canvas_draw_str(canvas, 5, y_pos, "Rename Selected");
            } else if(item_index == app->preset_count + 2) {
                // Indicate if delete is disabled for single-preset setups
                if(app->preset_count <= 1) {
                    canvas_draw_str(canvas, 5, y_pos, "Delete Selected (disabled)");
                } else {
                    canvas_draw_str(canvas, 5, y_pos, "Delete Selected");
                }
            } else if(item_index == app->preset_count + 3) {
                canvas_draw_str(canvas, 5, y_pos, "Create Preset");
            } else if(item_index == app->preset_count + 4) {
                canvas_draw_str(canvas, 5, y_pos, "Save as Profile");
            } else if(item_index == app->preset_count + 5) {
                // LED brightness line
                const char* percent_str = get_led_brightness_label(app->led_brightness);
                const char* label = "LED Brightness:";
                canvas_draw_str(canvas, 5, y_pos, label);

                uint8_t char_width = 5;
                uint8_t label_width = strlen(label) * char_width;
                uint8_t end_of_label = 5 + label_width;
                uint8_t right_edge = 123;
                uint8_t available = right_edge - end_of_label;
                uint8_t percent_len = strlen(percent_str);
                uint8_t percent_width = percent_len * char_width;
                if(is_selected) {
                    uint8_t arrow_width = char_width;
                    uint8_t total_width = arrow_width + 1 + percent_width + 6 + arrow_width;
                    uint8_t start_x = end_of_label + (available - total_width) / 2 + 1;
                    canvas_draw_str(canvas, start_x, y_pos, "<");
                    canvas_draw_str(canvas, start_x + arrow_width + 1, y_pos, percent_str);
                    canvas_draw_str(canvas, start_x + arrow_width + 1 + percent_width + 6, y_pos, ">");
                } else {
                    uint8_t start_x = end_of_label + (available - percent_width) / 2 + 1;
                    canvas_draw_str(canvas, start_x, y_pos, percent_str);
                }
            }
            canvas_set_color(canvas, ColorBlack);
        }
    }

    elements_scrollbar(canvas, app->preset_manager_state.selected_item, total);
}

static bool anki_remote_view_preset_manager_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;

    if(app->ignore_next_ok_release && event->key == InputKeyOk &&
       (event->type == InputTypeShort || event->type == InputTypeRelease)) {
        app->ignore_next_ok_release = false;
        return false;
    }

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    uint8_t total_items = preset_manager_total_items(app);
    uint8_t selected_item = app->preset_manager_state.selected_item;

    if(event->key == InputKeyUp) {
        selected_item = (selected_item + total_items - 1) % total_items; // BUG FIX: Corrected calculation for wrap-around
    } else if(event->key == InputKeyDown) {
        selected_item = (selected_item + 1) % total_items;
    } else if(event->key == InputKeyBack) {
        anki_remote_switch_to_view(app, AnkiRemoteViewMenu);
        return true;
    } else if(event->key == InputKeyOk) {
        // Activate preset if a preset row is selected
        if(selected_item < app->preset_count) {
            app->active_preset = selected_item;
            app->highlighted_preset = selected_item;
            anki_remote_save_presets(app);
        } else if(selected_item == app->preset_count) {
            // Edit Selected: edits the active preset (the one with the star)
            if(app->active_preset < app->preset_count) {
                app->preset_edit_menu_state.editing_preset_index = app->active_preset;
                anki_remote_switch_to_view(app, AnkiRemoteViewPresetEditMenu);
            }
        } else if(selected_item == app->preset_count + 1) {
            // Rename Selected: rename the active preset
            if(app->active_preset < app->preset_count) {
                start_text_input_for_rename(app, app->active_preset);
                // We stay in this scene; rendering will be handled by dispatcher until callback.
            }
        } else if(selected_item == app->preset_count + 2) {
            // Delete active preset (if allowed)
            if(app->preset_count > 1 && app->active_preset < app->preset_count) {
                delete_preset_by_index(app, app->active_preset);
            }
        } else if(selected_item == app->preset_count + 3) {
            // Create preset
            create_new_preset(app);
        } else if(selected_item == app->preset_count + 4) {
            // Save as Profile - duplicate current preset to new slot
            if(app->preset_count < MAX_PRESETS) {
                uint8_t new_idx = app->preset_count;
                app->presets[new_idx] = app->presets[app->active_preset];
                char temp_name[PRESET_NAME_MAX_LEN + 1];
                snprintf(
                    temp_name,
                    sizeof(temp_name),
                    "%s_copy",
                    app->presets[app->active_preset].name);
                strlcpy(app->presets[new_idx].name, temp_name, PRESET_NAME_MAX_LEN + 1);
                app->preset_count++;
                anki_remote_save_presets(app);
            }
        } else if(selected_item == app->preset_count + 5) {
            // LED brightness, handled via left/right
        }
    } else if(selected_item == (app->preset_count + 5)) {
        // LED brightness adjustments
        if(event->key == InputKeyLeft) {
            if(app->led_brightness == 0) app->led_brightness = 255;
            else if(app->led_brightness == 64) app->led_brightness = 0;
            else if(app->led_brightness == 128) app->led_brightness = 64;
            else if(app->led_brightness == 192) app->led_brightness = 128;
            else app->led_brightness = 192;
            anki_remote_save_presets(app);
            if(app->connected) furi_hal_light_set(LightBlue, app->led_brightness);
        } else if(event->key == InputKeyRight) {
            if(app->led_brightness == 0) app->led_brightness = 64;
            else if(app->led_brightness == 64) app->led_brightness = 128;
            else if(app->led_brightness == 128) app->led_brightness = 192;
            else if(app->led_brightness == 192) app->led_brightness = 255;
            else app->led_brightness = 0;
            anki_remote_save_presets(app);
            if(app->connected) furi_hal_light_set(LightBlue, app->led_brightness);
        }
    }

    // Update highlighted preset when the selection lands on a preset row
    if(selected_item < app->preset_count) {
        app->highlighted_preset = selected_item;
    }

    // Scrolling
    uint8_t y_start = 19;
    uint8_t y_spacing = 10;
    const uint8_t visible_items = ((64 - y_start) / y_spacing) + 1;

    if(selected_item < app->preset_manager_state.scroll_offset) {
        app->preset_manager_state.scroll_offset = selected_item;
    } else if(selected_item >= app->preset_manager_state.scroll_offset + visible_items) {
        app->preset_manager_state.scroll_offset = selected_item - visible_items + 1;
    }

    app->preset_manager_state.selected_item = selected_item;
    return true;
}

// -----------------------------------------------------------------------------
// Scene Drawing: Preset Edit Menu (per preset mappings)
// -----------------------------------------------------------------------------

static void anki_remote_reset_keymap_only_for_preset(Preset* preset) {
    for(uint8_t i = 0; i < NUM_FLIPPER_BUTTONS; ++i) {
        preset->keymap[i] = (KeyMapping){.keycode = 0, .modifiers = 0, .long_keycode = 0, .long_modifiers = 0};
    }
}

static void anki_remote_view_preset_edit_menu_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    char header[40];
    snprintf(header, sizeof(header), "Edit: %s", app->presets[app->preset_edit_menu_state.editing_preset_index].name);
    canvas_draw_str(canvas, 2, 10, header);
    canvas_set_font(canvas, FontSecondary);

    const uint8_t total_items = NUM_FLIPPER_BUTTONS + 1; // 6 buttons + Reset
    uint8_t y_start = 19;
    uint8_t y_spacing = 10;
    const uint8_t visible_items = ((64 - y_start) / y_spacing) + 1;

    for(uint8_t i = 0; i < visible_items; ++i) {
        uint8_t item_index = app->preset_edit_menu_state.scroll_offset + i;
        if(item_index >= total_items) break;

        uint8_t y_pos = y_start + (i * y_spacing);
        char label[64];

        bool is_selected = (item_index == app->preset_edit_menu_state.selected_item);

        if(item_index < NUM_FLIPPER_BUTTONS) {
            // Show both short and long press mappings
            KeyMapping map = app->presets[app->preset_edit_menu_state.editing_preset_index].keymap[item_index];
            char short_buf[16], long_buf[16];

            // Get short press name
            if(map.keycode != 0) {
                KeyMapping temp = {.keycode = map.keycode, .modifiers = map.modifiers};
                get_key_combo_name(temp, short_buf, sizeof(short_buf));
            } else {
                strlcpy(short_buf, "-", sizeof(short_buf));
            }

            // Get long press name
            if(map.long_keycode != 0) {
                KeyMapping temp = {.keycode = map.long_keycode, .modifiers = map.long_modifiers};
                get_key_combo_name(temp, long_buf, sizeof(long_buf));
            } else {
                strlcpy(long_buf, "-", sizeof(long_buf));
            }

            // Format: "Btn: short/long"
            snprintf(
                label,
                sizeof(label),
                "%s: %s/%s",
                get_button_name_oriented(app, item_index),
                short_buf,
                long_buf);
        } else {
            strlcpy(label, "Reset Mappings", sizeof(label));
        }

        if(is_selected) {
            elements_slightly_rounded_box(canvas, 2, y_pos - 7, 124, 9);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_draw_str(canvas, 5, y_pos, label);
        canvas_set_color(canvas, ColorBlack);
    }

    elements_scrollbar(canvas, app->preset_edit_menu_state.selected_item, total_items);
}

static bool anki_remote_view_preset_edit_menu_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;

    if(app->ignore_next_ok_release && event->key == InputKeyOk &&
       (event->type == InputTypeShort || event->type == InputTypeRelease)) {
        app->ignore_next_ok_release = false;
        return false;
    }
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    const uint8_t total_items = NUM_FLIPPER_BUTTONS + 1;
    uint8_t selected_item = app->preset_edit_menu_state.selected_item;

    if(event->key == InputKeyUp) {
        selected_item = (selected_item + total_items - 1) % total_items; // BUG FIX: Corrected calculation for wrap-around
    } else if(event->key == InputKeyDown) {
        selected_item = (selected_item + 1) % total_items;
    } else if(event->key == InputKeyBack) {
        anki_remote_switch_to_view(app, AnkiRemoteViewPresetManager);
        return true;
    } else if(event->key == InputKeyOk) {
        if(selected_item < NUM_FLIPPER_BUTTONS) {
            app->settings_state.configuring_button = selected_item;
            app->settings_state.editing_preset_index = app->preset_edit_menu_state.editing_preset_index;
            anki_remote_switch_to_view(app, AnkiRemoteViewKeyPicker);
        } else {
            // Reset only mappings (not brightness)
            anki_remote_reset_keymap_only_for_preset(&app->presets[app->preset_edit_menu_state.editing_preset_index]);
            anki_remote_save_presets(app);
        }
    }

    // Scrolling the edit menu
    uint8_t y_start = 19;
    uint8_t y_spacing = 10;
    const uint8_t visible_items = ((64 - y_start) / y_spacing) + 1;

    if(selected_item < app->preset_edit_menu_state.scroll_offset) {
        app->preset_edit_menu_state.scroll_offset = selected_item;
    } else if(selected_item >= app->preset_edit_menu_state.scroll_offset + visible_items) {
        app->preset_edit_menu_state.scroll_offset = selected_item - visible_items + 1;
    }
    app->preset_edit_menu_state.selected_item = selected_item;
    return true;
}

// -----------------------------------------------------------------------------
// Scene Drawing: Keyboard Picker
// -----------------------------------------------------------------------------

// Goofy ahh key drawing algorithm from BT remote
static void hid_keyboard_draw_key(
    Canvas* canvas,
    const HidKeyboardKey* key,
    uint8_t x,
    uint8_t y,
    bool selected,
    const struct AnkiRemoteApp* app) {
    if(!key->width) return;
#define MARGIN_TOP 12
#define MARGIN_LEFT 3
#define KEY_WIDTH 11
#define KEY_HEIGHT 13
#define KEY_PADDING -1
    canvas_set_color(canvas, ColorBlack);
    uint8_t key_width = KEY_WIDTH * key->width + KEY_PADDING * (key->width - 1);
    uint8_t px = MARGIN_LEFT + x * (KEY_WIDTH + KEY_PADDING);
    uint8_t py = MARGIN_TOP + y * (KEY_HEIGHT + KEY_PADDING);

    if(selected) {
        elements_slightly_rounded_box(canvas, px, py, key_width, KEY_HEIGHT);
        canvas_set_color(canvas, ColorWhite);
    } else {
        elements_slightly_rounded_frame(canvas, px, py, key_width, KEY_HEIGHT);
    }
    bool shift = app->settings_state.shift_pressed;
    bool is_toggled =
        (key->value == HID_KEYBOARD_L_SHIFT && app->settings_state.shift_pressed) ||
        (key->value == HID_KEYBOARD_L_CTRL && app->settings_state.ctrl_pressed) ||
        (key->value == HID_KEYBOARD_L_ALT && app->settings_state.alt_pressed) ||
        (key->value == HID_KEYBOARD_L_GUI && app->settings_state.gui_pressed);
    // toggled icon?
    if(is_toggled && key->icon_toggled) {
        const Icon* icon = key->icon_toggled;
        canvas_draw_icon(
            canvas,
            px + key_width / 2 - icon->width / 2,
            py + KEY_HEIGHT / 2 - icon->height / 2,
            icon);
        return;
    }
    // shift applied icons / chars
    if(shift) {
        if(key->icon_shift) {
            const Icon* icon = key->icon_shift;
            canvas_draw_icon(
                canvas,
                px + key_width / 2 - icon->width / 2,
                py + KEY_HEIGHT / 2 - icon->height / 2,
                icon);
            return;
        }
        if(key->shift_key != 0) {
            char text[2] = {key->shift_key, '\0'};
            canvas_draw_str_aligned(
                canvas,
                px + key_width / 2 + 1,
                py + KEY_HEIGHT / 2 + 1,
                AlignCenter,
                AlignCenter,
                text);
            return;
        }
    }

    // normal icon
    if(key->icon) {
        const Icon* icon = key->icon;
        canvas_draw_icon(
            canvas,
            px + key_width / 2 - icon->width / 2,
            py + KEY_HEIGHT / 2 - icon->height / 2,
            icon);
        return;
    }

    // text label (for sys keys, nav keys, etc.)
    if(key->label) {
        canvas_set_font(canvas, FontKeyboard);
        canvas_draw_str_aligned(
            canvas,
            px + key_width / 2,
            py + KEY_HEIGHT / 2 + 1,
            AlignCenter,
            AlignCenter,
            key->label);
        return;
    }

    // normal character
    if(key->key != 0) {
        char text[2] = {key->key, '\0'};
        canvas_draw_str_aligned(
            canvas,
            px + key_width / 2 + 1,
            py + KEY_HEIGHT / 2 + 1,
            AlignCenter,
            AlignCenter,
            text);
        return;
    }
}

static void anki_remote_view_key_picker_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    // Show button name, preset name, and whether editing short or long press
    char header_str[48];
    snprintf(
        header_str,
        sizeof(header_str),
        "%s: %s (%s) [%s]",
        get_button_name_oriented(app, app->settings_state.configuring_button),
        app->settings_state.editing_long_press ? "LONG" : "SHORT",
        app->presets[app->settings_state.editing_preset_index].name,
        app->settings_state.editing_long_press ? "hold OK" : "hold OK");
    canvas_draw_str(canvas, 2, 10, header_str);

    // Show current mapping for the selected mode
    char current_str[32];
    KeyMapping current_map = app->presets[app->settings_state.editing_preset_index]
        .keymap[app->settings_state.configuring_button];
    uint8_t current_keycode = app->settings_state.editing_long_press ?
        current_map.long_keycode : current_map.keycode;
    uint8_t current_mods = app->settings_state.editing_long_press ?
        current_map.long_modifiers : current_map.modifiers;

    if(current_keycode != 0) {
        KeyMapping temp = {.keycode = current_keycode, .modifiers = current_mods};
        get_key_combo_name(temp, current_str, sizeof(current_str));
    } else {
        strlcpy(current_str, "Not mapped", sizeof(current_str));
    }
    canvas_draw_str(canvas, 2, 62, current_str);

    canvas_set_font(canvas, FontKeyboard);
    uint8_t start_row = (app->settings_state.y > 3) ? (app->settings_state.y - 3) : 0;
    for(uint8_t y = start_row; y < KEYBOARD_ROW_COUNT; y++) {
        uint8_t current_x = 0;
        for(uint8_t x_idx = 0; x_idx < KEYBOARD_COLUMN_COUNT; x_idx++) {
            const HidKeyboardKey* key = &hid_keyboard_keyset[y][x_idx];
            if(key->width > 0) {
                bool is_selected = (app->settings_state.y == y && app->settings_state.x == x_idx);
                hid_keyboard_draw_key(canvas, key, current_x, y - start_row, is_selected, app);
                current_x += key->width;
            }
        }
    }
    elements_scrollbar(canvas, app->settings_state.y, KEYBOARD_ROW_COUNT);
}

// Move the keyboard picker cursor by dx/dy Calc BC ahh algorithm
static void settings_move_cursor(AnkiRemoteApp* app, int8_t dx, int8_t dy) {
    int current_x = app->settings_state.x;
    int current_y = app->settings_state.y;
    if(dy != 0) {
        current_y += dy;
        if(current_y < 0) current_y = KEYBOARD_ROW_COUNT - 1;
        if(current_y >= KEYBOARD_ROW_COUNT) current_y = 0;
    }
    if(dx != 0) {
        current_x += dx;
        while(true) {
            if(current_x < 0) current_x = KEYBOARD_COLUMN_COUNT - 1;
            if(current_x >= KEYBOARD_COLUMN_COUNT) current_x = 0;
            if(hid_keyboard_keyset[current_y][current_x].width != 0) break;
            current_x += dx > 0 ? 1 : -1;
        }
    }

    // In case we land on a zero-width void, try left then right until it works.
    if(hid_keyboard_keyset[current_y][current_x].width == 0) {
        int temp_x = current_x;
        while(temp_x >= 0 &&
              hid_keyboard_keyset[current_y][temp_x].width == 0) {
            temp_x--;
        }

        if(temp_x >= 0) {
            current_x = temp_x;
        } else {
            temp_x = current_x;
            while(temp_x < KEYBOARD_COLUMN_COUNT &&
                  hid_keyboard_keyset[current_y][temp_x].width == 0) {
                temp_x++;
            }
            if(temp_x < KEYBOARD_COLUMN_COUNT)
                current_x = temp_x;
        }
    }
    app->settings_state.x = (uint8_t)current_x;
    app->settings_state.y = (uint8_t)current_y;
}

static bool anki_remote_view_key_picker_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort || event->type == InputTypeLong) {
            anki_remote_switch_to_view(app, AnkiRemoteViewPresetEditMenu);
            return true;
        }
    }

    // Long press OK toggles between short and long press editing mode
    if(event->key == InputKeyOk && event->type == InputTypeLong) {
        app->settings_state.editing_long_press = !app->settings_state.editing_long_press;
        // View will be automatically updated by ViewDispatcher
        return true;
    }

    if(event->key == InputKeyOk) {
        if(event->type != InputTypePress) return false;
    } else {
        if(event->type != InputTypePress && event->type != InputTypeRepeat) return false;
    }

    // Arrow key presses
    if(event->key == InputKeyUp) {
        settings_move_cursor(app, 0, -1);
        return true;
    } else if(event->key == InputKeyDown) {
        settings_move_cursor(app, 0, 1);
        return true;
    } else if(event->key == InputKeyLeft) {
        settings_move_cursor(app, -1, 0);
        return true;
    } else if(event->key == InputKeyRight) {
        settings_move_cursor(app, 1, 0);
        return true;
    } else if(event->key != InputKeyOk) {
        return false;
    }

    // OK pressed: get the selected key
    const HidKeyboardKey* sel =
            &hid_keyboard_keyset[app->settings_state.y][app->settings_state.x];
    uint8_t selected_keycode = sel->value;

    bool treat_as_toggle =
        (sel->action == KeyActionToggleModifier) ||
        (selected_keycode == HID_KEYBOARD_L_SHIFT) ||
        (selected_keycode == HID_KEYBOARD_L_CTRL) ||
        (selected_keycode == HID_KEYBOARD_L_ALT) ||
        (selected_keycode == HID_KEYBOARD_L_GUI);

    // "Tap" modifier: maps directly as its own keycode (no bitmask)
    if(sel->action == KeyActionTapModifier) {
        KeyMapping* existing = &app->presets[app->settings_state.editing_preset_index]
            .keymap[app->settings_state.configuring_button];
        if(app->settings_state.editing_long_press) {
            existing->long_keycode = selected_keycode;
            existing->long_modifiers = 0;
        } else {
            existing->keycode = selected_keycode;
            existing->modifiers = 0;
        }
        anki_remote_save_presets(app);
        app->ignore_next_ok_release = true;
        anki_remote_switch_to_view(app, AnkiRemoteViewPresetEditMenu);
        return true;
    }

    // Toggle modifier state inside the picker
    if(treat_as_toggle) {
        if(selected_keycode == HID_KEYBOARD_L_SHIFT) {
            app->settings_state.shift_pressed = !app->settings_state.shift_pressed;
        } else if(selected_keycode == HID_KEYBOARD_L_CTRL) {
            app->settings_state.ctrl_pressed = !app->settings_state.ctrl_pressed;
        } else if(selected_keycode == HID_KEYBOARD_L_ALT) {
            app->settings_state.alt_pressed = !app->settings_state.alt_pressed;
        } else if(selected_keycode == HID_KEYBOARD_L_GUI) {
            app->settings_state.gui_pressed = !app->settings_state.gui_pressed;
        }
        // View will be automatically updated by ViewDispatcher
        return true;
    }

    // Normal key selection with modifiers
    uint8_t modifiers = 0;
    if(app->settings_state.shift_pressed) modifiers |= MOD_SHIFT_BIT;
    if(app->settings_state.ctrl_pressed)  modifiers |= MOD_CTRL_BIT;
    if(app->settings_state.alt_pressed)   modifiers |= MOD_ALT_BIT;
    if(app->settings_state.gui_pressed)   modifiers |= MOD_GUI_BIT;

    // Save to the appropriate field based on editing mode
    KeyMapping* existing = &app->presets[app->settings_state.editing_preset_index]
        .keymap[app->settings_state.configuring_button];
    if(app->settings_state.editing_long_press) {
        existing->long_keycode = selected_keycode;
        existing->long_modifiers = modifiers;
    } else {
        existing->keycode = selected_keycode;
        existing->modifiers = modifiers;
    }
    anki_remote_save_presets(app);

    app->ignore_next_ok_release = true;
    anki_remote_switch_to_view(app, AnkiRemoteViewPresetEditMenu);
    return true;
}

// -----------------------------------------------------------------------------
// View: Credits
// -----------------------------------------------------------------------------

static void anki_remote_view_credits_draw(Canvas* canvas, void* context) {
    AnkiRemoteViewModel* model = context;
    AnkiRemoteApp* app = model ? model->app : NULL;
    if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Credits");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 26, "Blue5GD - created original");
    canvas_draw_str(canvas, 2, 38, "dgore - my version");
    canvas_draw_str(canvas, 2, 50, "Blog @ drgo.re/blog :))");
    canvas_draw_icon(canvas, 2, 56, &I_Pin_back_arrow_10x8);
    canvas_draw_str(canvas, 15, 62, "Back");
}

static bool anki_remote_view_credits_input(InputEvent* event, void* context) {
    AnkiRemoteApp* app = context;
    if(event->key == InputKeyBack && (event->type == InputTypeShort || event->type == InputTypeLong)) {
        anki_remote_switch_to_view(app, AnkiRemoteViewMenu);
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// View Switching
// -----------------------------------------------------------------------------

static bool anki_remote_exit_callback(void* context) {
    AnkiRemoteApp* app = context;
    app->running = false;
    return false;  // Return false to exit the app
}

static void anki_remote_stop_controller_bt(AnkiRemoteApp* app) {
    if(!app || !app->bt) return;
    if(!app->ble_hid_profile) return;

    // Mirror the stock HID teardown order to avoid furi_check failures.
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    furi_hal_bt_stop_advertising();
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_profile_restore_default(app->bt);
    app->ble_hid_profile = NULL;
    app->connected = false;
}

static void anki_remote_switch_to_view(AnkiRemoteApp* app, AnkiRemoteView view) {
    // Clean up controller when leaving it
    if(app->current_scene == SceneController && view != AnkiRemoteViewController) {
        anki_remote_stop_controller_bt(app);
    }

    // Initialize scene when entering
    if(view == AnkiRemoteViewController && app->current_scene != SceneController) {
        memset(&app->controller_state, 0, sizeof(app->controller_state));
        bt_set_status_changed_callback(app->bt, anki_remote_connection_status_callback, app);
        app->ble_hid_profile = bt_profile_start(app->bt, ble_profile_hid, NULL);
        furi_check(app->ble_hid_profile);
        furi_hal_bt_start_advertising();
    } else if(view == AnkiRemoteViewPresetManager) {
        app->preset_manager_state.selected_item = 0;
        app->preset_manager_state.scroll_offset = 0;
        if(app->highlighted_preset >= app->preset_count) app->highlighted_preset = 0;
    } else if(view == AnkiRemoteViewPresetEditMenu) {
        app->preset_edit_menu_state.selected_item = 0;
        app->preset_edit_menu_state.scroll_offset = 0;
    } else if(view == AnkiRemoteViewKeyPicker) {
        app->settings_state.x = 0;
        app->settings_state.y = 1;
        app->settings_state.shift_pressed = false;
        app->settings_state.ctrl_pressed = false;
        app->settings_state.alt_pressed = false;
        app->settings_state.gui_pressed = false;
        app->settings_state.editing_long_press = false;
    }

    // Update current scene tracking
    switch(view) {
    case AnkiRemoteViewMenu:
        app->current_scene = SceneMenu;
        break;
    case AnkiRemoteViewController:
        app->current_scene = SceneController;
        break;
    case AnkiRemoteViewPresetManager:
        app->current_scene = ScenePresetManager;
        break;
    case AnkiRemoteViewPresetEditMenu:
        app->current_scene = ScenePresetEditMenu;
        break;
    case AnkiRemoteViewKeyPicker:
        app->current_scene = SceneKeyPicker;
        break;
    case AnkiRemoteViewCredits:
        app->current_scene = SceneCredits;
        break;
    default:
        break;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

// -----------------------------------------------------------------------------
// App Alloc / Free
// -----------------------------------------------------------------------------

static AnkiRemoteApp* anki_remote_app_alloc() {
    AnkiRemoteApp* app = malloc(sizeof(AnkiRemoteApp));
    furi_check(app);
    memset(app, 0, sizeof(AnkiRemoteApp));
    app->event_queue = furi_message_queue_alloc(8, sizeof(AnkiRemoteEvent));
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->bt = furi_record_open(RECORD_BT);
    bt_keys_storage_set_storage_path(app->bt, HID_BT_KEYS_STORAGE_PATH);

    // Allocate ViewDispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, anki_remote_exit_callback);

    // Allocate Views
    // Menu View
    app->views[AnkiRemoteViewMenu] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewMenu], app);
    view_allocate_model(app->views[AnkiRemoteViewMenu], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewMenu]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewMenu], anki_remote_view_menu_draw);
    view_set_input_callback(app->views[AnkiRemoteViewMenu], anki_remote_view_menu_input);
    view_commit_model(app->views[AnkiRemoteViewMenu], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewMenu, app->views[AnkiRemoteViewMenu]);

    // Controller View
    app->views[AnkiRemoteViewController] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewController], app);
    view_allocate_model(app->views[AnkiRemoteViewController], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewController]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewController], anki_remote_view_controller_draw);
    view_set_input_callback(app->views[AnkiRemoteViewController], anki_remote_view_controller_input);
    view_set_orientation(app->views[AnkiRemoteViewController], ViewOrientationVerticalFlip);
    view_commit_model(app->views[AnkiRemoteViewController], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewController, app->views[AnkiRemoteViewController]);

    // Preset Manager View
    app->views[AnkiRemoteViewPresetManager] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewPresetManager], app);
    view_allocate_model(app->views[AnkiRemoteViewPresetManager], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewPresetManager]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewPresetManager], anki_remote_view_preset_manager_draw);
    view_set_input_callback(app->views[AnkiRemoteViewPresetManager], anki_remote_view_preset_manager_input);
    view_commit_model(app->views[AnkiRemoteViewPresetManager], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewPresetManager, app->views[AnkiRemoteViewPresetManager]);

    // Preset Edit Menu View
    app->views[AnkiRemoteViewPresetEditMenu] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewPresetEditMenu], app);
    view_allocate_model(app->views[AnkiRemoteViewPresetEditMenu], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewPresetEditMenu]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewPresetEditMenu], anki_remote_view_preset_edit_menu_draw);
    view_set_input_callback(app->views[AnkiRemoteViewPresetEditMenu], anki_remote_view_preset_edit_menu_input);
    view_commit_model(app->views[AnkiRemoteViewPresetEditMenu], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewPresetEditMenu, app->views[AnkiRemoteViewPresetEditMenu]);

    // Key Picker View
    app->views[AnkiRemoteViewKeyPicker] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewKeyPicker], app);
    view_allocate_model(app->views[AnkiRemoteViewKeyPicker], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewKeyPicker]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewKeyPicker], anki_remote_view_key_picker_draw);
    view_set_input_callback(app->views[AnkiRemoteViewKeyPicker], anki_remote_view_key_picker_input);
    view_commit_model(app->views[AnkiRemoteViewKeyPicker], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewKeyPicker, app->views[AnkiRemoteViewKeyPicker]);

    // Credits View
    app->views[AnkiRemoteViewCredits] = view_alloc();
    view_set_context(app->views[AnkiRemoteViewCredits], app);
    view_allocate_model(app->views[AnkiRemoteViewCredits], ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel));
    ((AnkiRemoteViewModel*)view_get_model(app->views[AnkiRemoteViewCredits]))->app = app;
    view_set_draw_callback(app->views[AnkiRemoteViewCredits], anki_remote_view_credits_draw);
    view_set_input_callback(app->views[AnkiRemoteViewCredits], anki_remote_view_credits_input);
    view_commit_model(app->views[AnkiRemoteViewCredits], false);
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewCredits, app->views[AnkiRemoteViewCredits]);

    // Allocate TextInput (for rename)
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AnkiRemoteViewTextInput, text_input_get_view(app->text_input));

    // Initialize state
    app->running = true;
    app->connected = false;
    app->current_scene = SceneMenu;
    app->menu_state.selected_item = 0;
    app->ignore_next_ok_release = false;
    app->rename_preset_index = 0;
    app->led_brightness = 255;
    app->preset_count = 0;
    app->active_preset = 0;
    app->highlighted_preset = 0;
    anki_remote_load_presets(app);

    // Start at menu
    view_dispatcher_switch_to_view(app->view_dispatcher, AnkiRemoteViewMenu);

    return app;
}

static void anki_remote_app_free(AnkiRemoteApp* app) {
    furi_assert(app);

    // Clean up controller if active
    anki_remote_stop_controller_bt(app);

    // Free Views
    for(uint32_t i = 0; i < 7; i++) {
        if(app->views[i]) {
            view_dispatcher_remove_view(app->view_dispatcher, i);
            view_free(app->views[i]);
        }
    }

    // Free TextInput
    if(app->text_input) {
        view_dispatcher_remove_view(app->view_dispatcher, AnkiRemoteViewTextInput);
        text_input_free(app->text_input);
        app->text_input = NULL;
    }

    // Free ViewDispatcher
    view_dispatcher_free(app->view_dispatcher);
    app->view_dispatcher = NULL;

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    // Reset Bluetooth key storage path to default to not break other apps.
    bt_keys_storage_set_default_path(app->bt);
    furi_record_close(RECORD_BT);
    app->bt = NULL;
    furi_message_queue_free(app->event_queue);
    free(app);
}

// -----------------------------------------------------------------------------
// Entry Point
// -----------------------------------------------------------------------------

int32_t anki_remote_app(void* p) {
    UNUSED(p);
    AnkiRemoteApp* app = anki_remote_app_alloc();

    // Run the ViewDispatcher
    view_dispatcher_run(app->view_dispatcher);

    // Reset LED and clean up
    notification_internal_message(app->notifications, &sequence_reset_blue);
    anki_remote_app_free(app);
    return 0;
}

// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
// 67 67 67 67 67 67 67 67 67 67 67 67
