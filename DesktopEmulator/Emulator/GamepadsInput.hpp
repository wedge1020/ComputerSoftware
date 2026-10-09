// *****************************************************************************
    // start include guard
    #ifndef GAMEPADSINPUT_HPP
    #define GAMEPADSINPUT_HPP
    
    // include console logic headers
    #include "ConsoleLogic/ExternalInterfaces.hpp"
    
    // include C/C++ headers
    #include <map>              // [ C++ STL ] Maps
    #include <deque>            // [ C++ STL ] Double-ended queues
    #include <string>           // [ C++ STL ] Strings
    
    // include SDL2 headers
    #define SDL_MAIN_HANDLED
    #include "SDL.h"            // [ SDL2 ] Main header
// *****************************************************************************


// =============================================================================
//      DEFINITIONS FOR INPUT MAPPINGS
// =============================================================================


// control mapping for the keyboard
class KeyboardMapping
{
    public:
        
        // d-pad directions
        SDL_Keycode Left, Right, Up, Down;
        
        // buttons
        SDL_Keycode ButtonA, ButtonB, ButtonX, ButtonY;
        SDL_Keycode ButtonL, ButtonR, ButtonStart;
        
        // optional command button
        SDL_Keycode Command;
};

// -----------------------------------------------------------------------------

// possible types of joystick controls
enum class JoystickControlTypes
{
    None,
    Button,
    Axis,
    Hat
};

// -----------------------------------------------------------------------------

// identification of a single control from a given joystick
class JoystickControl
{
    public:
        
        // control type
        JoystickControlTypes Type;
        
        // button info
        int ButtonIndex;
        
        // axis info
        int AxisIndex;
        bool AxisPositive;
        
        // hat info
        int HatIndex;
        int HatDirection;
    
    public:
        
        // constructor to leave all controls unmapped
        JoystickControl();
        
        // type queries
        bool IsButton() { return (Type == JoystickControlTypes::Button); };
        bool IsAxis()   { return (Type == JoystickControlTypes::Axis  ); };
        bool IsHat()    { return (Type == JoystickControlTypes::Hat   ); };
};

// -----------------------------------------------------------------------------

// control mapping for a joystick
class JoystickMapping
{
    public:
        
        // static identification
        SDL_JoystickGUID GUID;
        
        // human-readable names
        std::string ProfileName;
        std::string JoystickName;
        
        // d-pad directions
        JoystickControl Left, Right, Up, Down;
        
        // buttons
        JoystickControl ButtonA, ButtonB, ButtonX, ButtonY;
        JoystickControl ButtonL, ButtonR, ButtonStart;    
        
        // optional command button
        JoystickControl Command;
};

// -----------------------------------------------------------------------------

// possible options for a mapped host device
enum class DeviceTypes
{
    NoDevice,
    Keyboard,       // a few keys mapped to gamepad controls
    Joystick,
    V32Kbd,         // full keyboard: scancodes encoded as gamepad controls
    V32Mouse        // host mouse: buttons and movement encoded as gamepad controls
};

// -----------------------------------------------------------------------------

// name used for the v32kbd device, both in the
// gamepads menu and as profile name in settings
#define V32KBD_PROFILE_NAME "v32kbd"

// A v32kbd device presents itself to the console as a regular gamepad,
// but its 11 controls are used to report keyboard events:
//
//   Start, A, B, X, Y, L, R --> 7-bit key code (Start = bit 0 ... R = bit 6),
//                               in the same order as their IO ports
//   Up / Down               --> key action: Up = pressed, Down = released
//   Left / Right            --> strobe: every new key event alternates
//                               between Left and Right, beginning by Left
//
// The d-pad never has opposite directions pressed together, so this is
// always a valid gamepad state and no console logic needs to be changed.
//
// Key codes identify keys (not characters: no shift is applied). Keys
// with an ASCII character use it as their code, taking that key in a US
// layout with no shift: 'a'-'z', '0'-'9', space and  ` - = [ ] \ ; ' , . /
// The other keys use the codes listed in the enumeration below.
//
// Only 1 key event is reported per frame, and controls keep their last
// reported state until the next event. A new event can be detected when
// the pressed side (Left/Right) changes. Before the first event, all
// controls are unpressed.
namespace V32Kbd
{
    enum KeyCodes
    {
        Key_None = 0,       // never reported
        Key_Up = 1,
        Key_Down,
        Key_Left,
        Key_Right,
        Key_CapsLock = 5,
        Key_LeftShift,
        Key_RightShift,
        Key_Backspace = 8,  // same as ASCII
        Key_Tab = 9,        // same as ASCII
        Key_LeftControl = 10,
        Key_RightControl,
        Key_LeftAlt = 12,   // Option key on Mac
        Key_Enter = 13,     // same as ASCII
        Key_F1 = 14,        // F1 to F12 are consecutive: 14 to 25
        Key_F12 = 25,
        Key_RightAlt = 26,  // Option key on Mac
        Key_Escape = 27,    // same as ASCII
        Key_LeftGUI = 28,   // Command key on Mac, Windows key on PC
        Key_RightGUI = 29,
                            // 30 and 31 are unused
        Key_Delete = 127    // same as ASCII
    };
    
    // number of bits in a key code
    const int CodeBits = 7;
    
    // pending key events over this limit are discarded
    const unsigned MaxQueuedEvents = 256;
    
    // gamepad control used to report each bit of the key code
    extern const V32::GamepadControls CodeControls[ CodeBits ];
    
    // converts host keys to key codes (Key_None for unsupported keys)
    int GetKeyCode( SDL_Scancode Scancode );
}

// a single key event waiting to be reported
struct V32KbdEvent
{
    uint8_t KeyCode;
    bool Pressed;
};

// -----------------------------------------------------------------------------

// name used for the v32mouse device, both in the
// gamepads menu and as profile name in settings
#define V32MOUSE_PROFILE_NAME "v32mouse"

// A v32mouse device presents itself to the console as a regular gamepad,
// but its 11 controls are used to report the host mouse (this is the same
// protocol as the v32io hardware adapter in mouse mode, "v32io:mouse"):
//
//   Start, A, B --> middle, left and right mouse buttons, as they are
//   Left / Right, X, Y  --> X counter: trit (Left = -, Right = +),
//                           Gray code (X = high bit, Y = low bit)
//   Up / Down,    L, R  --> Y counter: trit (Up = -, Down = +),
//                           Gray code (L = high bit, R = low bit)
//
// Movement is not sent as deltas, but as the position of 2 counters (one
// per axis) that go around a cycle of 12 positions. Moving right / down
// steps forward, left / up steps backward. Every single step changes
// exactly 1 control:
//
//   position:  0  1  2 | 3  4  5 | 6  7  8 | 9 10 11
//   gray:        00    |   01    |   11    |   10
//   trit:      -  0  + | +  0  - | -  0  + | +  0  -
//
// At rest (when the gamepad gets connected) both counters are at position
// 1, which has no controls pressed. Programs compare positions between
// frames, so a counter can move at most 5 positions per frame (6 would be
// ambiguous). A trit is a pair of opposite directions, which the console
// never shows pressed together, so all states are valid gamepad states.
//
// Since the host mouse can't be shared with the GUI, it is only reported
// while captured: click on the game screen to capture it, and press left
// Ctrl + left Alt (left Control + left Option on Mac) to release it.
// Switching to another window also releases it.
namespace V32Mouse
{
    // positions in each counter cycle
    const int Positions = 12;
    
    // counter position when nothing is pressed
    const int RestPosition = 1;
    
    // most steps a counter can move in a single frame
    const int MaxStepsPerFrame = 5;
    
    // movement waiting to be sent over this many steps is discarded,
    // so that the pointer stops soon after the host mouse does
    const int MaxPendingSteps = 10;
    
    // host mouse motion units (raw counts in relative mode) that make
    // 1 step of a counter: lower values give a faster pointer
    const int DefaultCountsPerStep = 2;
    const int MinCountsPerStep = 1;
    const int MaxCountsPerStep = 16;
    
    // indices for the mouse buttons
    enum Buttons
    {
        Button_Left = 0,
        Button_Right,
        Button_Middle,
        ButtonsCount
    };
    
    // pending button changes over this limit are discarded
    const unsigned MaxQueuedChanges = 16;
    
    // gamepad control used to report each mouse button
    extern const V32::GamepadControls ButtonControls[ ButtonsCount ];
    
    // the 4 gamepad controls that make up a movement counter
    struct CounterControls
    {
        V32::GamepadControls Negative, Positive;    // trit
        V32::GamepadControls High, Low;             // Gray code
    };
    
    extern const CounterControls CounterX;
    extern const CounterControls CounterY;
    
    // converts SDL mouse buttons to our indices (-1 for unsupported ones)
    int GetButtonIndex( Uint8 SDLButton );
}

// -----------------------------------------------------------------------------

// full identification of a host computer device
struct DeviceInfo
{
    // base device info
    DeviceTypes Type;
    
    // for a joystick, extra info is needed
    // since there can be several connected
    SDL_JoystickGUID GUID;        // joystick static identification
    SDL_JoystickID InstanceID;    // joystick dynamic identification
};


// =============================================================================
//      OPERATION WITH GUIDS
// =============================================================================


// operators needed to use GUIDs in a std::map
bool operator==( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 );
bool operator!=( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 );
bool operator<( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 );

// GUID <-> string conversions
std::string GUIDToString( SDL_JoystickGUID GUID );
bool GUIDStringIsValid( const std::string& GUIDString );


// =============================================================================
//      CLASS FOR GAMEPADS INPUT
// =============================================================================


class GamepadsInput
{
    private:
        
        // all currently connected joysticks
        std::map< SDL_JoystickID, SDL_JoystickGUID > ConnectedJoysticks;
        
        // all of our available mappings
        KeyboardMapping KeyboardProfile;
        std::map< SDL_JoystickGUID, JoystickMapping* > JoystickProfiles;
        
        // state of the command button for each gamepad (these are optional
        // and not part of the console gamepads so handle them separately)
        bool CommandPressed[ V32::Constants::GamepadPorts ];
        
        // key events pending to be reported by the v32kbd device
        std::deque< V32KbdEvent > V32KbdQueue;
        
        // v32mouse device: whether the host mouse is captured by it,
        // host mouse motion not yet sent (in host motion units), and
        // button changes pending to be shown (at most 1 per frame,
        // so that even the quickest clicks are seen by programs)
        bool V32MouseCaptured;
        int V32MousePendingX, V32MousePendingY;
        std::deque< bool > V32MouseButtonQueues[ V32Mouse::ButtonsCount ];
        bool V32MouseButtonTargets[ V32Mouse::ButtonsCount ];
    
    public:
        
        // v32mouse device: host motion units per counter step
        int V32MouseCountsPerStep;
    
    public:
        
        // maps {Vircon gamepads} --> {PC devices}
        DeviceInfo MappedGamepads[ V32::Constants::GamepadPorts ];
        
    private:
        
        // specialized event processing functions
        void ProcessJoystickAdded( SDL_Event Event );
        void ProcessJoystickRemoved( SDL_Event Event );
        void ProcessJoystickAxisMotion( SDL_Event Event );
        void ProcessJoystickHatMotion( SDL_Event Event );
        void ProcessJoystickButtonDown( SDL_Event Event );
        void ProcessJoystickButtonUp( SDL_Event Event );
        void ProcessKeyDown( SDL_Event Event );
        void ProcessKeyUp( SDL_Event Event );
        void ProcessV32KbdKey( SDL_Event Event );
        void ProcessV32MouseMotion( SDL_Event Event );
        void ProcessV32MouseButton( SDL_Event Event );
        
        // v32mouse helpers
        void ClearV32MouseInput();
        bool V32MouseCanCapture();
        
    public:
        
        // instance handling
        GamepadsInput();
       ~GamepadsInput();
        
        // handling control profiles
        void SetDefaultProfiles();
        void AddJoystickProfile( SDL_JoystickGUID NewJoystickGUID, JoystickMapping* NewJoystickProfile );
        const std::map< SDL_JoystickGUID, JoystickMapping* >& ReadAllJoystickProfiles();
        JoystickMapping* GetJoystickProfile( const std::string& ProfileName );
        JoystickMapping* GetJoystickProfile( SDL_JoystickGUID GUID );
        KeyboardMapping& GetKeyboardProfile();
        
        // handling devices
        void OpenAllJoysticks();
        void CloseAllJoysticks();
        void AssignInputDevices();
        
        // queries on device usage (-1 = not used in any gamepad)
        int GetKeyboardGamepad();
        int GetV32KbdGamepad();
        int GetV32MouseGamepad();
        
        // v32kbd device: call this exactly once before every emulated
        // frame, to report the next pending key event (if there is any)
        void UpdateV32Kbd();
        
        // v32mouse device: call this exactly once before every emulated
        // frame, to report button changes and movement since the last one
        void UpdateV32Mouse();
        
        // v32mouse device: capturing the host mouse. Releasing is
        // always safe (it does nothing when not captured)
        bool IsV32MouseCaptured();
        void CaptureV32Mouse();
        void ReleaseV32Mouse();
        
        // v32mouse device: call this once per main loop iteration;
        // it releases the mouse when it can no longer be captured
        void CheckV32MouseCapture();
        
        // processing input events
        void ProcessEvent( SDL_Event Event );
};


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
