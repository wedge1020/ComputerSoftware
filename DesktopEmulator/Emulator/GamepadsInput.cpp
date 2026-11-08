// *****************************************************************************
    // include console logic headers
    #include "ConsoleLogic/V32Console.hpp"
    
    // include infrastructure headers
    #include "DesktopInfrastructure/Logger.hpp"
    #include "DesktopInfrastructure/NumericFunctions.hpp"
    
    // include emulator headers
    #include "GamepadsInput.hpp"
    #include "EmulatorControl.hpp"
    #include "GUI.hpp"
    #include "Globals.hpp"
    
    // include imgui headers
    #include <imgui/imgui.h>    // [ Dear ImGui ] Main header
    
    // include C/C++ headers
    #include <stdexcept>        // [ C++ STL ] Exceptions
    #include <iostream>         // [ C++ STL ] I/O Streams
    #include <set>              // [ C++ STL ] Sets
    #include <climits>          // [ ANSI C ] Numeric limits
    
    // declare used namespaces
    using namespace std;
    using namespace V32;
// *****************************************************************************


// =============================================================================
//      DEFINITIONS FOR INPUT MAPPINGS
// =============================================================================


// gamepad control used to report each bit of a v32kbd key code
const GamepadControls V32Kbd::CodeControls[ V32Kbd::CodeBits ] =
{
    // same order as the console's IO ports for these controls
    GamepadControls::ButtonStart,   // bit 0 (port 0x406)
    GamepadControls::ButtonA,       // bit 1 (port 0x407)
    GamepadControls::ButtonB,       // bit 2 (port 0x408)
    GamepadControls::ButtonX,       // bit 3 (port 0x409)
    GamepadControls::ButtonY,       // bit 4 (port 0x40A)
    GamepadControls::ButtonL,       // bit 5 (port 0x40B)
    GamepadControls::ButtonR        // bit 6 (port 0x40C)
};

// -----------------------------------------------------------------------------

int V32Kbd::GetKeyCode( SDL_Scancode Scancode )
{
    // ranges of consecutive keys
    if( Scancode >= SDL_SCANCODE_A && Scancode <= SDL_SCANCODE_Z )
      return 'a' + (Scancode - SDL_SCANCODE_A);
    
    if( Scancode >= SDL_SCANCODE_1 && Scancode <= SDL_SCANCODE_9 )
      return '1' + (Scancode - SDL_SCANCODE_1);
    
    if( Scancode >= SDL_SCANCODE_F1 && Scancode <= SDL_SCANCODE_F12 )
      return Key_F1 + (Scancode - SDL_SCANCODE_F1);
    
    if( Scancode >= SDL_SCANCODE_KP_1 && Scancode <= SDL_SCANCODE_KP_9 )
      return '1' + (Scancode - SDL_SCANCODE_KP_1);
    
    // individual keys
    switch( Scancode )
    {
        // keys with an ASCII character (US layout, no shift)
        case SDL_SCANCODE_0:             return '0';
        case SDL_SCANCODE_SPACE:         return ' ';
        case SDL_SCANCODE_GRAVE:         return '`';
        case SDL_SCANCODE_MINUS:         return '-';
        case SDL_SCANCODE_EQUALS:        return '=';
        case SDL_SCANCODE_LEFTBRACKET:   return '[';
        case SDL_SCANCODE_RIGHTBRACKET:  return ']';
        case SDL_SCANCODE_BACKSLASH:     return '\\';
        case SDL_SCANCODE_NONUSHASH:     return '\\';
        case SDL_SCANCODE_SEMICOLON:     return ';';
        case SDL_SCANCODE_APOSTROPHE:    return '\'';
        case SDL_SCANCODE_COMMA:         return ',';
        case SDL_SCANCODE_PERIOD:        return '.';
        case SDL_SCANCODE_SLASH:         return '/';
        
        // keys with an ASCII control code
        case SDL_SCANCODE_BACKSPACE:     return Key_Backspace;
        case SDL_SCANCODE_TAB:           return Key_Tab;
        case SDL_SCANCODE_RETURN:        return Key_Enter;
        case SDL_SCANCODE_ESCAPE:        return Key_Escape;
        case SDL_SCANCODE_DELETE:        return Key_Delete;
        
        // keys with no ASCII code
        case SDL_SCANCODE_UP:            return Key_Up;
        case SDL_SCANCODE_DOWN:          return Key_Down;
        case SDL_SCANCODE_LEFT:          return Key_Left;
        case SDL_SCANCODE_RIGHT:         return Key_Right;
        case SDL_SCANCODE_CAPSLOCK:      return Key_CapsLock;
        case SDL_SCANCODE_LSHIFT:        return Key_LeftShift;
        case SDL_SCANCODE_RSHIFT:        return Key_RightShift;
        case SDL_SCANCODE_LCTRL:         return Key_LeftControl;
        case SDL_SCANCODE_RCTRL:         return Key_RightControl;
        case SDL_SCANCODE_LALT:          return Key_LeftAlt;
        case SDL_SCANCODE_RALT:          return Key_RightAlt;
        case SDL_SCANCODE_LGUI:          return Key_LeftGUI;
        case SDL_SCANCODE_RGUI:          return Key_RightGUI;
        
        // numeric keypad: same codes as the equivalent main keys
        case SDL_SCANCODE_KP_0:          return '0';
        case SDL_SCANCODE_KP_PERIOD:     return '.';
        case SDL_SCANCODE_KP_DIVIDE:     return '/';
        case SDL_SCANCODE_KP_MINUS:      return '-';
        case SDL_SCANCODE_KP_ENTER:      return Key_Enter;
        
        // any other keys are not supported
        default:                         return Key_None;
    }
}

// -----------------------------------------------------------------------------

// gamepad control used to report each v32mouse button
const GamepadControls V32Mouse::ButtonControls[ V32Mouse::ButtonsCount ] =
{
    GamepadControls::ButtonA,       // left button   (port 0x407)
    GamepadControls::ButtonB,       // right button  (port 0x408)
    GamepadControls::ButtonStart    // middle button (port 0x406)
};

// gamepad controls used by each v32mouse movement counter
const V32Mouse::CounterControls V32Mouse::CounterX =
{
    GamepadControls::Left,  GamepadControls::Right,     // trit
    GamepadControls::ButtonX, GamepadControls::ButtonY  // Gray code
};

const V32Mouse::CounterControls V32Mouse::CounterY =
{
    GamepadControls::Up,    GamepadControls::Down,      // trit
    GamepadControls::ButtonL, GamepadControls::ButtonR  // Gray code
};

// -----------------------------------------------------------------------------

int V32Mouse::GetButtonIndex( Uint8 SDLButton )
{
    switch( SDLButton )
    {
        case SDL_BUTTON_LEFT:   return Button_Left;
        case SDL_BUTTON_RIGHT:  return Button_Right;
        case SDL_BUTTON_MIDDLE: return Button_Middle;
        default:                return -1;
    }
}

// -----------------------------------------------------------------------------

JoystickControl::JoystickControl()
{
    Type = JoystickControlTypes::None;
    ButtonIndex = -1;
    AxisIndex = -1;
    HatIndex = -1;
    AxisPositive = true;
    HatDirection = SDL_HAT_CENTERED;
}


// =============================================================================
//     OPERATION WITH GUIDS
// =============================================================================


bool operator==( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 )
{
    return !memcmp( &GUID1, &GUID2, sizeof(SDL_JoystickGUID) );
}

// -----------------------------------------------------------------------------

bool operator!=( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 )
{
    return memcmp( &GUID1, &GUID2, sizeof(SDL_JoystickGUID) );
}

// -----------------------------------------------------------------------------

bool operator<( const SDL_JoystickGUID& GUID1, const SDL_JoystickGUID& GUID2 )
{
    int Result = memcmp( &GUID1, &GUID2, sizeof(SDL_JoystickGUID) );
    return (Result < 0);
}

// -----------------------------------------------------------------------------

string GUIDToString( SDL_JoystickGUID GUID )
{
    char GUIDString[ 35 ];
    SDL_JoystickGetGUIDString( GUID, GUIDString, 34 );  
    return GUIDString;  
}

// -----------------------------------------------------------------------------

bool GUIDStringIsValid( const string& GUIDString )
{
    // length must be even and no greater than 32 characters
    if( GUIDString.size() > 32 ) return false;
    if( GUIDString.size() &  1 ) return false;
    
    // characters must be hexadecimal and lowercase
    for( char c: GUIDString )
    {
        if( isdigit( c ) ) continue;
        if( isupper( c ) ) return false;
        if( c < 'a' && c > 'f' ) return false;
    }
    
    return true;
}


// =============================================================================
//      V32KBD: CHARACTERS OF THE BIOS FONT
// =============================================================================


// characters of the BIOS font that are not ASCII, by Unicode value (sorted).
// The BIOS font is Windows-1252 except for codes 14-31, which hold box
// drawing characters. Generated by tools/gen-charset.py --emulator
struct BiosCharacter
{
    uint16_t Unicode;
    uint8_t Code;
};

static const BiosCharacter BiosCharacters[] =
{
    { 0x00A1, 0xA1 },
    { 0x00A2, 0xA2 },
    { 0x00A3, 0xA3 },
    { 0x00A4, 0xA4 },
    { 0x00A5, 0xA5 },
    { 0x00A6, 0xA6 },
    { 0x00A7, 0xA7 },
    { 0x00A8, 0xA8 },
    { 0x00A9, 0xA9 },
    { 0x00AA, 0xAA },
    { 0x00AB, 0xAB },
    { 0x00AC, 0xAC },
    { 0x00AD, 0xAD },
    { 0x00AE, 0xAE },
    { 0x00AF, 0xAF },
    { 0x00B0, 0xB0 },
    { 0x00B1, 0xB1 },
    { 0x00B2, 0xB2 },
    { 0x00B3, 0xB3 },
    { 0x00B4, 0xB4 },
    { 0x00B5, 0xB5 },
    { 0x00B6, 0xB6 },
    { 0x00B7, 0xB7 },
    { 0x00B8, 0xB8 },
    { 0x00B9, 0xB9 },
    { 0x00BA, 0xBA },
    { 0x00BB, 0xBB },
    { 0x00BC, 0xBC },
    { 0x00BD, 0xBD },
    { 0x00BE, 0xBE },
    { 0x00BF, 0xBF },
    { 0x00C0, 0xC0 },
    { 0x00C1, 0xC1 },
    { 0x00C2, 0xC2 },
    { 0x00C3, 0xC3 },
    { 0x00C4, 0xC4 },
    { 0x00C5, 0xC5 },
    { 0x00C6, 0xC6 },
    { 0x00C7, 0xC7 },
    { 0x00C8, 0xC8 },
    { 0x00C9, 0xC9 },
    { 0x00CA, 0xCA },
    { 0x00CB, 0xCB },
    { 0x00CC, 0xCC },
    { 0x00CD, 0xCD },
    { 0x00CE, 0xCE },
    { 0x00CF, 0xCF },
    { 0x00D0, 0xD0 },
    { 0x00D1, 0xD1 },
    { 0x00D2, 0xD2 },
    { 0x00D3, 0xD3 },
    { 0x00D4, 0xD4 },
    { 0x00D5, 0xD5 },
    { 0x00D6, 0xD6 },
    { 0x00D7, 0xD7 },
    { 0x00D8, 0xD8 },
    { 0x00D9, 0xD9 },
    { 0x00DA, 0xDA },
    { 0x00DB, 0xDB },
    { 0x00DC, 0xDC },
    { 0x00DD, 0xDD },
    { 0x00DE, 0xDE },
    { 0x00DF, 0xDF },
    { 0x00E0, 0xE0 },
    { 0x00E1, 0xE1 },
    { 0x00E2, 0xE2 },
    { 0x00E3, 0xE3 },
    { 0x00E4, 0xE4 },
    { 0x00E5, 0xE5 },
    { 0x00E6, 0xE6 },
    { 0x00E7, 0xE7 },
    { 0x00E8, 0xE8 },
    { 0x00E9, 0xE9 },
    { 0x00EA, 0xEA },
    { 0x00EB, 0xEB },
    { 0x00EC, 0xEC },
    { 0x00ED, 0xED },
    { 0x00EE, 0xEE },
    { 0x00EF, 0xEF },
    { 0x00F0, 0xF0 },
    { 0x00F1, 0xF1 },
    { 0x00F2, 0xF2 },
    { 0x00F3, 0xF3 },
    { 0x00F4, 0xF4 },
    { 0x00F5, 0xF5 },
    { 0x00F6, 0xF6 },
    { 0x00F7, 0xF7 },
    { 0x00F8, 0xF8 },
    { 0x00F9, 0xF9 },
    { 0x00FA, 0xFA },
    { 0x00FB, 0xFB },
    { 0x00FC, 0xFC },
    { 0x00FD, 0xFD },
    { 0x00FE, 0xFE },
    { 0x00FF, 0xFF },
    { 0x0152, 0x8C },
    { 0x0153, 0x9C },
    { 0x0160, 0x8A },
    { 0x0161, 0x9A },
    { 0x0178, 0x9F },
    { 0x017D, 0x8E },
    { 0x017E, 0x9E },
    { 0x0192, 0x83 },
    { 0x02C6, 0x88 },
    { 0x02DC, 0x98 },
    { 0x2013, 0x96 },
    { 0x2014, 0x97 },
    { 0x2018, 0x91 },
    { 0x2019, 0x92 },
    { 0x201A, 0x82 },
    { 0x201C, 0x93 },
    { 0x201D, 0x94 },
    { 0x201E, 0x84 },
    { 0x2020, 0x86 },
    { 0x2021, 0x87 },
    { 0x2022, 0x95 },
    { 0x2026, 0x85 },
    { 0x2030, 0x89 },
    { 0x2039, 0x8B },
    { 0x203A, 0x9B },
    { 0x20AC, 0x80 },
    { 0x2122, 0x99 },
    { 0x2500, 0x15 },
    { 0x2502, 0x16 },
    { 0x250C, 0x19 },
    { 0x2510, 0x18 },
    { 0x2514, 0x1A },
    { 0x2518, 0x17 },
    { 0x251C, 0x1B },
    { 0x2524, 0x1C },
    { 0x252C, 0x1E },
    { 0x2534, 0x1D },
    { 0x253C, 0x1F },
    { 0x2588, 0x14 },
    { 0x2591, 0x11 },
    { 0x2592, 0x12 },
    { 0x2593, 0x13 },
    { 0x25AF, 0x0F },
    { 0x2B1A, 0x0E },
};

int V32Kbd::GetBiosCharacter( uint32_t Unicode )
{
    // ASCII is the same in the BIOS font
    if( Unicode >= 32 && Unicode <= 126 )
      return (int)Unicode;
    
    // binary search in the table
    int Low = 0;
    int High = (int)(sizeof(BiosCharacters) / sizeof(BiosCharacters[0])) - 1;
    
    while( Low <= High )
    {
        int Middle = (Low + High) / 2;
        
        if( BiosCharacters[ Middle ].Unicode == Unicode )
          return BiosCharacters[ Middle ].Code;
        
        if( BiosCharacters[ Middle ].Unicode < Unicode )
          Low = Middle + 1;
        else
          High = Middle - 1;
    }
    
    return 0;
}

// -----------------------------------------------------------------------------

// reads the next character from an UTF-8 string (invalid bytes are skipped)
static uint32_t NextUTF8Character( const char*& Text )
{
    while( *Text )
    {
        unsigned char First = (unsigned char)*Text;
        int Extra = 0;
        uint32_t Value = 0;
        
        if( First < 0x80 )                { Value = First;        Extra = 0; }
        else if( (First & 0xE0) == 0xC0 ) { Value = First & 0x1F; Extra = 1; }
        else if( (First & 0xF0) == 0xE0 ) { Value = First & 0x0F; Extra = 2; }
        else if( (First & 0xF8) == 0xF0 ) { Value = First & 0x07; Extra = 3; }
        else                              { Text++; continue; }
        
        Text++;
        bool Valid = true;
        
        for( int i = 0; i < Extra; i++ )
        {
            if( (((unsigned char)*Text) & 0xC0) != 0x80 )
            {
                Valid = false;
                break;
            }
            
            Value = (Value << 6) | (((unsigned char)*Text) & 0x3F);
            Text++;
        }
        
        if( Valid )
          return Value;
    }
    
    return 0;
}


// =============================================================================
//      GAMEPADS INPUT: INSTANCE HANDLING
// =============================================================================


GamepadsInput::GamepadsInput()
{
    SetDefaultProfiles();
    
    // command buttons are all initially unpressed
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
      CommandPressed[ Gamepad ] = false;
    
    // v32kbd starts with no pending input
    ClearV32KbdInput();
    
    // v32mouse starts not captured, with no pending input
    V32MouseCaptured = false;
    V32MouseCountsPerStep = V32Mouse::DefaultCountsPerStep;
    ClearV32MouseInput();
}

// -----------------------------------------------------------------------------

GamepadsInput::~GamepadsInput()
{
    // delete all joystick profiles
    for( auto Pair: JoystickProfiles )
      delete Pair.second;
    
    JoystickProfiles.clear();
}


// =============================================================================
//      GAMEPADS INPUT: HANDLING CONTROL PROFILES
// =============================================================================


void GamepadsInput::SetDefaultProfiles()
{
    // first delete any joystick profiles
    for( auto Pair: JoystickProfiles )
      delete Pair.second;
    
    JoystickProfiles.clear();
    
    // set the default keyboard profile
    KeyboardProfile.Left = SDLK_LEFT;
    KeyboardProfile.Right = SDLK_RIGHT;
    KeyboardProfile.Up = SDLK_UP;
    KeyboardProfile.Down = SDLK_DOWN;
    
    KeyboardProfile.ButtonA = SDLK_x;
    KeyboardProfile.ButtonB = SDLK_z;
    KeyboardProfile.ButtonX = SDLK_s;
    KeyboardProfile.ButtonY = SDLK_a;
    KeyboardProfile.ButtonL = SDLK_q;
    KeyboardProfile.ButtonR = SDLK_w;
    
    KeyboardProfile.ButtonStart = SDLK_RETURN;
    
    // by default Command button is not used
    KeyboardProfile.Command = -1;
}

// -----------------------------------------------------------------------------

void GamepadsInput::AddJoystickProfile( SDL_JoystickGUID NewJoystickGUID, JoystickMapping* NewJoystickProfile )
{
    JoystickProfiles[ NewJoystickGUID ] = NewJoystickProfile;
}

// -----------------------------------------------------------------------------

const map< SDL_JoystickGUID, JoystickMapping* >& GamepadsInput::ReadAllJoystickProfiles()
{
    return JoystickProfiles;
}

// -----------------------------------------------------------------------------

JoystickMapping* GamepadsInput::GetJoystickProfile( const string& ProfileName )
{
    for( auto Pair: JoystickProfiles )
      if( Pair.second->ProfileName == ProfileName )
        return Pair.second;
    
    return nullptr;
}

// -----------------------------------------------------------------------------

JoystickMapping* GamepadsInput::GetJoystickProfile( SDL_JoystickGUID GUID )
{
    auto Position = JoystickProfiles.find( GUID );
    
    if( Position == JoystickProfiles.end() )
      return nullptr;
    
    return Position->second;
}

// -----------------------------------------------------------------------------

KeyboardMapping& GamepadsInput::GetKeyboardProfile()
{
    return KeyboardProfile;
}


// =============================================================================
//      GAMEPADS INPUT: HANDLING DEVICES
// =============================================================================


void GamepadsInput::OpenAllJoysticks()
{
    // open all connected joysticks
    int NumberOfJoysticks = SDL_NumJoysticks();
    LOG( "Active joysticks: " + to_string( NumberOfJoysticks ) );
    
    for( int JoystickIndex = 0; JoystickIndex < NumberOfJoysticks; JoystickIndex++ )
    {
        SDL_Joystick* NewJoystick = SDL_JoystickOpen( JoystickIndex );
        
        if( NewJoystick )
        {
            SDL_JoystickGUID NewGUID = SDL_JoystickGetGUID( NewJoystick );
            Sint32 AddedInstanceID = SDL_JoystickInstanceID( NewJoystick );
            ConnectedJoysticks[ AddedInstanceID ] = NewGUID;
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::CloseAllJoysticks()
{
    // close all connected joysticks
    for( auto Pair: ConnectedJoysticks )
    {
        SDL_Joystick* ClosedJoystick = SDL_JoystickFromInstanceID( Pair.first );
        SDL_JoystickClose( ClosedJoystick );
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::AssignInputDevices()
{
    set< SDL_JoystickID > MappedInstanceIDs;
    bool IsKeyboardUsed = false;
    
    bool IsMouseUsed = false;
    
    // any change in devices discards pending v32kbd events; that
    // gamepad gets disconnected here so its controls are all reset
    ClearV32KbdInput();
    
    // the same applies to v32mouse (on reconnection its counters
    // will be back at rest, which is what programs expect)
    ClearV32MouseInput();
    
    // update mappings for gamepads
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        DeviceInfo* GamepadDevice = &MappedGamepads[ Gamepad ];
        
        // preemptively disconnect the gamepad
        Console.SetGamepadConnection( Gamepad, false );
        
        // process non-joystick devices
        if( GamepadDevice->Type == DeviceTypes::NoDevice )
          continue;
        
        if( GamepadDevice->Type == DeviceTypes::Keyboard )
        {
            // allow only for 1 gamepad to use the keyboard
            if( IsKeyboardUsed )
              GamepadDevice->Type = DeviceTypes::NoDevice;
              
            else
            {
                IsKeyboardUsed = true;
                Console.SetGamepadConnection( Gamepad, true );
            }
            
            continue;
        }
        
        if( GamepadDevice->Type == DeviceTypes::V32Kbd )
        {
            // there is a single host keyboard: allow only 1 gamepad
            // to use it, either as keyboard or as v32kbd (never both)
            if( IsKeyboardUsed )
              GamepadDevice->Type = DeviceTypes::NoDevice;
            
            else
            {
                IsKeyboardUsed = true;
                Console.SetGamepadConnection( Gamepad, true );
                
                // v32kbd also needs the text the host types, to report
                // the characters that are not ASCII keys
                SDL_StartTextInput();
            }
            
            continue;
        }
        
        if( GamepadDevice->Type == DeviceTypes::V32Mouse )
        {
            // there is a single host mouse: allow only 1 gamepad
            // to use it (it is independent of the keyboard)
            if( IsMouseUsed )
              GamepadDevice->Type = DeviceTypes::NoDevice;
            
            else
            {
                IsMouseUsed = true;
                Console.SetGamepadConnection( Gamepad, true );
            }
            
            continue;
        }
        
        // preemptively set an unused instance ID in case errors happen
        GamepadDevice->InstanceID = -1;
        
        // test every connected joystick
        for( auto Pair = ConnectedJoysticks.begin(); Pair != ConnectedJoysticks.end(); Pair++ )
        {
            SDL_JoystickID JoystickInstanceID = Pair->first;
            SDL_JoystickGUID JoystickGUID = Pair->second;
            
            if( GamepadDevice->GUID != JoystickGUID )
              continue;
            
            // for multiple identical joysticks, make sure
            // we are only using 1 for each separate gamepad
            if( MappedInstanceIDs.find( JoystickInstanceID ) == MappedInstanceIDs.end() )
            {
                MappedInstanceIDs.insert( JoystickInstanceID );
                GamepadDevice->InstanceID = JoystickInstanceID;
                Console.SetGamepadConnection( Gamepad, true );
                break;
            }
        }
    }
    
    // with no v32mouse in use, give the host mouse back
    if( !IsMouseUsed )
      ReleaseV32Mouse();
}

// -----------------------------------------------------------------------------

int GamepadsInput::GetKeyboardGamepad()
{
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
      if( MappedGamepads[ Gamepad ].Type == DeviceTypes::Keyboard )
        return Gamepad;
    
    return -1;
}

// -----------------------------------------------------------------------------

int GamepadsInput::GetV32KbdGamepad()
{
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
      if( MappedGamepads[ Gamepad ].Type == DeviceTypes::V32Kbd )
        return Gamepad;
    
    return -1;
}

// -----------------------------------------------------------------------------

int GamepadsInput::GetV32MouseGamepad()
{
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
      if( MappedGamepads[ Gamepad ].Type == DeviceTypes::V32Mouse )
        return Gamepad;
    
    return -1;
}


// =============================================================================
//      GAMEPADS INPUT: V32KBD DEVICE
// =============================================================================


void GamepadsInput::UpdateV32Kbd()
{
    // nothing to do when the device is not in use
    int Gamepad = GetV32KbdGamepad();
    
    if( Gamepad < 0 || !Console.HasGamepad( Gamepad ) )
    {
        ClearV32KbdInput();
        return;
    }
    
    // a key press still waiting for its text is a plain key press
    FlushV32KbdPendingKey();
    
    // with no new events, controls just keep their state
    if( V32KbdQueue.empty() )
      return;
    
    // report only 1 event per frame
    V32KbdEvent KeyEvent = V32KbdQueue.front();
    V32KbdQueue.pop_front();
    
    // buttons report the key code
    for( int Bit = 0; Bit < V32Kbd::CodeBits; Bit++ )
      Console.SetGamepadControl( Gamepad, V32Kbd::CodeControls[ Bit ], KeyEvent.KeyCode & (1 << Bit) );
    
    // vertical d-pad axis reports the key action (pressing
    // a direction makes the console release the opposite)
    Console.SetGamepadControl( Gamepad, (KeyEvent.Pressed? GamepadControls::Up : GamepadControls::Down), true );
    
    // horizontal d-pad axis is the strobe: switch sides. This is read
    // from its current state in the console (and not from a variable
    // of ours) so that it will still be right after loading a state
    bool LeftIsPressed = (Console.GamepadController.RealTimeGamepadStates[ Gamepad ].Left > 0);
    Console.SetGamepadControl( Gamepad, (LeftIsPressed? GamepadControls::Right : GamepadControls::Left), true );
}

// -----------------------------------------------------------------------------

void GamepadsInput::ClearV32KbdInput()
{
    V32KbdQueue.clear();
    V32KbdHasPendingKey = false;
    V32KbdPendingScancode = 0;
    V32KbdReplacedKeys.clear();
    V32KbdKeyRepeated = false;
}

// -----------------------------------------------------------------------------

// adds events that must stay together (a codepage prefix and its payload)
void GamepadsInput::QueueV32KbdEvents( const V32KbdEvent* Events, int Count )
{
    // if the program is not reading events, discard the oldest, but never
    // half of a character. If the prefix has been reported already the
    // payload stays, so there is no room for the new events: they are lost
    while( V32KbdQueue.size() + Count > V32Kbd::MaxQueuedEvents && !V32KbdQueue.empty() )
    {
        if( V32KbdQueue.front().Continued )
          return;
        
        V32KbdQueue.pop_front();
        
        if( !V32KbdQueue.empty() && V32KbdQueue.front().Continued )
          V32KbdQueue.pop_front();
    }
    
    for( int i = 0; i < Count; i++ )
      V32KbdQueue.push_back( Events[ i ] );
}

// -----------------------------------------------------------------------------

// types a character of the BIOS font (see the extended characters)
void GamepadsInput::QueueV32KbdCharacter( int Character )
{
    V32KbdEvent Events[ 2 ];
    
    // both events are key releases, so old libraries see no key press
    Events[ 0 ].KeyCode   = (Character >= 128)? V32Kbd::Key_PageHigh : V32Kbd::Key_PageLow;
    Events[ 0 ].Pressed   = false;
    Events[ 0 ].Continued = false;
    
    Events[ 1 ].KeyCode   = Character & 0x7F;
    Events[ 1 ].Pressed   = false;
    Events[ 1 ].Continued = true;
    
    QueueV32KbdEvents( Events, 2 );
}

// -----------------------------------------------------------------------------

// reports the key press that was waiting for the text it types, as a key
void GamepadsInput::FlushV32KbdPendingKey()
{
    if( !V32KbdHasPendingKey )
      return;
    
    V32KbdHasPendingKey = false;
    QueueV32KbdEvents( &V32KbdPendingKey, 1 );
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessV32KbdKey( SDL_Event Event )
{
    bool Pressed = (Event.type == SDL_KEYDOWN);
    int Scancode = (int)Event.key.keysym.scancode;
    
    // don't report automatic key retriggers, nor the text they type
    if( Pressed && Event.key.repeat )
    {
        V32KbdKeyRepeated = true;
        return;
    }
    
    // any other key event settles the press that was waiting for text
    FlushV32KbdPendingKey();
    
    // keys with no assigned code are not reported
    int KeyCode = V32Kbd::GetKeyCode( Event.key.keysym.scancode );
    
    if( KeyCode == V32Kbd::Key_None )
      return;
    
    V32KbdEvent KeyEvent;
    KeyEvent.KeyCode = KeyCode;
    KeyEvent.Pressed = Pressed;
    KeyEvent.Continued = false;
    
    if( !Pressed )
    {
        // a key whose press was replaced by a character has no release
        if( V32KbdReplacedKeys.erase( Scancode ) > 0 )
          return;
        
        QueueV32KbdEvents( &KeyEvent, 1 );
        return;
    }
    
    V32KbdKeyRepeated = false;
    V32KbdReplacedKeys.erase( Scancode );
    
    // keys that type an ASCII character may type something else in the
    // layout of the host (like an accented letter), which is not known
    // until the text comes just after: wait for it
    if( KeyCode >= 32 && KeyCode <= 126 )
    {
        V32KbdHasPendingKey = true;
        V32KbdPendingKey = KeyEvent;
        V32KbdPendingScancode = Scancode;
        return;
    }
    
    QueueV32KbdEvents( &KeyEvent, 1 );
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessV32KbdText( SDL_Event Event )
{
    // find the characters of the BIOS font that this text has. ASCII
    // is not among them: keys already report it
    int Extended[ 32 ];
    int Count = 0;
    
    const char* Text = Event.text.text;
    
    while( Count < 32 )
    {
        uint32_t Unicode = NextUTF8Character( Text );
        
        if( Unicode == 0 )
          break;
        
        int Character = V32Kbd::GetBiosCharacter( Unicode );
        
        if( Character >= 128 || (Character > 0 && Character < 32) )
          Extended[ Count++ ] = Character;
    }
    
    // text of the key that is waiting: a single extended character takes
    // the place of the key, and anything else leaves the key as it is
    if( V32KbdHasPendingKey )
    {
        if( Count == 1 )
        {
            V32KbdHasPendingKey = false;
            V32KbdReplacedKeys.insert( V32KbdPendingScancode );
            QueueV32KbdCharacter( Extended[ 0 ] );
            return;
        }
        
        FlushV32KbdPendingKey();
    }
    
    // text from retriggers is not reported, as their keys aren't
    if( V32KbdKeyRepeated )
      return;
    
    for( int i = 0; i < Count; i++ )
      QueueV32KbdCharacter( Extended[ i ] );
}


// =============================================================================
//      GAMEPADS INPUT: V32MOUSE DEVICE
// =============================================================================


// reads the current position of a v32mouse counter from the console's
// gamepad state (and not from a variable of ours) so that it will still
// be right after loading a state
static int ReadV32MouseCounter( int Gamepad, const V32Mouse::CounterControls& Counter )
{
    int32_t* ControlStates = &Console.GamepadController.RealTimeGamepadStates[ Gamepad ].Left;
    
    bool NegativePressed = (ControlStates[ (int)Counter.Negative ] > 0);
    bool PositivePressed = (ControlStates[ (int)Counter.Positive ] > 0);
    bool HighPressed     = (ControlStates[ (int)Counter.High     ] > 0);
    bool LowPressed      = (ControlStates[ (int)Counter.Low      ] > 0);
    
    // Gray code to group: 00 -> 0, 01 -> 1, 11 -> 2, 10 -> 3
    int Group = HighPressed? (LowPressed? 2 : 3) : (LowPressed? 1 : 0);
    
    // trit: 0 = negative, 1 = none, 2 = positive
    // (the console never has both of them pressed)
    int Trit = NegativePressed? 0 : (PositivePressed? 2 : 1);
    
    // odd groups walk the trit backwards
    return 3 * Group + ((Group & 1)? 2 - Trit : Trit);
}

// -----------------------------------------------------------------------------

// sets the controls of a v32mouse counter for the given position
static void WriteV32MouseCounter( int Gamepad, const V32Mouse::CounterControls& Counter, int Position )
{
    int Group = Position / 3;
    int Index = Position % 3;
    int Trit  = (Group & 1)? 2 - Index : Index;
    int Gray  = Group ^ (Group >> 1);
    
    Console.SetGamepadControl( Gamepad, Counter.High, (Gray & 2) != 0 );
    Console.SetGamepadControl( Gamepad, Counter.Low,  (Gray & 1) != 0 );
    
    // pressing a direction makes the console release the opposite one
    if( Trit == 0 )
      Console.SetGamepadControl( Gamepad, Counter.Negative, true );
    
    else if( Trit == 2 )
      Console.SetGamepadControl( Gamepad, Counter.Positive, true );
    
    else
    {
        Console.SetGamepadControl( Gamepad, Counter.Negative, false );
        Console.SetGamepadControl( Gamepad, Counter.Positive, false );
    }
}

// -----------------------------------------------------------------------------

// moves a v32mouse counter as much as allowed in 1 frame, taking
// those steps from the pending host motion (in host motion units)
static void StepV32MouseCounter( int Gamepad, const V32Mouse::CounterControls& Counter, int& PendingMotion, int CountsPerStep )
{
    // integer division truncates towards 0, so any remainder is
    // kept for later frames, and it keeps the motion's direction
    int Steps = PendingMotion / CountsPerStep;
    Clamp( Steps, -V32Mouse::MaxStepsPerFrame, V32Mouse::MaxStepsPerFrame );
    
    if( Steps == 0 )
      return;
    
    PendingMotion -= Steps * CountsPerStep;
    
    int Position = ReadV32MouseCounter( Gamepad, Counter );
    Position = (Position + Steps + V32Mouse::Positions) % V32Mouse::Positions;
    WriteV32MouseCounter( Gamepad, Counter, Position );
}

// -----------------------------------------------------------------------------

void GamepadsInput::ClearV32MouseInput()
{
    V32MousePendingX = 0;
    V32MousePendingY = 0;
    
    for( int Button = 0; Button < V32Mouse::ButtonsCount; Button++ )
    {
        V32MouseButtonQueues[ Button ].clear();
        V32MouseButtonTargets[ Button ] = false;
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::UpdateV32Mouse()
{
    // nothing to do when the device is not in use
    int Gamepad = GetV32MouseGamepad();
    
    if( Gamepad < 0 || !Console.HasGamepad( Gamepad ) )
    {
        ClearV32MouseInput();
        return;
    }
    
    // buttons: show at most 1 change per frame for each one, so that
    // a press and release within the same frame is never missed
    for( int Button = 0; Button < V32Mouse::ButtonsCount; Button++ )
    {
        deque< bool >& Queue = V32MouseButtonQueues[ Button ];
        
        if( Queue.empty() )
          continue;
        
        Console.SetGamepadControl( Gamepad, V32Mouse::ButtonControls[ Button ], Queue.front() );
        Queue.pop_front();
    }
    
    // movement: step both counters
    StepV32MouseCounter( Gamepad, V32Mouse::CounterX, V32MousePendingX, V32MouseCountsPerStep );
    StepV32MouseCounter( Gamepad, V32Mouse::CounterY, V32MousePendingY, V32MouseCountsPerStep );
}

// -----------------------------------------------------------------------------

bool GamepadsInput::IsV32MouseCaptured()
{
    return V32MouseCaptured;
}

// -----------------------------------------------------------------------------

bool GamepadsInput::V32MouseCanCapture()
{
    // the device must be in use
    int Gamepad = GetV32MouseGamepad();
    
    if( Gamepad < 0 || !Console.HasGamepad( Gamepad ) )
      return false;
    
    // and there must be a program running
    return Emulator.IsPowerOn();
}

// -----------------------------------------------------------------------------

void GamepadsInput::CaptureV32Mouse()
{
    if( V32MouseCaptured )
      return;
    
    // relative mode hides the host pointer, keeps it within
    // our window and reports motion even past screen edges
    if( SDL_SetRelativeMouseMode( SDL_TRUE ) != 0 )
    {
        LOG( "v32mouse: cannot capture the mouse: " + string( SDL_GetError() ) );
        return;
    }
    
    LOG( "v32mouse: mouse captured" );
    V32MouseCaptured = true;
    
    // motion before the capture is not reported
    V32MousePendingX = 0;
    V32MousePendingY = 0;
    
    // the GUI is not shown while the mouse is captured
    MouseIsOnWindow = false;
}

// -----------------------------------------------------------------------------

void GamepadsInput::ReleaseV32Mouse()
{
    if( !V32MouseCaptured )
      return;
    
    SDL_SetRelativeMouseMode( SDL_FALSE );
    
    LOG( "v32mouse: mouse released" );
    V32MouseCaptured = false;
    
    // discard pending motion
    V32MousePendingX = 0;
    V32MousePendingY = 0;
    
    // buttons held at this point will not get their release events
    // reported, so programs need to see them released now
    for( int Button = 0; Button < V32Mouse::ButtonsCount; Button++ )
    {
        if( !V32MouseButtonTargets[ Button ] )
          continue;
        
        V32MouseButtonQueues[ Button ].push_back( false );
        V32MouseButtonTargets[ Button ] = false;
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::CheckV32MouseCapture()
{
    if( V32MouseCaptured && !V32MouseCanCapture() )
      ReleaseV32Mouse();
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessV32MouseMotion( SDL_Event Event )
{
    // motion is only reported while the mouse is captured
    // (and touch screens are not taken as a mouse)
    if( !V32MouseCaptured || Event.motion.which == SDL_TOUCH_MOUSEID )
      return;
    
    // if the program is not reading the counters fast enough,
    // discard the excess so that the pointer won't drift on
    int MaxPending = V32Mouse::MaxPendingSteps * V32MouseCountsPerStep;
    
    V32MousePendingX += Event.motion.xrel;
    V32MousePendingY += Event.motion.yrel;
    Clamp( V32MousePendingX, -MaxPending, MaxPending );
    Clamp( V32MousePendingY, -MaxPending, MaxPending );
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessV32MouseButton( SDL_Event Event )
{
    if( Event.button.which == SDL_TOUCH_MOUSEID )
      return;
    
    bool Pressed = (Event.type == SDL_MOUSEBUTTONDOWN);
    
    // when not captured, a left click on the game screen captures the
    // mouse. That click is not reported (its release is ignored too,
    // since the button was never shown as pressed). Clicks on the GUI
    // are left for it
    if( !V32MouseCaptured )
    {
        if( Pressed && Event.button.button == SDL_BUTTON_LEFT )
          if( V32MouseCanCapture() && !ImGui::GetIO().WantCaptureMouse )
            CaptureV32Mouse();
        
        return;
    }
    
    int Button = V32Mouse::GetButtonIndex( Event.button.button );
    
    if( Button < 0 )
      return;
    
    // ignore redundant changes
    if( Pressed == V32MouseButtonTargets[ Button ] )
      return;
    
    // if the program is not reading buttons, skip
    // to the latest state instead of growing the queue
    deque< bool >& Queue = V32MouseButtonQueues[ Button ];
    
    if( Queue.size() >= V32Mouse::MaxQueuedChanges )
      Queue.clear();
    
    Queue.push_back( Pressed );
    V32MouseButtonTargets[ Button ] = Pressed;
}


// =============================================================================
//      GAMEPADS INPUT: PROCESSING INPUT EVENTS
// =============================================================================


void GamepadsInput::ProcessEvent( SDL_Event Event )
{
    switch( Event.type )
    {
        case SDL_JOYDEVICEADDED:
            ProcessJoystickAdded( Event );
            break;
        case SDL_JOYDEVICEREMOVED:
            ProcessJoystickRemoved( Event );
            break;
        case SDL_JOYAXISMOTION:
            ProcessJoystickAxisMotion( Event );
            break;
        case SDL_JOYHATMOTION:
            ProcessJoystickHatMotion( Event );
            break;
        case SDL_JOYBUTTONDOWN:
            ProcessJoystickButtonDown( Event );
            break;
        case SDL_JOYBUTTONUP:
            ProcessJoystickButtonUp( Event );
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            
            // when v32kbd is in use it takes every key. It can't
            // coexist with the keyboard device so there's no overlap
            if( GetV32KbdGamepad() >= 0 )
              ProcessV32KbdKey( Event );
            
            else if( Event.type == SDL_KEYDOWN )
              ProcessKeyDown( Event );
            
            else
              ProcessKeyUp( Event );
            
            break;
        
        // v32kbd also reports the characters the host types
        case SDL_TEXTINPUT:
            if( GetV32KbdGamepad() >= 0 )
              ProcessV32KbdText( Event );
            break;
        
        // the host mouse is only used by the v32mouse device
        case SDL_MOUSEMOTION:
            if( GetV32MouseGamepad() >= 0 )
              ProcessV32MouseMotion( Event );
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if( GetV32MouseGamepad() >= 0 )
              ProcessV32MouseButton( Event );
            break;
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickAdded( SDL_Event Event )
{
    // access the joystick
    SDL_Joystick* NewJoystick = SDL_JoystickOpen( Event.jdevice.which );
    
    if( NewJoystick )
    {
        // find out joystick instance ID and GUID
        SDL_JoystickGUID NewGUID = SDL_JoystickGetGUID( NewJoystick );
        Sint32 AddedInstanceID = SDL_JoystickInstanceID( NewJoystick );
        
        // update the list of connected joysticks
        ConnectedJoysticks[ AddedInstanceID ] = NewGUID;
    }
    
    // detect joysticks and assign them to gamepads
    AssignInputDevices();
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickRemoved( SDL_Event Event )
{
    // find out joystick instance ID and GUID
    Sint32 RemovedInstanceID = Event.jdevice.which;
    SDL_Joystick* OldJoystick = SDL_JoystickFromInstanceID( RemovedInstanceID );
    
    // update the list of connected joysticks
    ConnectedJoysticks.erase( RemovedInstanceID );
    SDL_JoystickClose( OldJoystick );
    
    // detect joysticks and assign them to gamepads
    AssignInputDevices();
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickAxisMotion( SDL_Event Event )
{
    Uint8 AxisIndex = Event.jaxis.axis;
    Sint16 AxisPosition = Event.jaxis.value;
    Sint32 InstanceID = Event.jaxis.which;
    SDL_Joystick* Joystick = SDL_JoystickFromInstanceID( InstanceID );
    SDL_JoystickGUID GUID = SDL_JoystickGetGUID( Joystick );
    
    // we need to process both directions in this axis
    // at the same time, because they are correlated.
    // But be careful because it could happen that not
    // both directions have been mapped
    
    // joystick could be analog, so allow for
    // a dead zone in the center of +/- 50%
    bool PositivePressed = (AxisPosition > +16000);
    bool NegativePressed = (AxisPosition < -16000);
    
    // check all gamepads
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is a joystick
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Joystick )
          continue;
          
        // check if mapped device is this specific joystick
        if( MappedGamepads[ Gamepad ].InstanceID != InstanceID
        ||  MappedGamepads[ Gamepad ].GUID != GUID )
          continue;
        
        // obtain the applicable joystick profile
        auto Position = JoystickProfiles.find( GUID );
        
        if( Position == JoystickProfiles.end() )
          continue;
        
        JoystickMapping* JoystickProfile = Position->second;
        
        // if the command button is mapped to an axis
        // check it before any regular controls
        if( JoystickProfile->Command.IsAxis() )
          if( AxisIndex == JoystickProfile->Command.AxisIndex )
            CommandPressed[ Gamepad ] = JoystickProfile->Command.AxisPositive? PositivePressed : NegativePressed;
        
        // check the mapped axes for directions
        if( JoystickProfile->Left.IsAxis() )
          if( AxisIndex == JoystickProfile->Left.AxisIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Left, JoystickProfile->Left.AxisPositive? PositivePressed : NegativePressed );
        
        if( JoystickProfile->Right.IsAxis() )
          if( AxisIndex == JoystickProfile->Right.AxisIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Right, JoystickProfile->Right.AxisPositive? PositivePressed : NegativePressed );
        
        if( JoystickProfile->Up.IsAxis() )
          if( AxisIndex == JoystickProfile->Up.AxisIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Up, JoystickProfile->Up.AxisPositive? PositivePressed : NegativePressed );
        
        if( JoystickProfile->Down.IsAxis() )
          if( AxisIndex == JoystickProfile->Down.AxisIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Down, JoystickProfile->Down.AxisPositive? PositivePressed : NegativePressed );
        
        // when command is pressed, check only for button combinations
        // (regular button presses are ignored until command is released)
        if( CommandPressed[ Gamepad ] )
        {
            // hold Command + press X = Reset
            if( JoystickProfile->ButtonX.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonX.AxisIndex )
              {
                  bool IsPressed = JoystickProfile->ButtonX.AxisPositive? PositivePressed : NegativePressed;
                  
                  if( IsPressed )
                    Console.Reset();
              }
            
            // hold Command + press L = Save state
            if( JoystickProfile->ButtonL.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonL.AxisIndex )
              {
                  bool IsPressed = JoystickProfile->ButtonL.AxisPositive? PositivePressed : NegativePressed;
                  
                  if( IsPressed )
                  {
                      GUI_SaveState();
                      CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
                  }
              }
            
            // hold Command + press R = Load state
            if( JoystickProfile->ButtonR.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonR.AxisIndex )
              {
                  bool IsPressed = JoystickProfile->ButtonR.AxisPositive? PositivePressed : NegativePressed;
                  
                  if( IsPressed && CommandPressed[ Gamepad ] )
                  {
                      GUI_LoadState();
                      CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
                  }
              }
            
            // hold Command + press Start = Quit emulator
            if( JoystickProfile->ButtonStart.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonStart.AxisIndex )
              {
                  bool IsPressed = JoystickProfile->ButtonStart.AxisPositive? PositivePressed : NegativePressed;
                  
                  if( IsPressed )
                    GlobalLoopActive = false;
              }
        }
        
        // only when command is not pressed check the mapped axes for buttons
        else
        {
            if( JoystickProfile->ButtonA.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonA.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, JoystickProfile->ButtonA.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonB.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonB.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, JoystickProfile->ButtonB.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonY.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonY.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, JoystickProfile->ButtonY.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonX.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonX.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, JoystickProfile->ButtonX.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonL.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonL.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, JoystickProfile->ButtonL.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonR.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonR.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, JoystickProfile->ButtonR.AxisPositive? PositivePressed : NegativePressed );
            
            if( JoystickProfile->ButtonStart.IsAxis() )
              if( AxisIndex == JoystickProfile->ButtonStart.AxisIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, JoystickProfile->ButtonStart.AxisPositive? PositivePressed : NegativePressed );
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickHatMotion( SDL_Event Event )
{
    Uint8 HatIndex = Event.jhat.hat;
    Uint8 HatDirection = Event.jhat.value;
    Sint32 InstanceID = Event.jhat.which;
    SDL_Joystick* Joystick = SDL_JoystickFromInstanceID( InstanceID );
    SDL_JoystickGUID GUID = SDL_JoystickGetGUID( Joystick );
    
    // SDL treats hat directions similar to a d-pad: we are
    // given its current direction with combinable flags
    
    // check all gamepads
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is a joystick
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Joystick )
          continue;
        
        // check if mapped device is this specific joystick
        if( MappedGamepads[ Gamepad ].InstanceID != InstanceID
        ||  MappedGamepads[ Gamepad ].GUID != GUID )
          continue;
        
        // obtain the applicable joystick profile
        auto Position = JoystickProfiles.find( GUID );
        
        if( Position == JoystickProfiles.end() )
          continue;
        
        JoystickMapping* JoystickProfile = Position->second;
        
        // if the command button is mapped to a hat direction
        // check it before any regular controls
        if( JoystickProfile->Command.IsHat() )
          if( HatIndex == JoystickProfile->Command.HatIndex )
            CommandPressed[ Gamepad ] = (bool)(HatDirection & JoystickProfile->Command.HatDirection);
        
        // check the mapped hat directions for directions
        if( JoystickProfile->Left.IsHat() )
          if( HatIndex == JoystickProfile->Left.HatIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Left, (bool)(HatDirection & JoystickProfile->Left.HatDirection) );
        
        if( JoystickProfile->Right.IsHat() )
          if( HatIndex == JoystickProfile->Right.HatIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Right, (bool)(HatDirection & JoystickProfile->Right.HatDirection) );
        
        if( JoystickProfile->Up.IsHat() )
          if( HatIndex == JoystickProfile->Up.HatIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Up, (bool)(HatDirection & JoystickProfile->Up.HatDirection) );
        
        if( JoystickProfile->Down.IsHat() )
          if( HatIndex == JoystickProfile->Down.HatIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Down, (bool)(HatDirection & JoystickProfile->Down.HatDirection) );
        
        // when command is pressed, check only for button combinations
        // (regular button presses are ignored until command is released)
        if( CommandPressed[ Gamepad ] )
        {
            // hold Command + press X = Reset
            if( JoystickProfile->ButtonX.IsHat() )
              if( HatIndex == JoystickProfile->ButtonX.HatIndex )
              {
                  bool IsPressed = (bool)(HatDirection & JoystickProfile->ButtonX.HatDirection);
                  
                  if( IsPressed )
                    Console.Reset();
              }
            
            // hold Command + press L = Save state
            if( JoystickProfile->ButtonL.IsHat() )
              if( HatIndex == JoystickProfile->ButtonL.HatIndex )
              {
                  bool IsPressed = (bool)(HatDirection & JoystickProfile->ButtonL.HatDirection);
                  
                  if( IsPressed )
                  {
                      GUI_SaveState();
                      CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
                  }
              }
            
            // hold Command + press R = Load state
            if( JoystickProfile->ButtonR.IsHat() )
              if( HatIndex == JoystickProfile->ButtonR.HatIndex )
              {
                  bool IsPressed = (bool)(HatDirection & JoystickProfile->ButtonR.HatDirection);
                  
                  if( IsPressed && CommandPressed[ Gamepad ] )
                  {
                      GUI_LoadState();
                      CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
                  }
              }
            
            // hold Command + press Start = Quit emulator
            if( JoystickProfile->ButtonStart.IsHat() )
              if( HatIndex == JoystickProfile->ButtonStart.HatIndex )
              {
                  bool IsPressed = (bool)(HatDirection & JoystickProfile->ButtonStart.HatDirection);
                  
                  if( IsPressed )
                    GlobalLoopActive = false;
              }
        }
        
        // only when command is not pressed check the mapped hat directions for buttons
        else
        {
            if( !JoystickProfile->ButtonA.IsHat() )
              if( HatIndex == JoystickProfile->ButtonA.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, (bool)(HatDirection & JoystickProfile->ButtonA.HatDirection) );
            
            if( !JoystickProfile->ButtonB.IsHat() )
              if( HatIndex == JoystickProfile->ButtonB.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, (bool)(HatDirection & JoystickProfile->ButtonB.HatDirection) );
            
            if( !JoystickProfile->ButtonX.IsHat() )
              if( HatIndex == JoystickProfile->ButtonX.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, (bool)(HatDirection & JoystickProfile->ButtonX.HatDirection) );
            
            if( !JoystickProfile->ButtonY.IsHat() )
              if( HatIndex == JoystickProfile->ButtonY.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, (bool)(HatDirection & JoystickProfile->ButtonY.HatDirection) );
            
            if( !JoystickProfile->ButtonL.IsHat() )
              if( HatIndex == JoystickProfile->ButtonL.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, (bool)(HatDirection & JoystickProfile->ButtonL.HatDirection) );
            
            if( !JoystickProfile->ButtonR.IsHat() )
              if( HatIndex == JoystickProfile->ButtonR.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, (bool)(HatDirection & JoystickProfile->ButtonR.HatDirection) );
            
            if( !JoystickProfile->ButtonStart.IsHat() )
              if( HatIndex == JoystickProfile->ButtonStart.HatIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, (bool)(HatDirection & JoystickProfile->ButtonStart.HatDirection) );
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickButtonDown( SDL_Event Event )
{
    Uint8 ButtonIndex = Event.jbutton.button;
    Sint32 InstanceID = Event.jbutton.which;
    SDL_Joystick* Joystick = SDL_JoystickFromInstanceID( InstanceID );
    SDL_JoystickGUID GUID = SDL_JoystickGetGUID( Joystick );
    
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is a joystick
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Joystick )
          continue;
        
        // check if mapped device is this specific joystick
        if( MappedGamepads[ Gamepad ].InstanceID != InstanceID
        ||  MappedGamepads[ Gamepad ].GUID != GUID )
          continue;
        
        // obtain the applicable joystick profile
        auto Position = JoystickProfiles.find( GUID );
        
        if( Position == JoystickProfiles.end() )
          continue;
        
        JoystickMapping* JoystickProfile = Position->second;
        
        // if the command button is mapped to a joystick button
        // check it before any regular controls
        if( JoystickProfile->Command.IsButton() )
          if( ButtonIndex == JoystickProfile->Command.ButtonIndex )
            CommandPressed[ Gamepad ] = true;
        
        // check the mapped buttons for directions
        if( JoystickProfile->Left.IsButton() )
          if( ButtonIndex == JoystickProfile->Left.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Left, true );
          
        if( JoystickProfile->Right.IsButton() )
          if( ButtonIndex == JoystickProfile->Right.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Right, true );
          
        if( JoystickProfile->Up.IsButton() )
          if( ButtonIndex == JoystickProfile->Up.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Up, true );
          
        if( JoystickProfile->Down.IsButton() )
          if( ButtonIndex == JoystickProfile->Down.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Down, true );
          
        // when command is pressed, check only for button combinations
        // (regular button presses are ignored until command is released)
        if( CommandPressed[ Gamepad ] )
        {
            // hold Command + press X = Reset
            if( JoystickProfile->ButtonX.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonX.ButtonIndex )
                Console.Reset();
            
            // hold Command + press L = Save state
            if( JoystickProfile->ButtonL.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonL.ButtonIndex )
              {
                  GUI_SaveState();
                  CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
              }
            
            // hold Command + press R = Load state
            if( JoystickProfile->ButtonR.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonR.ButtonIndex )
              {
                  GUI_LoadState();
                  CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
              }
            
            // hold Command + press Start = Quit emulator
            if( JoystickProfile->ButtonStart.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonStart.ButtonIndex )
                GlobalLoopActive = false;
        }
        
        // only when command is not pressed check the mapped joystick buttons for buttons
        else
        {
            // check the mapped buttons for buttons
            if( JoystickProfile->ButtonA.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonA.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, true );
            
            if( JoystickProfile->ButtonB.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonB.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, true );
            
            if( JoystickProfile->ButtonX.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonX.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, true );
            
            if( JoystickProfile->ButtonY.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonY.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, true );
              
            if( JoystickProfile->ButtonL.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonL.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, true );
            
            if( JoystickProfile->ButtonR.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonR.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, true );
            
            if( JoystickProfile->ButtonStart.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonStart.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, true );
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessJoystickButtonUp( SDL_Event Event )
{
    Uint8 ButtonIndex = Event.jbutton.button;
    Sint32 InstanceID = Event.jbutton.which;
    SDL_Joystick* Joystick = SDL_JoystickFromInstanceID( InstanceID );
    SDL_JoystickGUID GUID = SDL_JoystickGetGUID( Joystick );
    
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is a joystick
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Joystick )
          continue;
        
        // check if mapped device is this specific joystick
        if( MappedGamepads[ Gamepad ].InstanceID != InstanceID
        ||  MappedGamepads[ Gamepad ].GUID != GUID )
          continue;
        
        // obtain the applicable joystick profile
        auto Position = JoystickProfiles.find( GUID );
        
        if( Position == JoystickProfiles.end() )
          continue;
        
        JoystickMapping* JoystickProfile = Position->second;
        
        // if the command button is mapped to a joystick button
        // check it before any regular controls
        if( JoystickProfile->Command.IsButton() )
          if( ButtonIndex == JoystickProfile->Command.ButtonIndex )
            CommandPressed[ Gamepad ] = false;
        
        // check the mapped buttons for directions
        if( JoystickProfile->Left.IsButton() )
          if( ButtonIndex == JoystickProfile->Left.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Left, false );
          
        if( JoystickProfile->Right.IsButton() )
          if( ButtonIndex == JoystickProfile->Right.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Right, false );
          
        if( JoystickProfile->Up.IsButton() )
          if( ButtonIndex == JoystickProfile->Up.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Up, false );
          
        if( JoystickProfile->Down.IsButton() )
          if( ButtonIndex == JoystickProfile->Down.ButtonIndex )
            Console.SetGamepadControl( Gamepad, GamepadControls::Down, false );
          
        // only when command is not pressed check the mapped buttons for buttons
        if( !CommandPressed[ Gamepad ] )
        {
            if( JoystickProfile->ButtonA.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonA.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, false );
            
            if( JoystickProfile->ButtonB.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonB.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, false );
            
            if( JoystickProfile->ButtonX.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonX.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, false );
            
            if( JoystickProfile->ButtonY.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonY.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, false );
              
            if( JoystickProfile->ButtonL.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonL.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, false );
            
            if( JoystickProfile->ButtonR.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonR.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, false );
            
            if( JoystickProfile->ButtonStart.IsButton() )
              if( ButtonIndex == JoystickProfile->ButtonStart.ButtonIndex )
                Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, false );
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessKeyDown( SDL_Event Event )
{
    // don't process automatic key retriggers
    if( Event.key.repeat ) return;
    
    // ignore keypresses when control is pressed,
    // so that keyboard shortcuts will not interfere
    SDL_Keycode KeyCode = Event.key.keysym.sym;
    bool ControlIsPressed = (SDL_GetModState() & KMOD_CTRL);
    if( ControlIsPressed ) return;
    
    // in other cases process the key normally
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is the keyboard
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Keyboard )
          continue;
        
        // if the command button is mapped to a key
        // check it before any regular controls
        if( KeyboardProfile.Command >= 0 )
          if( KeyCode == KeyboardProfile.Command )
            CommandPressed[ Gamepad ] = true;
        
        // check the mapped keys for directions
        if( KeyCode == KeyboardProfile.Left )
          Console.SetGamepadControl( Gamepad, GamepadControls::Left, true );
          
        if( KeyCode == KeyboardProfile.Right )
          Console.SetGamepadControl( Gamepad, GamepadControls::Right, true );
          
        if( KeyCode == KeyboardProfile.Up )
          Console.SetGamepadControl( Gamepad, GamepadControls::Up, true );
          
        if( KeyCode == KeyboardProfile.Down )
          Console.SetGamepadControl( Gamepad, GamepadControls::Down, true );
          
        // when command is pressed, check only for button combinations
        // (regular button presses are ignored until command is released)
        if( CommandPressed[ Gamepad ] )
        {
            // hold Command + press X = Reset
            if( KeyCode == KeyboardProfile.ButtonX )
              Console.Reset();
            
            // hold Command + press L = Save state
            if( KeyCode == KeyboardProfile.ButtonL )
            {
                GUI_SaveState();
                CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
            }
            
            // hold Command + press R = Load state
            if( KeyCode == KeyboardProfile.ButtonR )
            {
                GUI_LoadState();
                CancelDelayedMessageBox();  // for these combinations inhibit any GUI messages
            }
            
            // hold Command + press Start = Quit emulator
            if( KeyCode == KeyboardProfile.ButtonStart )
              GlobalLoopActive = false;
        }
        
        // only when command is not pressed check the mapped keys for buttons
        else
        {
            if( KeyCode == KeyboardProfile.ButtonA )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, true );
            
            if( KeyCode == KeyboardProfile.ButtonB )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, true );
            
            if( KeyCode == KeyboardProfile.ButtonX )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, true );
            
            if( KeyCode == KeyboardProfile.ButtonY )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, true );
              
            if( KeyCode == KeyboardProfile.ButtonL )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, true );
            
            if( KeyCode == KeyboardProfile.ButtonR )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, true );
            
            if( KeyCode == KeyboardProfile.ButtonStart )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, true );
        }
    }
}

// -----------------------------------------------------------------------------

void GamepadsInput::ProcessKeyUp( SDL_Event Event )
{
    // ignore keypresses when control is pressed,
    // so that keyboard shortcuts will not interfere
    SDL_Keycode KeyCode = Event.key.keysym.sym;
    bool ControlIsPressed = (SDL_GetModState() & KMOD_CTRL);
    if( ControlIsPressed ) return;
    
    // in other cases process the key normally
    for( int Gamepad = 0; Gamepad < Constants::GamepadPorts; Gamepad++ )
    {
        // non-connected gamepads are ignored
        if( !Console.HasGamepad( Gamepad ) )
          continue;
        
        // check if mapped device is the keyboard
        if( MappedGamepads[ Gamepad ].Type != DeviceTypes::Keyboard )
          continue;
        
        // if the command button is mapped to a key
        // check it before any regular controls
        if( KeyboardProfile.Command >= 0 )
          if( KeyCode == KeyboardProfile.Command )
            CommandPressed[ Gamepad ] = false;
        
        // check the mapped keys for directions
        if( KeyCode == KeyboardProfile.Left )
          Console.SetGamepadControl( Gamepad, GamepadControls::Left, false );
          
        if( KeyCode == KeyboardProfile.Right )
          Console.SetGamepadControl( Gamepad, GamepadControls::Right, false );
          
        if( KeyCode == KeyboardProfile.Up )
          Console.SetGamepadControl( Gamepad, GamepadControls::Up, false );
          
        if( KeyCode == KeyboardProfile.Down )
          Console.SetGamepadControl( Gamepad, GamepadControls::Down, false );
          
        // only when command is not pressed check the mapped buttons for buttons
        if( !CommandPressed[ Gamepad ] )
        {
            // check the mapped keys for buttons
            if( KeyCode == KeyboardProfile.ButtonA )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonA, false );
            
            if( KeyCode == KeyboardProfile.ButtonB )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonB, false );
            
            if( KeyCode == KeyboardProfile.ButtonX )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonX, false );
            
            if( KeyCode == KeyboardProfile.ButtonY )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonY, false );
              
            if( KeyCode == KeyboardProfile.ButtonL )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonL, false );
            
            if( KeyCode == KeyboardProfile.ButtonR )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonR, false );
            
            if( KeyCode == KeyboardProfile.ButtonStart )
              Console.SetGamepadControl( Gamepad, GamepadControls::ButtonStart, false );
        }
    }
}
