// *****************************************************************************
    // include console logic headers
    #include "ConsoleLogic/V32Console.hpp"
    
    // include infrastructure headers
    #include "DesktopInfrastructure/FilePaths.hpp"
    #include "DesktopInfrastructure/Logger.hpp"
    
    // include emulator headers
    #include "Tracing.hpp"
    #include "GUI.hpp"
    #include "Globals.hpp"
    #include "Languages.hpp"
    
    // include C/C++ headers
    #include <string>           // [ C++ STL ] Strings
    
    // include SDL2 headers
    #include "SDL.h"            // [ SDL2 ] Main header
    
    // creating folders
    #if defined(WINDOWS_OS)
      #include <windows.h>      // [ WINDOWS ] Main header
    #else
      #include <sys/stat.h>     // [ POSIX ] File status
      #include <sys/types.h>    // [ POSIX ] Types
    #endif
    
    // declare used namespaces
    using namespace std;
    using namespace V32;
// *****************************************************************************


// requests posted by the GUI and the keyboard
static bool ToggleRequested = false;
static bool MarkerRequested = false;


// =============================================================================
//      FOLDER FOR THE REPORTS
// =============================================================================


static string GetTracesFolder()
{
    return EmulatorFolder + "Traces" + PathSeparator;
}

// -----------------------------------------------------------------------------

static void EnsureTracesFolder()
{
    string Folder = GetTracesFolder();
    
    if( DirectoryExists( Folder ) )
      return;
    
    #if defined(WINDOWS_OS)
      CreateDirectoryW( ToUTF16( Folder ).c_str(), nullptr );
    #else
      mkdir( Folder.c_str(), 0777 );
    #endif
}


// =============================================================================
//      REQUESTS
// =============================================================================


void Trace_RequestToggle()
{
    ToggleRequested = true;
}

// -----------------------------------------------------------------------------

void Trace_RequestMarker()
{
    MarkerRequested = true;
}

// -----------------------------------------------------------------------------

void Trace_ProcessRequests()
{
    if( ToggleRequested )
    {
        ToggleRequested = false;
        
        if( !Console.Trace.Armed )
        {
            // from now on, every run of a cartridge is recorded
            // (and one that is already running, from this moment)
            EnsureTracesFolder();
            
            if( !DirectoryExists( GetTracesFolder() ) )
            {
                DelayedMessageBox
                (
                    SDL_MESSAGEBOX_ERROR,
                    "Error",
                    (Texts( TextIDs::Dialogs_TraceFolderError_Label ) + GetTracesFolder()).c_str()
                );
                return;
            }
            
            Console.Trace.OutputFolder = GetTracesFolder();
            Console.Trace.Arm( true );
            LOG( "Trace recording ON, reports go to " + Console.Trace.OutputFolder );
            
            DelayedMessageBox
            (
                SDL_MESSAGEBOX_INFORMATION,
                Texts( TextIDs::Dialogs_Done ),
                (Texts( TextIDs::Dialogs_TraceOn_Label ) + Console.Trace.OutputFolder).c_str()
            );
        }
        else
        {
            // stopping saves the report
            Console.Trace.Arm( false );
            LOG( "Trace recording OFF, last report: " + Console.Trace.LastReportPath );
            
            if( !Console.Trace.LastReportPath.empty() )
              DelayedMessageBox
              (
                  SDL_MESSAGEBOX_INFORMATION,
                  Texts( TextIDs::Dialogs_Done ),
                  (Texts( TextIDs::Dialogs_TraceSaved_Label ) + Console.Trace.LastReportPath).c_str()
              );
            else
              DelayedMessageBox
              (
                  SDL_MESSAGEBOX_ERROR,
                  "Error",
                  (Texts( TextIDs::Dialogs_TraceFolderError_Label ) + Console.Trace.OutputFolder).c_str()
              );
        }
    }
    
    if( MarkerRequested )
    {
        MarkerRequested = false;
        Console.Trace.AddMarker( "" );
    }
}
