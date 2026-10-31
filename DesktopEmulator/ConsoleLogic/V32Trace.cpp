// *****************************************************************************
    // include common Vircon32 headers
    #include "../VirconDefinitions/Constants.hpp"
    #include "../VirconDefinitions/Enumerations.hpp"

    // include console logic headers
    #include "V32Trace.hpp"
    #include "V32Console.hpp"
    #include "ExternalInterfaces.hpp"

    // include C/C++ headers
    #include <algorithm>        // [ C++ STL ] Algorithms
    #include <cstdarg>          // [ ANSI C ] Variable arguments
    #include <cstdio>           // [ ANSI C ] Standard I/O
    #include <cstdlib>          // [ ANSI C ] General utilities
    #include <cstring>          // [ ANSI C ] Strings
    #include <ctime>            // [ ANSI C ] Time and date
    #include <fstream>          // [ C++ STL ] File streams
    #include <map>              // [ C++ STL ] Maps
    #include <sstream>          // [ C++ STL ] String streams

    using namespace std;
// *****************************************************************************


namespace V32
{
    // =============================================================================
    //      CONSTANTS OF THE TRACE
    // =============================================================================


    // size of the cartridge program space and limits of the recording
    static const uint32_t MaximumHits = 64;
    static const uint32_t HoldFrames = 60;       // frames a trap screen stays before the cartridge restarts
    static const uint32_t MaximumFrames = 4 * 1024 * 1024;
    static const size_t MaximumMarkers = 64;
    static const size_t MaximumEvents = 200;

    // frame flags
    static const uint8_t FrameOverran = 1;      // the cart did not reach WAIT in time
    static const uint8_t FrameCPUError = 2;     // the CPU raised a hardware error
    static const uint8_t FrameHeld = 8;         // the cartridge was stopped in a trap routine
    static const uint8_t FrameCatchUp = 4;      // run as the 2nd or later frame of a catch-up burst

    // gamepad bits in the per frame record, and the names that the
    // V32_INPUT syntax of v32sim / gbprof uses for them (Y has no name there)
    static const int ButtonCount = 11;
    static const char* const ButtonNames[ ButtonCount ] =
    {
        "LEFT", "RIGHT", "UP", "DOWN", "START", "A", "B", "SELECT", nullptr, "L", "R"
    };


    // =============================================================================
    //      AUXILIARY FUNCTIONS
    // =============================================================================


    static string Format( const char* Pattern, ... )
    {
        char Buffer[ 4096 ];
        va_list Arguments;
        va_start( Arguments, Pattern );
        vsnprintf( Buffer, sizeof(Buffer), Pattern, Arguments );
        va_end( Arguments );
        return string( Buffer );
    }

    // -----------------------------------------------------------------------------

    static bool StartsWith( const string& Text, const char* Prefix )
    {
        return Text.compare( 0, strlen( Prefix ), Prefix ) == 0;
    }

    // -----------------------------------------------------------------------------

    static string GetDirectoryOf( const string& Path )
    {
        size_t Position = Path.find_last_of( "/\\" );

        if( Position == string::npos )
          return "";

        return Path.substr( 0, Position + 1 );
    }

    // -----------------------------------------------------------------------------

    // file names built from the cartridge name must be safe
    static string SafeFileStem( const string& FileName )
    {
        string Stem = FileName;
        size_t Dot = Stem.find_last_of( '.' );

        if( Dot != string::npos && Dot > 0 )
          Stem = Stem.substr( 0, Dot );

        for( char& Character: Stem )
          if( !isalnum( (unsigned char)Character ) && Character != '-' && Character != '_' )
            Character = '_';

        return Stem.empty()? string("cart") : Stem;
    }

    // -----------------------------------------------------------------------------

    // a guest location as "bank:address" in hex (the form V32TRACE files use everywhere)
    static string DescribeBlockKey( uint32_t Key )
    {
        return Format( "%X:%04X", Key >> 16, Key & 0xFFFF );
    }

    // -----------------------------------------------------------------------------

    static const char* DescribeErrorCode( uint32_t Code )
    {
        switch( (CPUErrorCodes)Code )
        {
            case CPUErrorCodes::InvalidMemoryRead:  return "invalid memory read";
            case CPUErrorCodes::InvalidMemoryWrite: return "invalid memory write";
            case CPUErrorCodes::InvalidPortRead:    return "invalid port read";
            case CPUErrorCodes::InvalidPortWrite:   return "invalid port write";
            case CPUErrorCodes::StackOverflow:      return "stack overflow";
            case CPUErrorCodes::StackUnderflow:     return "stack underflow";
            case CPUErrorCodes::DivisionError:      return "division error";
            default:                                return "error";
        }
    }


    // =============================================================================
    //      TRACE: INSTANCE HANDLING
    // =============================================================================


    V32Trace::V32Trace()
    {
        Armed = false;
        Recording = false;
        Console = nullptr;
        CartWords = 0;
        CartHash = 0;
        HavePrevious = false;
        PreviousAddress = ExpectedAddress = 0;
        RingCount = 0;
        RingFrozen = false;
        CartFrame0 = -1;
        StartedMidRun = false;
        Trapped = false;
        HeldFrames = 0;
        HitsDropped = 0;
        ResumeGaveUp = false;
        Restarts = 0;
        RepeatRun = 0;
        ContinueAfterTrap = true;
        HaveResume = false;
        ResumeOffset = 0;
        HaveHash = false;
        ExpectedHash = 0;
        memset( RingFrom, 0, sizeof(RingFrom) );
        memset( RingTo, 0, sizeof(RingTo) );
        LabelsFromFile = false;
        HaveMeta = false;
        WritePending = false;
    }

    // -----------------------------------------------------------------------------

    V32Trace::~V32Trace()
    {
        for( uint32_t* Page: Pages )
          free( Page );
    }

    // -----------------------------------------------------------------------------

    void V32Trace::ResetSessionData()
    {
        for( uint32_t* Page: Pages )
          free( Page );

        Pages.clear();
        Frames.clear();
        Markers.clear();
        Events.clear();
        Labels.clear();
        Blocks.clear();
        Hits.clear();
        HitsDropped = 0;
        HeldFrames = 0;
        ResumeGaveUp = false;
        Restarts = 0;
        RepeatRun = 0;
        LastTrapKey = "";
        WatchSpecs.clear();
        Watches.clear();
        WatchBits.clear();
        HaveResume = false;
        ResumeLabel = "";
        ResumeOffset = 0;
        HaveHash = false;
        ExpectedHash = 0;

        HavePrevious = false;
        RingCount = 0;
        RingFrozen = false;
        CartFrame0 = -1;
        StartedMidRun = false;
        Trapped = false;
        LabelsFromFile = false;
        LabelsPath = "";
        HaveMeta = false;
        MetaStale = false;
        MetaProblem = "";
        MetaWords = 0;
        SeriesList.clear();
        KindNames.clear();
        Arch = "unknown";
        BlockPatterns.clear();
        ContinuePrefixes.clear();
        Captures.clear();
        WritePending = false;
    }


    // =============================================================================
    //      TRACE: SESSION CONTROL
    // =============================================================================


    void V32Trace::Attach( V32Console* Owner )
    {
        Console = Owner;
    }
    
    // -----------------------------------------------------------------------------
    
    void V32Trace::Arm( bool On )
    {
        Armed = On;

        if( !Console )
          return;

        // while a console is running, take effect right away
        if( On && !Recording && Console->IsPowerOn() )
          BeginSession( *Console );

        if( !On && Recording )
          EndSession( "recording stopped" );
    }

    // -----------------------------------------------------------------------------

    void V32Trace::BeginSession( V32Console& Owner )
    {
        Console = &Owner;

        if( Recording )
          EndSession( "new session" );

        // a trace only makes sense while a cartridge is running
        if( !Owner.HasCartridge() || !Owner.IsPowerOn() )
          return;

        ResetSessionData();

        // facts about the cartridge
        CartWords = (uint32_t)max( 0, Owner.CartridgeController.MemorySize );
        CartName = Owner.GetCartridgeFileName();
        CartTitle = Owner.GetCartridgeTitle();
        BiosName = Owner.BiosFileName;

        uint32_t Hash = 2166136261u;

        for( uint32_t i = 0; i < CartWords && i < Owner.CartridgeController.Memory.size(); i++ )
        {
            Hash ^= Owner.CartridgeController.Memory[ i ].AsBinary;
            Hash *= 16777619u;
        }

        CartHash = Hash;

        // name of the report: cartridge and local time
        time_t Now;
        time( &Now );
        struct tm* Local = localtime( &Now );
        char Stamp[ 32 ];
        strftime( Stamp, sizeof(Stamp), "%Y-%m-%d %H:%M:%S", Local );
        StartedAt = Stamp;
        strftime( Stamp, sizeof(Stamp), "%Y%m%d-%H%M%S", Local );
        BaseName = SafeFileStem( CartName ) + "_" + Stamp;
        
        // several sessions can begin within a second (reset, power cycle)
        static int SessionNumber = 0;
        SessionNumber++;
        
        if( SessionNumber > 1 )
          BaseName += Format( "_%d", SessionNumber );

        Pages.assign( (CartWords >> 10) + 1, nullptr );
        LoadLabels();

        // started in the middle of a run: cartridge frame 0 is now
        uint32_t IPOffset = Owner.CPU.InstructionPointer.AsBinary - (uint32_t)Constants::CartridgeProgramROMFirstAddress;

        if( IPOffset < CartWords )
        {
            StartedMidRun = true;
            CartFrame0 = 0;
        }

        Recording = true;
        Owner.CPU.Tracer = this;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::EndSession( const char* Reason )
    {
        if( !Recording )
          return;

        WriteReport( Reason );

        Recording = false;

        if( Console )
          Console->CPU.Tracer = nullptr;

        for( uint32_t*& Page: Pages )
        {
            free( Page );
            Page = nullptr;
        }
    }

    // -----------------------------------------------------------------------------

    void V32Trace::AddMarker( const string& Note )
    {
        if( !Recording || Markers.size() >= MaximumMarkers )
          return;

        Marker NewMarker;
        NewMarker.CartFrame = CurrentCartFrame();
        NewMarker.Note = Note;

        // where the game is right now, in the translator's own terms (bank, screen, ...)
        if( HaveMeta && Console )
          for( const Capture& C: Captures )
            if( C.Reg < 0 && C.IndexReg < 0 )
              NewMarker.Note += Format( "%s[%s=%s]", NewMarker.Note.empty()? "" : " ", C.Name.c_str(), CaptureNow( C ).c_str() );

        Aggregate( NewMarker.LabelSnapshot );
        Markers.push_back( NewMarker );

        AddEvent( Format( "marker %d%s%s", (int)Markers.size(), Note.empty()? "" : ": ", Note.c_str() ) );
        WritePending = true;
    }


    // =============================================================================
    //      TRACE: HOOKS
    // =============================================================================


    void V32Trace::NewPage( uint32_t*& Page )
    {
        Page = (uint32_t*)calloc( 1024, sizeof(uint32_t) );

        if( !Page )
          throw bad_alloc();
    }

    // -----------------------------------------------------------------------------

    void V32Trace::CartStarted()
    {
        CartFrame0 = Frames.empty()? 0 : (int32_t)Frames.size() - 1;
    }

    // -----------------------------------------------------------------------------

    int32_t V32Trace::CurrentCartFrame() const
    {
        if( CartFrame0 < 0 )
          return -1;

        return (int32_t)Frames.size() - 1 - CartFrame0;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::AddEvent( const string& Text )
    {
        if( Events.size() >= MaximumEvents )
          return;

        Event NewEvent;
        NewEvent.CartFrame = CurrentCartFrame();
        NewEvent.Text = Text;
        Events.push_back( NewEvent );
    }

    // -----------------------------------------------------------------------------

    void V32Trace::PushTransfer( uint32_t From, uint32_t To )
    {
        RingFrom[ RingCount % RingSize ] = From;
        RingTo[ RingCount % RingSize ] = To;
        RingCount++;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::BeginFrame()
    {
        if( !Recording || !Console || Frames.size() >= MaximumFrames )
          return;

        // what the program will see of the first gamepad this frame
        const GamepadState& Pad = Console->GamepadController.ProvidedGamepadStates[ 0 ];
        const int32_t* Values = &Pad.Left;

        FrameRecord Record;
        Record.Cycles = 0;
        Record.Pad = 0;
        Record.Flags = 0;
        Record.GPULoad = 0;
        Record.HostUs = 0;
        Record.Quads = 0;
        Record.Flushes = 0;

        for( int i = 0; i < ButtonCount; i++ )
          if( Values[ i ] > 0 )
            Record.Pad |= (1 << i);

        Frames.push_back( Record );
    }

    // -----------------------------------------------------------------------------

    void V32Trace::EndFrame()
    {
        if( !Recording || !Console || Frames.empty() )
          return;

        FrameRecord& Record = Frames.back();
        Record.Cycles = (uint32_t)max( 0, Console->Timer.CycleCounter );

        if( Trapped )
        {
            // the cartridge is spinning in its trap routine: not a measurement of the game
            Record.Flags |= FrameHeld;
            HeldFrames++;
        }
        else if( !Console->CPU.Waiting && !Console->CPU.Halted )
          Record.Flags |= FrameOverran;

        for( Series& S: SeriesList )
        {
            S.Values.resize( Frames.size(), 0 );
            S.Values.back() = (int32_t)((S.Address < Console->RAM.Memory.size())? Console->RAM.Memory[ S.Address ].AsBinary : 0);
        }

        float GPULoad = Console->LastGPULoads[ 0 ];
        Record.GPULoad = (uint8_t)max( 0.0f, min( 255.0f, GPULoad ) );
        Record.Quads = (uint16_t)min< uint32_t >( 65535, Console->GPU.StatQuads );
        Record.Flushes = (uint16_t)min< uint32_t >( 65535, Console->GPU.StatFlushes + (Console->GPU.StatPending > 0? 1 : 0) );

        TryResume();

        if( WritePending )
          WriteReport( "in progress" );
    }

    // -----------------------------------------------------------------------------

    void V32Trace::OnCPUError( uint32_t Code, uint32_t Address )
    {
        if( !Recording || Frames.empty() )
          return;

        Frames.back().Flags |= FrameCPUError;

        AddEvent
        (
            Format( "CPU hardware error %u (%s) at %s", Code, DescribeErrorCode( Code ), DescribeAddress( Address ).c_str() ) +
            "; the BIOS error screen follows"
        );

        WritePending = true;
    }

    // -----------------------------------------------------------------------------

    // a watched label ran: a trap routine (the cartridge stops) or a soft event (it goes on)
    void V32Trace::OnWatch( uint32_t Offset )
    {
        if( Trapped )
          return;

        for( const Watch& W: Watches )
        {
            if( W.Offset != Offset )
              continue;

            CaptureHit( W.IsTrap, W.Name );
            return;
        }
    }

    // -----------------------------------------------------------------------------

    void V32Trace::CaptureHit( bool IsTrap, const string& Name )
    {
        if( !Console )
          return;

        Hit H;
        H.IsEvent = !IsTrap;
        H.Name = Name;
        H.CartFrame = CurrentCartFrame();
        H.Count = 1;

        LiveRegisters( H.Regs );

        // the translator said which words describe its state (V32TRACE `capture` and `register` lines)
        string Key = Name;

        for( const Capture& C: Captures )
        {
            vector< uint32_t > Values;
            ReadCapture( C, H.Regs, Values );

            if( !C.Volatile )
              for( uint32_t V: Values )
                Key += Format( ",%X", (unsigned)V );

            H.Values.push_back( Values );
        }

        H.Key = Key;

        for( const auto& K: KindNames )
          for( size_t i = 0; i < Captures.size(); i++ )
            if( Captures[ i ].Name == K.first && H.Values[ i ].size() == 1 )
              for( const auto& N: K.second )
                if( N.first == H.Values[ i ][ 0 ] && H.Kind.empty() )
                  H.Kind = N.second;

        // the jump trail, oldest first
        uint32_t Count = min( RingCount, RingSize );

        for( uint32_t i = 0; i < Count; i++ )
        {
            uint32_t Slot = (RingCount - Count + i) % RingSize;
            H.From.push_back( RingFrom[ Slot ] );
            H.To.push_back( RingTo[ Slot ] );
        }

        // the same hit again: count it
        bool Known = false;

        for( Hit& Old: Hits )
        {
            if( Old.IsEvent == H.IsEvent && Old.Key == H.Key )
            {
                Old.Count++;
                Known = true;
                break;
            }
        }

        if( !Known )
        {
            if( Hits.size() >= MaximumHits )
              HitsDropped++;
            else
            {
                Hits.push_back( H );
                AddEvent( Format( "%s #%d: %s%s", IsTrap? "TRAP" : "event", (int)Hits.size(), Name.c_str(), H.Kind.empty()? "" : (" (" + H.Kind + ")").c_str() ) );
            }
        }

        WritePending = true;

        if( !IsTrap )
          return;

        // a trap stops the cartridge: keep the trail as it is while it spins in the trap routine
        Trapped = true;
        HeldFrames = 0;
        RingFrozen = true;
        RepeatRun = (H.Key == LastTrapKey)? RepeatRun + 1 : 0;
        LastTrapKey = H.Key;
    }

    // -----------------------------------------------------------------------------

    // after a trap and a moment to look at it: run the cartridge again from its start, so that the
    // session goes on and can meet the next gap (the guest state is lost, the recorded data is not)
    void V32Trace::TryResume()
    {
        if( !Trapped || !ContinueAfterTrap || ResumeGaveUp || !HaveMeta || !Console || HeldFrames < (int32_t)HoldFrames )
          return;

        if( RepeatRun >= 2 )
        {
            ResumeGaveUp = true;
            AddEvent( "the same trap came back three times in a row: not restarting again (play on from a new reset to see more)" );
            WritePending = true;
            return;
        }

        V32CPU& CPU = Console->CPU;

        memset( &CPU.Registers[ 0 ], 0, 16 * sizeof(V32Word) );
        CPU.StackPointer.AsBinary = Constants::RAMFirstAddress + Constants::RAMSize - 1;
        CPU.BasePointer = CPU.StackPointer;
        CPU.InstructionPointer.AsBinary = (uint32_t)Constants::CartridgeProgramROMFirstAddress + ResumeOffset;
        CPU.Halted = false;
        CPU.Waiting = false;

        Trapped = false;
        RingFrozen = false;
        HavePrevious = false;
        Restarts++;
        AddEvent( Format( "cartridge restarted after the trap (restart %u)", (unsigned)Restarts ) );
        WritePending = true;
    }


    void V32Trace::HostFrame( uint32_t Microseconds, bool CatchUp )
    {
        if( !Recording || Frames.empty() )
          return;

        Frames.back().HostUs = Microseconds;

        if( CatchUp )
          Frames.back().Flags |= FrameCatchUp;
    }


    // =============================================================================
    //      TRACE: TRANSLATOR LABELS
    // =============================================================================


    void V32Trace::LoadLabels()
    {
        Labels.clear();
        Blocks.clear();
        LabelsFromFile = false;

        // the translator writes labels next to the cartridge it builds: <cart>.labels.txt or labels.txt
        string Folder = GetDirectoryOf( CartridgePath );
        string Stem = CartridgePath.substr( Folder.size() );
        size_t Dot = Stem.find_last_of( '.' );

        if( Dot != string::npos && Dot > 0 )
          Stem = Stem.substr( 0, Dot );

        LoadMeta( Folder, Stem );

        if( MetaStale )
        {
            BuildSyntheticLabels();
            return;
        }

        string Path = Folder + Stem + ".labels.txt";
        ifstream File;
        OpenInputFile( File, Path );

        if( !File.is_open() )
        {
            Path = Folder + "labels.txt";
            OpenInputFile( File, Path );
        }

        if( File.is_open() )
        {
            string Line;

            while( getline( File, Line ) )
            {
                unsigned long Address = 0;
                char Name[ 256 ];

                if( sscanf( Line.c_str(), "%lx %255s", &Address, Name ) != 2 )
                  continue;

                if( (Address >> 28) != 2 )
                  continue;

                uint32_t Offset = (uint32_t)(Address & 0x0FFFFFFF);

                if( Offset >= CartWords )
                  continue;

                Label NewLabel;
                NewLabel.Offset = Offset;
                NewLabel.Name = Name;
                NewLabel.Block = -1;
                Labels.push_back( NewLabel );
            }
        }

        if( Labels.empty() )
        {
            BuildSyntheticLabels();
            return;
        }

        LabelsFromFile = true;
        LabelsPath = Path;

        stable_sort
        (
            Labels.begin(), Labels.end(),
            []( const Label& A, const Label& B ) { return A.Offset < B.Offset; }
        );

        AssignBlocks();

        // the labels the translator asked us to watch (trap routines, soft events, the restart point)
        for( const WatchSpec& S: WatchSpecs )
        {
            for( const Label& L: Labels )
            {
                if( L.Name == S.Label )
                {
                    Watch W;
                    W.Offset = L.Offset;
                    W.IsTrap = S.IsTrap;
                    W.Name = S.Name;
                    Watches.push_back( W );
                    break;
                }
            }
        }

        if( HaveResume && !ResumeLabel.empty() )
          for( const Label& L: Labels )
            if( L.Name == ResumeLabel ) { ResumeOffset = L.Offset; break; }

        if( !Watches.empty() )
        {
            WatchBits.assign( (CartWords >> 5) + 1, 0 );

            for( const Watch& W: Watches )
              WatchBits[ W.Offset >> 5 ] |= (1u << (W.Offset & 31));
        }
    }

    // -----------------------------------------------------------------------------

    void V32Trace::LoadMeta( const string& Folder, const string& Stem )
    {
        ifstream File;
        OpenInputFile( File, Folder + Stem + ".v32trace.meta" );

        if( !File.is_open() )
          OpenInputFile( File, Folder + "v32trace.meta" );

        if( !File.is_open() )
          return;

        string Line;

        while( getline( File, Line ) )
        {
            char Word[ 64 ], Text[ 192 ], Name[ 64 ];
            unsigned Address = 0, Count = 0;

            if( Line.empty() || Line[ 0 ] == '#' )
              continue;

            if( sscanf( Line.c_str(), "%63s %191s", Word, Text ) < 2 )
            {
                if( string( Word ) == "resume" ) { HaveResume = true; ResumeLabel = ""; }
                continue;
            }

            string Key = Word;

            if( Key == "format" )        { if( atoi( Text ) != 1 && atoi( Text ) != 2 ) { MetaProblem = string("unsupported v32trace.meta format ") + Text; MetaStale = true; return; } }
            else if( Key == "words" )    MetaWords = (uint32_t)strtoul( Text, 0, 0 );
            else if( Key == "series" && sscanf( Line.c_str(), "%*s %63s %x", Name, &Address ) == 2 && SeriesList.size() < 8 )
            {
                Series S; S.Name = Name; S.Address = Address; SeriesList.push_back( S );
            }
            else if( Key == "kinds" )
            {
                // kinds CAPTURE 1=name 2=name ...
                istringstream Words( Line ); string W, CapName; Words >> W >> CapName;
                vector< pair< uint32_t, string > > Names;

                while( Words >> W )
                {
                    size_t Eq = W.find( '=' );
                    if( Eq != string::npos ) Names.push_back( make_pair( (uint32_t)strtoul( W.c_str(), 0, 16 ), W.substr( Eq + 1 ) ) );
                }

                KindNames.push_back( make_pair( CapName, Names ) );
            }
            else if( Key == "arch" )          { Arch = Text; HaveMeta = true; }
            else if( Key == "trap" )     { WatchSpec S; S.IsTrap = true; S.Name = "trap"; S.Label = Text; WatchSpecs.push_back( S ); }
            else if( Key == "event" && sscanf( Line.c_str(), "%*s %63s %191s", Name, Text ) == 2 )
            {
                WatchSpec S; S.IsTrap = false; S.Name = Name; S.Label = Text; WatchSpecs.push_back( S );
            }
            else if( Key == "resume" )   { HaveResume = true; ResumeLabel = Text; }
            else if( Key == "fnv1a" )    { HaveHash = true; ExpectedHash = (uint32_t)strtoul( Text, 0, 16 ); }
            else if( Key == "block" )
            {
                // block PATTERN [packed] [bank=HEX] [bankadd=HEX]; more than one block line may be given
                BlockPattern P;
                P.Format = Text; P.Packed = false; P.FixedBank = -1; P.BankAdd = 0;
                istringstream Words( Line ); string W; Words >> W >> W;

                while( Words >> W )
                {
                    if( W == "packed" ) P.Packed = true;
                    else if( W.compare( 0, 5, "bank=" ) == 0 ) P.FixedBank = (int32_t)strtoul( W.c_str() + 5, 0, 16 );
                    else if( W.compare( 0, 8, "bankadd=" ) == 0 ) P.BankAdd = (uint32_t)strtoul( W.c_str() + 8, 0, 16 );
                }

                BlockPatterns.push_back( P );
            }
            else if( Key == "continue" ) ContinuePrefixes.push_back( Text );
            else if( Key == "capture" && sscanf( Line.c_str(), "%*s %63s %x %u", Name, &Address, &Count ) == 3 && Count > 0 && Count <= 64 )
            {
                Capture NewCapture;
                NewCapture.Name = Name;
                NewCapture.Address = Address;
                NewCapture.Count = Count;
                NewCapture.Reg = -1;
                NewCapture.IndexReg = -1;
                NewCapture.Mask = 0xFFFFFFFF;
                NewCapture.Volatile = ( Line.find( " volatile" ) != string::npos );

                // optional: index=R<n>[&MASK]  (RAM address = ADDR + (R<n> & MASK), for words addressed through a guest register)
                size_t Ix = Line.find( "index=R" );
                unsigned Reg = 0, Mask = 0xFFFFFFFF;

                if( Ix != string::npos && sscanf( Line.c_str() + Ix, "index=R%u&%x", &Reg, &Mask ) >= 1 && Reg < 16 )
                {
                    NewCapture.IndexReg = (int)Reg;
                    NewCapture.Mask = Mask;
                }

                Captures.push_back( NewCapture );
            }
            else if( Key == "register" )
            {
                // register NAME R<n> [MASK]: a CPU register (the guest PC or SP of a translator that keeps them there)
                unsigned Reg = 0, Mask = 0xFFFFFFFF;

                if( sscanf( Line.c_str(), "%*s %63s R%u %x", Name, &Reg, &Mask ) >= 2 && Reg < 16 )
                {
                    Capture NewCapture;
                    NewCapture.Name = Name;
                    NewCapture.Address = 0;
                    NewCapture.Count = 1;
                    NewCapture.Reg = (int)Reg;
                    NewCapture.IndexReg = -1;
                    NewCapture.Mask = Mask;
                    NewCapture.Volatile = ( Line.find( " volatile" ) != string::npos );
                    Captures.push_back( NewCapture );
                }
            }
        }

        // a meta file (and the labels beside it) from another build of the cartridge would mislead
        if( !MetaStale && MetaWords && MetaWords != CartWords )
        {
            MetaProblem = Format( "v32trace.meta says the cartridge has %u program words, this one has %u", (unsigned)MetaWords, (unsigned)CartWords );
            MetaStale = true;
        }

        if( !MetaStale && HaveHash && ExpectedHash != CartHash )
        {
            MetaProblem = Format( "v32trace.meta says the program ROM hash is %08X, this cartridge's is %08X", (unsigned)ExpectedHash, (unsigned)CartHash );
            MetaStale = true;
        }

        if( MetaStale )
        {
            WatchSpecs.clear();
            HaveResume = false;
            HaveMeta = false;
            SeriesList.clear();
            KindNames.clear();
            Captures.clear();
            BlockPatterns.clear();
            ContinuePrefixes.clear();
        }
    }

    // -----------------------------------------------------------------------------

    string V32Trace::CaptureNow( const Capture& C ) const
    {
        uint32_t Regs[ 16 ];
        vector< uint32_t > Values;
        LiveRegisters( Regs );
        ReadCapture( C, Regs, Values );
        string Out;

        for( size_t i = 0; i < Values.size(); i++ )
          Out += Format( "%s%X", i? "," : "", (unsigned)Values[ i ] );

        return Out;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::LiveRegisters( uint32_t* Regs ) const
    {
        V32CPU& CPU = Console->CPU;

        for( int i = 0; i < 11; i++ )
          Regs[ i ] = CPU.Registers[ i ].AsBinary;

        Regs[ 11 ] = CPU.CountRegister.AsBinary;
        Regs[ 12 ] = CPU.SourceRegister.AsBinary;
        Regs[ 13 ] = CPU.DestinationRegister.AsBinary;
        Regs[ 14 ] = CPU.BasePointer.AsBinary;
        Regs[ 15 ] = CPU.StackPointer.AsBinary;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::ReadCapture( const Capture& C, const uint32_t* Regs, vector< uint32_t >& Out ) const
    {
        const vector< V32Word >& RAM = Console->RAM.Memory;
        Out.clear();

        if( C.Reg >= 0 )
        {
            Out.push_back( Regs[ C.Reg ] & C.Mask );
            return;
        }

        uint32_t Base = C.Address + ((C.IndexReg >= 0)? (Regs[ C.IndexReg ] & C.Mask) : 0);

        for( uint32_t i = 0; i < C.Count; i++ )
          Out.push_back( (Base + i < RAM.size())? RAM[ Base + i ].AsBinary : 0 );
    }

    // -----------------------------------------------------------------------------

    // value of a captured word, with the translator's name for it when `kinds` gave one
    string V32Trace::CaptureText( const Capture& C ) const
    {
        string Out;

        for( size_t i = 0; i < C.Values.size(); i++ )
          Out += Format( "%s%X", i? "," : "", (unsigned)C.Values[ i ] );

        for( const auto& K: KindNames )
          if( K.first == C.Name && C.Values.size() == 1 )
            for( const auto& N: K.second )
              if( N.first == C.Values[ 0 ] )
                Out += " (" + N.second + ")";

        return Out;
    }

    // -----------------------------------------------------------------------------

    // the last guest blocks entered before a hit, oldest first
    string V32Trace::DescribeTrail( const Hit& H, int Limit, const char* Separator ) const
    {
        string Trail; string Last; int Shown = 0;

        for( size_t i = H.To.size(); i > 0 && Shown < Limit; i-- )
        {
            string Block = DescribeBlock( H.To[ i - 1 ] );
            if( Block.empty() || Block == Last ) continue;
            Trail = Block + (Trail.empty()? "" : Separator) + Trail;
            Last = Block; Shown++;
        }

        return Trail;
    }

    // -----------------------------------------------------------------------------

    // machine-readable lines, one per distinct trap or event: key=value pairs a script can split without knowing the report layout
    string V32Trace::BuildTrapLines() const
    {
        string Out;

        for( size_t n = 0; n < Hits.size(); n++ )
        {
            const Hit& H = Hits[ n ];
            Out += Format( "%s n=%d cartframe=%d count=%u arch=%s", H.IsEvent? "EVENT" : "TRAP", (int)n + 1, H.CartFrame, (unsigned)H.Count, Arch.c_str() );

            if( H.IsEvent )
              Out += " name=" + H.Name;

            if( !H.Kind.empty() )
              Out += " kind=" + H.Kind;

            for( size_t k = 0; k < Captures.size(); k++ )
            {
                Out += " " + Captures[ k ].Name + "=";

                for( size_t i = 0; i < H.Values[ k ].size(); i++ )
                  Out += Format( "%s%X", i? "," : "", (unsigned)H.Values[ k ][ i ] );
            }

            string Trail = DescribeTrail( H, 32, "," );
            Out += " trail=" + (Trail.empty()? string("-") : Trail) + "\n";
        }

        return Out;
    }

    // -----------------------------------------------------------------------------

    // block labels of a translator that wrote a meta file: two hex fields, bank and address
    bool V32Trace::ParseMetaBlock( const string& Name, uint32_t& Key ) const
    {
        for( const BlockPattern& P: BlockPatterns )
        {
            string Format = P.Format + "%n";
            unsigned A = 0, B = 0;
            int Used = 0;
            int Got = sscanf( Name.c_str(), Format.c_str(), &A, &B, &Used );

            // %n lands in the first free argument, so look again for the one-number forms
            if( P.Packed || P.FixedBank >= 0 )
            {
                Used = 0;
                Got = sscanf( Name.c_str(), Format.c_str(), &A, &Used );
                if( Got != 1 || Used != (int)Name.size() ) continue;

                if( P.Packed ) Key = A;
                else           Key = (((uint32_t)P.FixedBank + P.BankAdd) << 16) | (A & 0xFFFF);
                return true;
            }

            if( Got != 2 || Used != (int)Name.size() || B > 0xFFFFFF )
              continue;

            Key = ((A + P.BankAdd) << 16) | B;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------------

    // without a labels file we can still tell where the time goes: use
    // regular buckets of the cartridge program as if they were routines
    void V32Trace::BuildSyntheticLabels()
    {
        const uint32_t Bucket = 256;

        for( uint32_t Offset = 0; Offset < CartWords; Offset += Bucket )
        {
            Label NewLabel;
            NewLabel.Offset = Offset;
            NewLabel.Name = Format( "@%08X", (unsigned)(Offset + Constants::CartridgeProgramROMFirstAddress) );
            NewLabel.Block = -1;
            Labels.push_back( NewLabel );
        }
    }

    // -----------------------------------------------------------------------------

    // decide which labels belong to a translated guest block, so
    // that time can be reported per guest routine and not per label
    void V32Trace::AssignBlocks()
    {
        map< uint32_t, int32_t > BlockByKey;
        int32_t Current = -1;

        auto IsContinuation = [&]( const string& N )
        {
            // "PREFIX" continues a block when a digit follows; "PREFIX*" when anything does
            for( const string& P: ContinuePrefixes )
            {
                if( !P.empty() && P.back() == '*' )
                {
                    if( StartsWith( N, P.substr( 0, P.size() - 1 ).c_str() ) )
                      return true;
                }
                else if( StartsWith( N, P.c_str() ) && N.size() > P.size() && isdigit( (unsigned char)N[ P.size() ] ) )
                    return true;
            }

            return false;
        };

        for( Label& L: Labels )
        {
            uint32_t Key = 0;

            if( ParseMetaBlock( L.Name, Key ) )
            {
                auto Found = BlockByKey.find( Key );

                if( Found == BlockByKey.end() )
                {
                    Block NewBlock;
                    NewBlock.Key = Key;
                    NewBlock.Display = DescribeBlockKey( Key );
                    Blocks.push_back( NewBlock );
                    Found = BlockByKey.insert( make_pair( Key, (int32_t)Blocks.size() - 1 ) ).first;
                }

                Current = Found->second;
                L.Block = Current;
            }
            else if( Current >= 0 && IsContinuation( L.Name ) )
            {
                L.Block = Current;
            }
            else
            {
                Current = -1;
            }
        }
    }

    // -----------------------------------------------------------------------------

    // index of the last label at or before an offset (-1 if none)
    int32_t V32Trace::FindLabel( uint32_t Offset ) const
    {
        int32_t Low = 0, High = (int32_t)Labels.size() - 1, Result = -1;

        while( Low <= High )
        {
            int32_t Middle = (Low + High) / 2;

            if( Labels[ Middle ].Offset <= Offset )
            {
                Result = Middle;
                Low = Middle + 1;
            }
            else
              High = Middle - 1;
        }

        return Result;
    }

    // -----------------------------------------------------------------------------

    string V32Trace::DescribeAddress( uint32_t Address ) const
    {
        uint32_t Offset = Address - (uint32_t)Constants::CartridgeProgramROMFirstAddress;

        if( Offset < CartWords )
        {
            int32_t Index = FindLabel( Offset );

            if( Index < 0 )
              return Format( "0x%08X", Address );

            uint32_t Delta = Offset - Labels[ Index ].Offset;

            if( Delta == 0 )
              return Labels[ Index ].Name;

            return Format( "%s+%u", Labels[ Index ].Name.c_str(), Delta );
        }

        if( Address >= (uint32_t)Constants::BiosProgramROMFirstAddress && Address < (uint32_t)Constants::CartridgeProgramROMFirstAddress )
          return Format( "bios+%X", Address - (uint32_t)Constants::BiosProgramROMFirstAddress );

        return Format( "0x%08X", Address );
    }

    // -----------------------------------------------------------------------------

    // the guest block (bank:address) of a cartridge address, or "" if none
    string V32Trace::DescribeBlock( uint32_t Address ) const
    {
        uint32_t Offset = Address - (uint32_t)Constants::CartridgeProgramROMFirstAddress;

        if( Offset >= CartWords )
          return "";

        int32_t Index = FindLabel( Offset );

        if( Index < 0 || Labels[ Index ].Block < 0 )
          return "";

        return Blocks[ Labels[ Index ].Block ].Display;
    }


    // =============================================================================
    //      TRACE: REPORT GENERATION
    // =============================================================================


    // instruction counts per label (the last slot counts what precedes the first label)
    void V32Trace::Aggregate( vector< uint64_t >& ByLabel ) const
    {
        ByLabel.assign( Labels.size() + 1, 0 );

        for( size_t PageIndex = 0; PageIndex < Pages.size(); PageIndex++ )
        {
            const uint32_t* Page = Pages[ PageIndex ];

            if( !Page )
              continue;

            for( uint32_t i = 0; i < 1024; i++ )
            {
                if( !Page[ i ] )
                  continue;

                int32_t Index = FindLabel( (uint32_t)(PageIndex << 10) + i );
                ByLabel[ (Index < 0)? Labels.size() : (size_t)Index ] += Page[ i ];
            }
        }
    }

    // -----------------------------------------------------------------------------

    void V32Trace::AppendProfile
    (
        string& Out, const vector< uint64_t >& ByLabel,
        int32_t FirstFrame, int32_t LastFrame, int TopCount
    ) const
    {
        uint64_t Total = 0, InBlocks = 0;
        vector< uint64_t > ByBlock( Blocks.size(), 0 );

        for( size_t i = 0; i < ByLabel.size(); i++ )
        {
            Total += ByLabel[ i ];

            if( i < Labels.size() && Labels[ i ].Block >= 0 )
            {
                ByBlock[ Labels[ i ].Block ] += ByLabel[ i ];
                InBlocks += ByLabel[ i ];
            }
        }

        Out += Format( "PC profile frames %d-%d, %llu instructions\n", FirstFrame, LastFrame, (unsigned long long)Total );

        if( Total == 0 )
          return;

        // routines and labels, as v32sim's V32_PCPROF prints them
        vector< size_t > Order;

        for( size_t i = 0; i < ByLabel.size(); i++ )
          if( ByLabel[ i ] )
            Order.push_back( i );

        sort( Order.begin(), Order.end(), [&]( size_t A, size_t B ) { return ByLabel[ A ] > ByLabel[ B ]; } );

        for( int i = 0; i < TopCount && i < (int)Order.size(); i++ )
        {
            size_t Index = Order[ i ];
            string Name = (Index < Labels.size())? Labels[ Index ].Name : string("(before first label)");
            Out += Format( "  %8llu %5.1f%%  %s\n", (unsigned long long)ByLabel[ Index ], 100.0 * ByLabel[ Index ] / Total, Name.c_str() );
        }

        // translated guest blocks (bank:address in hex)
        if( Blocks.empty() )
          return;

        Out += Format( "%s blocks: %.1f%% of instructions run in translated blocks, the rest in runtime code\n", Arch.c_str(), 100.0 * InBlocks / Total );

        vector< size_t > BlockOrder;

        for( size_t i = 0; i < ByBlock.size(); i++ )
          if( ByBlock[ i ] )
            BlockOrder.push_back( i );

        sort( BlockOrder.begin(), BlockOrder.end(), [&]( size_t A, size_t B ) { return ByBlock[ A ] > ByBlock[ B ]; } );

        for( int i = 0; i < TopCount && i < (int)BlockOrder.size(); i++ )
        {
            size_t Index = BlockOrder[ i ];
            Out += Format( "  %8llu %5.1f%%  %s %s\n", (unsigned long long)ByBlock[ Index ], 100.0 * ByBlock[ Index ] / Total, "BLK", Blocks[ Index ].Display.c_str() );
        }
    }

    // -----------------------------------------------------------------------------

    // quads, flushes and host time of a range of cartridge frames
    string V32Trace::SummarizeSeries( int32_t FirstFrame, int32_t LastFrame ) const
    {
        string Out;

        if( CartFrame0 < 0 || LastFrame < FirstFrame )
          return Out;

        for( const Series& S: SeriesList )
        {
            int64_t Sum = 0; int32_t Max = INT32_MIN, Min = INT32_MAX; int32_t At = FirstFrame; int32_t N = 0;

            for( int32_t i = FirstFrame; i <= LastFrame; i++ )
            {
                size_t Index = CartFrame0 + i;
                if( Index >= S.Values.size() ) break;
                if( Frames[ Index ].Flags & FrameHeld ) continue;
                int32_t V = S.Values[ Index ];
                Sum += V; N++; Min = min( Min, V );
                if( V > Max ) { Max = V; At = i; }
            }

            if( N )
              Out += Format( "series %s: avg %.1f min %d max %d (frame %d)\n", S.Name.c_str(), (double)Sum / N, (int)Min, (int)Max, (int)At );
        }

        return Out;
    }

    // -----------------------------------------------------------------------------

    string V32Trace::SummarizeHost( int32_t FirstFrame, int32_t LastFrame ) const
    {
        if( CartFrame0 < 0 || LastFrame < FirstFrame )
          return "no frames\n";

        int32_t Count = LastFrame - FirstFrame + 1;
        uint64_t Quads = 0, Flushes = 0, Us = 0;
        uint32_t MaxFlushes = 0, MaxQuads = 0, MaxUs = 0, Slow = 0, CatchUp = 0, Timed = 0;
        vector< uint32_t > Times;

        for( int32_t i = FirstFrame; i <= LastFrame; i++ )
        {
            const FrameRecord& R = Frames[ CartFrame0 + i ];
            if( R.Flags & FrameHeld ) { Count--; continue; }
            Quads += R.Quads;
            Flushes += R.Flushes;
            MaxQuads = max< uint32_t >( MaxQuads, R.Quads );
            MaxFlushes = max< uint32_t >( MaxFlushes, R.Flushes );

            if( R.Flags & FrameCatchUp ) CatchUp++;

            if( R.HostUs )
            {
                Timed++;
                Us += R.HostUs;
                MaxUs = max( MaxUs, R.HostUs );
                if( R.HostUs > 16667 ) Slow++;
                Times.push_back( R.HostUs );
            }
        }

        if( Count <= 0 )
          return "no frames\n";

        string Out = Format( "frames %d-%d: quads/frame avg %llu max %u, flushes/frame avg %llu max %u\n", FirstFrame, LastFrame,
                             (unsigned long long)(Quads / Count), (unsigned)MaxQuads, (unsigned long long)(Flushes / Count), (unsigned)MaxFlushes );

        if( Timed )
        {
            sort( Times.begin(), Times.end() );
            Out += Format( "  host frame ms: avg %.2f p95 %.2f max %.2f; %u of %u frames over 16.7 ms; catch-up frames %u\n",
                           Us / 1000.0 / Timed, Times[ (Times.size() * 95) / 100 ] / 1000.0, MaxUs / 1000.0, (unsigned)Slow, (unsigned)Timed, (unsigned)CatchUp );
        }

        return Out;
    }

    // -----------------------------------------------------------------------------

    // the gamepad history of the cartridge frames as a V32_INPUT script
    string V32Trace::BuildInputScript( int32_t& CartFrames ) const
    {
        CartFrames = (CartFrame0 < 0)? 0 : (int32_t)Frames.size() - CartFrame0;

        // runs of each button
        struct Run { int32_t From, To; };
        map< pair< int32_t, int32_t >, vector< const char* > > Groups;

        for( int Button = 0; Button < ButtonCount; Button++ )
        {
            if( !ButtonNames[ Button ] )
              continue;

            int32_t RunStart = -1;

            for( int32_t Frame = 0; Frame <= CartFrames; Frame++ )
            {
                bool Held = (Frame < CartFrames) && (Frames[ CartFrame0 + Frame ].Pad & (1 << Button));

                if( Held && RunStart < 0 )
                  RunStart = Frame;

                if( !Held && RunStart >= 0 )
                {
                    Groups[ make_pair( RunStart, Frame - 1 ) ].push_back( ButtonNames[ Button ] );
                    RunStart = -1;
                }
            }
        }

        string Script;

        for( const auto& Group: Groups )
        {
            if( !Script.empty() )
              Script += ",";

            for( size_t i = 0; i < Group.second.size(); i++ )
            {
                if( i > 0 ) Script += "+";
                Script += Group.second[ i ];
            }

            Script += Format( "@%d-%d", Group.first.first, Group.first.second );
        }

        return Script;
    }

    // -----------------------------------------------------------------------------

    string V32Trace::BuildBlockList() const
    {
        vector< uint64_t > ByLabel;
        Aggregate( ByLabel );

        vector< uint64_t > ByBlock( Blocks.size(), 0 );

        for( size_t i = 0; i < Labels.size(); i++ )
          if( Labels[ i ].Block >= 0 )
            ByBlock[ Labels[ i ].Block ] += ByLabel[ i ];

        string Out = string("# ") + Arch + " blocks that ran (bank:address in hex, instructions executed)\n";
        Out += Format( "# cart=%s fnv1a=%08X\n", CartName.c_str(), (unsigned)CartHash );

        for( size_t i = 0; i < Blocks.size(); i++ )
          if( ByBlock[ i ] )
            Out += Format( "%s %llu\n", Blocks[ i ].Display.c_str(), (unsigned long long)ByBlock[ i ] );

        return Out;
    }

    // -----------------------------------------------------------------------------

    string V32Trace::BuildReport( const char* Reason ) const
    {
        string Out;
        const int32_t Budget = Constants::CyclesPerFrame;

        int32_t CartFrames = 0;
        string InputScript = BuildInputScript( CartFrames );
        int32_t BootFrames = (CartFrame0 < 0)? (int32_t)Frames.size() : CartFrame0;

        // header
        Out += "# V32TRACE report, spec 1\n";
        Out += "# Written by the Vircon32 DesktopEmulator (V32TRACE). Send this whole file.\n";
        Out += Format( "session: started %s local time; this copy written because: %s\n", StartedAt.c_str(), Reason );
        Out += Format( "cart: file=\"%s\" title=\"%s\" words=%u fnv1a=%08X\n", CartName.c_str(), CartTitle.c_str(), (unsigned)CartWords, (unsigned)CartHash );
        Out += Format( "arch: %s\n", Arch.c_str() );

        if( MetaStale )
          Out += "meta: IGNORED, " + MetaProblem + " (rebuild the cartridge and keep its files together)\n";
        Out += Format( "bios: %s\n", BiosName.c_str() );

        if( LabelsFromFile )
          Out += Format( "labels: %s (%u labels, %u translated blocks)\n", LabelsPath.c_str(), (unsigned)Labels.size(), (unsigned)Blocks.size() );
        else
          Out += "labels: none found (keep the labels file the translator writes in the same folder as the .v32 for readable names and guest addresses)\n";

        Out += Format( "frames: total=%u boot=%d cart=%d%s\n", (unsigned)Frames.size(), BootFrames, CartFrames,
                       StartedMidRun? " (recording started while the cartridge was already running: cart frame 0 is that moment)" : "" );
        Out += "frame numbers below are cartridge frames: 0 is the first frame in which the cartridge ran\n";

        // events: traps, errors, markers
        Out += "\n[events]\n";

        if( Events.empty() && Hits.empty() )
          Out += "none\n";

        for( const Event& E: Events )
          Out += Format( "frame %d: %s\n", E.CartFrame, E.Text.c_str() );

        for( size_t n = 0; n < Hits.size(); n++ )
        {
            const Hit& H = Hits[ n ];

            Out += Format( "%s #%d at frame %d: %s arch=%s, seen %u time%s%s%s\n", H.IsEvent? "EVENT" : "TRAP", (int)n + 1, H.CartFrame,
                           H.Name.c_str(), Arch.c_str(), (unsigned)H.Count, H.Count == 1? "" : "s",
                           H.Kind.empty()? "" : ", kind ", H.Kind.c_str() );

            for( size_t k = 0; k < Captures.size(); k++ )
            {
                Out += "  capture " + Captures[ k ].Name + "=";

                for( size_t i = 0; i < H.Values[ k ].size(); i++ )
                  Out += Format( "%s%X", i? "," : "", (unsigned)H.Values[ k ][ i ] );

                for( const auto& K: KindNames )
                  if( K.first == Captures[ k ].Name && H.Values[ k ].size() == 1 )
                    for( const auto& N: K.second )
                      if( N.first == H.Values[ k ][ 0 ] )
                        Out += " (" + N.second + ")";

                Out += "\n";
            }

            if( !H.IsEvent )
            {
                Out += "  regs:";

                for( int i = 0; i < 16; i++ )
                  Out += Format( " R%d=%X", i, (unsigned)H.Regs[ i ] );

                Out += "\n";
            }

            string Trail = DescribeTrail( H, H.IsEvent? 8 : 32, " " );

            if( !Trail.empty() )
              Out += "  " + Arch + " block trail (oldest first): " + Trail + "\n";

            if( !H.IsEvent )
            {
                Out += "  V32 transfer trail (last 12, oldest first):\n";

                size_t First = (H.From.size() > 12)? H.From.size() - 12 : 0;

                for( size_t i = First; i < H.From.size(); i++ )
                  Out += "    " + DescribeAddress( H.From[ i ] ) + " -> " + DescribeAddress( H.To[ i ] ) + "\n";
            }
        }

        if( HitsDropped )
          Out += Format( "(%u more distinct traps or events were not recorded: the limit is %u)\n", (unsigned)HitsDropped, (unsigned)MaximumHits );

        if( Restarts || (Trapped && ContinueAfterTrap) )
          Out += Format( "the cartridge was restarted %u time%s after traps; frames spent in trap routines are left out of [perf], [host] and the hot spots\n", (unsigned)Restarts, Restarts == 1? "" : "s" );

        // input history
        Out += "\n[input]\n";
        Out += Format( "# gamepad 1, cart frames 0-%d. Replay with: V32_INPUT=@this_file v32sim cart.v32 %d\n", max( 0, CartFrames - 1 ), CartFrames );
        Out += "V32_INPUT=" + InputScript + "\n";

        // performance
        Out += "\n[perf]\n";
        
        // frames in which the cartridge only spun in a trap routine say nothing about the game
        int32_t PerfFrames = CartFrames;
        int32_t Valid = 0;

        for( int32_t i = 0; i < PerfFrames; i++ )
          if( !(Frames[ CartFrame0 + i ].Flags & FrameHeld) )
            Valid++;

        if( Valid != PerfFrames )
          Out += Format( "(%d of %d frames were spent in trap routines and are left out)\n", PerfFrames - Valid, PerfFrames );

        if( Valid > 0 )
        {
            uint64_t Sum = 0, GPUSum = 0;
            uint32_t Max = 0, OverBudget = 0, Overran = 0, Errors = 0;

            for( int32_t i = 0; i < PerfFrames; i++ )
            {
                const FrameRecord& R = Frames[ CartFrame0 + i ];
                if( R.Flags & FrameHeld ) continue;
                Sum += R.Cycles;
                GPUSum += R.GPULoad;
                Max = max( Max, R.Cycles );
                if( (int32_t)R.Cycles > Budget ) OverBudget++;
                if( R.Flags & FrameOverran ) Overran++;
                if( R.Flags & FrameCPUError ) Errors++;
            }

            Out += Format( "cycles/frame from frame 0: avg %llu max %u, %u of %d frames over %d\n",
                           (unsigned long long)(Sum / Valid), (unsigned)Max, (unsigned)OverBudget, Valid, Budget );
            Out += Format( "frames where the cartridge missed its WAIT (dropped frames): %u; frames with a CPU error: %u; average GPU load %llu%%\n",
                           (unsigned)Overran, (unsigned)Errors, (unsigned long long)(GPUSum / Valid) );

            // runs of dropped frames
            string Runs;
            int RunCount = 0;

            for( int32_t i = 0; i < PerfFrames; i++ )
            {
                if( !(Frames[ CartFrame0 + i ].Flags & FrameOverran) )
                  continue;

                int32_t j = i;
                while( j + 1 < PerfFrames && (Frames[ CartFrame0 + j + 1 ].Flags & FrameOverran) ) j++;

                if( RunCount < 20 )
                  Runs += Format( " %d-%d", i, j );

                RunCount++;
                i = j;
            }

            if( RunCount > 0 )
              Out += Format( "dropped frame runs (%d):%s%s\n", RunCount, Runs.c_str(), RunCount > 20? " ..." : "" );

            // the most expensive frames
            vector< int32_t > Worst;

            for( int32_t i = 0; i < PerfFrames; i++ )
              if( !(Frames[ CartFrame0 + i ].Flags & FrameHeld) )
                Worst.push_back( i );

            size_t WorstCount = min< size_t >( 10, Worst.size() );

            partial_sort
            (
                Worst.begin(), Worst.begin() + WorstCount, Worst.end(),
                [&]( int32_t A, int32_t B )
                {
                    uint32_t CA = Frames[ CartFrame0 + A ].Cycles, CB = Frames[ CartFrame0 + B ].Cycles;
                    return (CA != CB)? (CA > CB) : (A < B);
                }
            );

            Out += "most expensive frames:";

            for( size_t i = 0; i < WorstCount; i++ )
              Out += Format( " %d(%u)", Worst[ i ], (unsigned)Frames[ CartFrame0 + Worst[ i ] ].Cycles );

            Out += "\n";

            // timeline: at most ~120 rows
            int32_t Step = 60;
            while( PerfFrames / Step > 120 ) Step += 60;

            Out += Format( "timeline (%d frames per row): frames, avg cycles, max cycles, dropped, max GPU load\n", Step );

            for( int32_t Start = 0; Start < PerfFrames; Start += Step )
            {
                int32_t End = min( PerfFrames, Start + Step );
                uint64_t RowSum = 0;
                uint32_t RowMax = 0, RowDropped = 0, RowGPU = 0, RowValid = 0;

                for( int32_t i = Start; i < End; i++ )
                {
                    const FrameRecord& R = Frames[ CartFrame0 + i ];
                    if( R.Flags & FrameHeld ) continue;
                    RowValid++;
                    RowSum += R.Cycles;
                    RowMax = max( RowMax, R.Cycles );
                    RowGPU = max< uint32_t >( RowGPU, R.GPULoad );
                    if( R.Flags & FrameOverran ) RowDropped++;
                }

                if( RowValid == 0 )
                  continue;

                Out += Format( "  %6d-%-6d %7llu %7u %4u %4u%%\n", Start, End - 1, (unsigned long long)(RowSum / RowValid),
                               (unsigned)RowMax, (unsigned)RowDropped, (unsigned)RowGPU );
            }
        }
        else
          Out += "the cartridge never ran\n";

        // what the frames cost the host (the cycle budget says nothing about that)
        Out += "\n[host]\n";
        Out += "# quads = draw commands, flushes = batches the video queue sends (a batch ends when the queue holds 20 quads or the texture, multiply colour or blending changes);\n";
        Out += "# frame ms is wall time in the emulator (absent for headless runs). A frame that takes more than 16.7 ms makes the main loop owe frames: catch-up frames.\n";
        Out += SummarizeHost( 0, PerfFrames - 1 );
        Out += SummarizeSeries( 0, PerfFrames - 1 );

        // where the instructions went
        Out += "\n[hot spots, whole session]\n";
        vector< uint64_t > Total;
        Aggregate( Total );
        AppendProfile( Out, Total, 0, max( 0, CartFrames - 1 ), 25 );

        // and between the markers the player set
        if( !Markers.empty() )
        {
            Out += "\n[markers]\n";

            vector< uint64_t > Previous( Total.size(), 0 );
            int32_t PreviousFrame = 0;

            for( size_t m = 0; m <= Markers.size(); m++ )
            {
                const vector< uint64_t >& Now = (m < Markers.size())? Markers[ m ].LabelSnapshot : Total;
                int32_t NowFrame = (m < Markers.size())? Markers[ m ].CartFrame : max( 0, CartFrames - 1 );

                vector< uint64_t > Segment( Total.size(), 0 );

                for( size_t i = 0; i < Segment.size() && i < Now.size() && i < Previous.size(); i++ )
                  Segment[ i ] = (Now[ i ] >= Previous[ i ])? Now[ i ] - Previous[ i ] : 0;

                if( m < Markers.size() )
                  Out += Format( "marker %u at frame %d%s%s\n", (unsigned)(m + 1), Markers[ m ].CartFrame,
                                 Markers[ m ].Note.empty()? "" : ": ", Markers[ m ].Note.c_str() );

                Out += Format( "segment %u: ", (unsigned)(m + 1) );
                AppendProfile( Out, Segment, PreviousFrame, NowFrame, 12 );
                Out += "  host " + SummarizeHost( PreviousFrame, NowFrame );

                { string SeriesText = SummarizeSeries( PreviousFrame, NowFrame );
                  size_t Pos = 0;
                  while( Pos < SeriesText.size() ) { size_t E = SeriesText.find( '\n', Pos ); Out += "  " + SeriesText.substr( Pos, E - Pos ) + "\n"; Pos = E + 1; } }

                if( m < Markers.size() )
                {
                    Previous = Now;
                    PreviousFrame = NowFrame;
                }
            }
        }

        return Out;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::WriteReport( const char* Reason )
    {
        WritePending = false;

        string Path = OutputFolder + BaseName + ".trace.txt";
        string BlocksPath = OutputFolder + BaseName + ".blocks.txt";

        try
        {
            string Report = BuildReport( Reason );
            ofstream File;
            OpenOutputFile( File, Path, ios::out | ios::binary );

            if( !File.is_open() )
            {
                Callbacks::LogLine( "Trace: cannot write " + Path );
                return;
            }

            File << Report;
            File.close();
            LastReportPath = Path;

            if( LabelsFromFile )
            {
                ofstream BlocksFile;
                OpenOutputFile( BlocksFile, BlocksPath, ios::out | ios::binary );

                if( BlocksFile.is_open() )
                  BlocksFile << BuildBlockList();
            }

            if( !Hits.empty() )
            {
                ofstream TrapFile;
                OpenOutputFile( TrapFile, OutputFolder + BaseName + ".traps.txt", ios::out | ios::binary );

                if( TrapFile.is_open() )
                  TrapFile << "# V32TRACE-traps 2 cart=" << CartName << " fnv1a=" << Format( "%08X", (unsigned)CartHash ) << " restarts=" << Restarts << "\n" << BuildTrapLines();
            }

            Callbacks::LogLine( "Trace written to " + Path );
        }

        catch( const exception& e )
        {
            Callbacks::LogLine( string("Trace: failed to write the report: ") + e.what() );
        }
    }
}
