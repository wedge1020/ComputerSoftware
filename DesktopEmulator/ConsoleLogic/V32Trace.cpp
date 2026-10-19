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


    // layout of the gbtov32 runtime (see inc/gbtov32.h): the translator
    // keeps its variables at RAM word 0x400 and the Game Boy memory at 0x100000
    static const uint32_t GBRuntimeVariables = 0x00000400;
    static const uint32_t GBRuntimeBank      = GBRuntimeVariables + 8;
    static const uint32_t GBMemoryBase       = 0x00100000;

    // size of the cartridge program space and limits of the recording
    static const uint32_t MaximumFrames = 4 * 1024 * 1024;
    static const size_t MaximumMarkers = 64;
    static const size_t MaximumEvents = 200;

    // frame flags
    static const uint8_t FrameOverran = 1;      // the cart did not reach WAIT in time
    static const uint8_t FrameCPUError = 2;     // the CPU raised a hardware error

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

    // the translator names its blocks after the Game Boy address and ROM bank:
    //   _blk_XXXX  (bank 0)    _blk_bBB_XXXX  (window bank)    _blk_vVV_XXXX (RAM code)
    //   _blkb_KKKKK (the body of a block, KKKKK = bank<<16 | address)
    static bool ParseBlockKey( const string& Name, uint32_t& Key )
    {
        const uint32_t VariantBase = 0x4000;

        if( StartsWith( Name, "_blkb_" ) )
        {
            char* End = nullptr;
            Key = (uint32_t)strtoul( Name.c_str() + 6, &End, 16 );
            return (End != nullptr && *End == 0 && End != Name.c_str() + 6);
        }

        if( StartsWith( Name, "_blk_v" ) || StartsWith( Name, "_blk_b" ) )
        {
            unsigned Bank = 0, Address = 0;

            if( sscanf( Name.c_str() + 6, "%2x_%4x", &Bank, &Address ) != 2 )
              return false;

            if( Name[ 5 ] == 'v' ) Bank += VariantBase;
            Key = (Bank << 16) | Address;
            return true;
        }

        if( StartsWith( Name, "_blk_" ) )
        {
            unsigned Address = 0;

            if( sscanf( Name.c_str() + 5, "%4x", &Address ) != 1 )
              return false;

            Key = Address;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------------

    // as "bank:address" in hex, which is the syntax of gbtov32's --root
    static string DescribeBlockKey( uint32_t Key )
    {
        uint32_t Bank = Key >> 16;
        uint32_t Address = Key & 0xFFFF;

        if( Bank >= 0x4000 )
          return Format( "v%02X:%04X", Bank - 0x4000, Address );

        return Format( "%X:%04X", Bank, Address );
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
        TrapFromLabel = false;
        TrapCartFrame = 0;
        TrapBank = 0;
        memset( TrapRegisters, 0, sizeof(TrapRegisters) );
        memset( TrapStack, 0, sizeof(TrapStack) );
        memset( RingFrom, 0, sizeof(RingFrom) );
        memset( RingTo, 0, sizeof(RingTo) );
        LabelsFromFile = false;
        DtrapOffset = 0xFFFFFFFF;
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
        TrapFrom.clear();
        TrapTo.clear();

        HavePrevious = false;
        RingCount = 0;
        RingFrozen = false;
        CartFrame0 = -1;
        StartedMidRun = false;
        Trapped = false;
        TrapFromLabel = false;
        LabelsFromFile = false;
        LabelsPath = "";
        DtrapOffset = 0xFFFFFFFF;
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

        if( !Console->CPU.Waiting && !Console->CPU.Halted )
          Record.Flags |= FrameOverran;

        float GPULoad = Console->LastGPULoads[ 0 ];
        Record.GPULoad = (uint8_t)max( 0.0f, min( 255.0f, GPULoad ) );

        // a red clear color is how gbtov32 carts show an untranslated jump
        // (a hand written cartridge may also just like red: no harm done)
        const GPUColor& Clear = Console->GPU.ClearColor;

        if( !Trapped && CartFrame0 >= 0 && Clear.R == 255 && Clear.G == 0 && Clear.B == 0 && Clear.A == 255 )
          CaptureTrap( false );

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

    void V32Trace::OnTrapEntry()
    {
        if( Trapped )
          return;

        // freeze the trail of jumps before the red screen routine
        // fills it with its own loops
        CaptureTrap( true );
        RingFrozen = true;
    }

    // -----------------------------------------------------------------------------

    void V32Trace::CaptureTrap( bool FromLabel )
    {
        if( !Console )
          return;

        Trapped = true;
        TrapFromLabel = FromLabel;
        TrapCartFrame = CurrentCartFrame();

        V32CPU& CPU = Console->CPU;

        for( int i = 0; i < 11; i++ )
          TrapRegisters[ i ] = CPU.Registers[ i ].AsBinary;

        TrapRegisters[ 11 ] = CPU.CountRegister.AsBinary;
        TrapRegisters[ 12 ] = CPU.SourceRegister.AsBinary;
        TrapRegisters[ 13 ] = CPU.DestinationRegister.AsBinary;
        TrapRegisters[ 14 ] = CPU.BasePointer.AsBinary;
        TrapRegisters[ 15 ] = CPU.StackPointer.AsBinary;

        // translator state: current ROM bank and the bytes at the Game Boy SP
        const vector< V32Word >& RAM = Console->RAM.Memory;
        TrapBank = (GBRuntimeBank < RAM.size())? RAM[ GBRuntimeBank ].AsBinary : 0;
        uint32_t GBSP = TrapRegisters[ 6 ] & 0xFFFF;

        for( uint32_t i = 0; i < sizeof(TrapStack); i++ )
        {
            uint32_t Index = GBMemoryBase + ((GBSP + i) & 0xFFFF);
            TrapStack[ i ] = (Index < RAM.size())? (uint8_t)(RAM[ Index ].AsBinary & 0xFF) : 0;
        }

        // the trail, oldest first
        uint32_t Count = min( RingCount, RingSize );
        TrapFrom.clear();
        TrapTo.clear();

        for( uint32_t i = 0; i < Count; i++ )
        {
            uint32_t Slot = (RingCount - Count + i) % RingSize;
            TrapFrom.push_back( RingFrom[ Slot ] );
            TrapTo.push_back( RingTo[ Slot ] );
        }

        WritePending = true;
    }


    // =============================================================================
    //      TRACE: TRANSLATOR LABELS
    // =============================================================================


    void V32Trace::LoadLabels()
    {
        Labels.clear();
        Blocks.clear();
        DtrapOffset = 0xFFFFFFFF;
        LabelsFromFile = false;

        // the translator writes labels.txt next to the cartridge it builds
        string Path = GetDirectoryOf( CartridgePath ) + "labels.txt";
        ifstream File;
        OpenInputFile( File, Path );

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

        for( const Label& L: Labels )
          if( L.Name == "_dtrap" )
            DtrapOffset = L.Offset;
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

    // decide which labels belong to a translated Game Boy block, so
    // that time can be reported per Game Boy routine and not per label
    void V32Trace::AssignBlocks()
    {
        map< uint32_t, int32_t > BlockByKey;
        int32_t Current = -1;

        for( Label& L: Labels )
        {
            uint32_t Key = 0;

            if( ParseBlockKey( L.Name, Key ) )
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
            else if( Current >= 0 && (StartsWith( L.Name, "_bl_" ) || StartsWith( L.Name, "_pu" ) || StartsWith( L.Name, "_ldbb" )) )
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

    // the Game Boy block (bank:address) of a cartridge address, or "" if none
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

        // translated Game Boy blocks (bank:address is what --root takes)
        if( Blocks.empty() )
          return;

        Out += Format( "Game Boy blocks: %.1f%% of instructions run in translated blocks, the rest in runtime code\n", 100.0 * InBlocks / Total );

        vector< size_t > BlockOrder;

        for( size_t i = 0; i < ByBlock.size(); i++ )
          if( ByBlock[ i ] )
            BlockOrder.push_back( i );

        sort( BlockOrder.begin(), BlockOrder.end(), [&]( size_t A, size_t B ) { return ByBlock[ A ] > ByBlock[ B ]; } );

        for( int i = 0; i < TopCount && i < (int)BlockOrder.size(); i++ )
        {
            size_t Index = BlockOrder[ i ];
            Out += Format( "  %8llu %5.1f%%  GB %s\n", (unsigned long long)ByBlock[ Index ], 100.0 * ByBlock[ Index ] / Total, Blocks[ Index ].Display.c_str() );
        }
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

        string Out = "# Game Boy blocks that ran (bank:address in hex as --root takes it, instructions executed)\n";
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
        Out += "# v32trace 1\n";
        Out += "# Profiling trace written by the Vircon32 DesktopEmulator. Send this whole file.\n";
        Out += Format( "session: started %s local time; this copy written because: %s\n", StartedAt.c_str(), Reason );
        Out += Format( "cart: file=\"%s\" title=\"%s\" words=%u fnv1a=%08X\n", CartName.c_str(), CartTitle.c_str(), (unsigned)CartWords, (unsigned)CartHash );
        Out += Format( "bios: %s\n", BiosName.c_str() );

        if( LabelsFromFile )
          Out += Format( "labels: %s (%u labels, %u Game Boy blocks)\n", LabelsPath.c_str(), (unsigned)Labels.size(), (unsigned)Blocks.size() );
        else
          Out += "labels: none found (keep the labels.txt that gbtov32 writes in the same folder as the .v32 for readable names and Game Boy addresses)\n";

        Out += Format( "frames: total=%u boot=%d cart=%d%s\n", (unsigned)Frames.size(), BootFrames, CartFrames,
                       StartedMidRun? " (recording started while the cartridge was already running: cart frame 0 is that moment)" : "" );
        Out += "frame numbers below are cartridge frames: 0 is the first frame in which the cartridge ran\n";

        // events: traps, errors, markers
        Out += "\n[events]\n";

        if( Events.empty() && !Trapped )
          Out += "none\n";

        for( const Event& E: Events )
          Out += Format( "frame %d: %s\n", E.CartFrame, E.Text.c_str() );

        if( Trapped )
        {
            uint32_t TrapPC = TrapRegisters[ 7 ] & 0xFFFF;
            Out += Format( "TRAP at frame %d: GB pc=%04X, ROM bank %u (dispatch miss or untranslated code)%s\n",
                           TrapCartFrame, (unsigned)TrapPC, (unsigned)(TrapBank & 0xFFFF),
                           TrapFromLabel? "" : " [found by the red clear color, the trap routine label is unknown]" );

            if( TrapPC >= 0x8000 )
              Out += Format( "  RAM code at %04X (copied there at run time): --root cannot add it; build with a gbprof profile (--profile or --profile-ram)\n", (unsigned)TrapPC );
            else
              Out += Format( "  rebuild with: --root %X:%04X\n", TrapPC < 0x4000? 0u : (unsigned)(TrapBank & 0xFFFF), (unsigned)TrapPC );

            Out += Format( "  A=%02X F=%02X BC=%04X DE=%04X HL=%04X SP=%04X stack:",
                           (unsigned)(TrapRegisters[ 1 ] & 0xFF), (unsigned)(TrapRegisters[ 2 ] & 0xFF),
                           (unsigned)(TrapRegisters[ 3 ] & 0xFFFF), (unsigned)(TrapRegisters[ 4 ] & 0xFFFF),
                           (unsigned)(TrapRegisters[ 5 ] & 0xFFFF), (unsigned)(TrapRegisters[ 6 ] & 0xFFFF) );

            for( size_t i = 0; i < 10; i++ )
              Out += Format( " %02X", TrapStack[ i ] );

            Out += "\n";

            // the Game Boy blocks entered right before the trap, oldest first
            string Trail;
            string Last;
            int Shown = 0;

            for( size_t i = TrapTo.size(); i > 0 && Shown < 32; i-- )
            {
                string Block = DescribeBlock( TrapTo[ i - 1 ] );

                if( Block.empty() || Block == Last )
                  continue;

                Trail = Block + " " + Trail;
                Last = Block;
                Shown++;
            }

            if( !Trail.empty() )
              Out += "  GB block trail (oldest first, last " + Format( "%d", Shown ) + "): " + Trail + "\n";

            Out += "  V32 transfer trail (last 12, oldest first):\n";

            size_t First = (TrapFrom.size() > 12)? TrapFrom.size() - 12 : 0;

            for( size_t i = First; i < TrapFrom.size(); i++ )
              Out += "    " + DescribeAddress( TrapFrom[ i ] ) + " -> " + DescribeAddress( TrapTo[ i ] ) + "\n";

            Out += Format( "  R7=%08X (the trapped target), R15=%08X\n", (unsigned)TrapRegisters[ 7 ], (unsigned)TrapRegisters[ 15 ] );
        }

        // input history
        Out += "\n[input]\n";
        Out += Format( "# gamepad 1, cart frames 0-%d. Replay with: V32_INPUT=@this_file v32sim cart.v32 %d  (or gbprof)\n", max( 0, CartFrames - 1 ), CartFrames );
        Out += "V32_INPUT=" + InputScript + "\n";

        // performance
        Out += "\n[perf]\n";
        
        // after a red screen the cartridge only repeats the trap routine
        int32_t PerfFrames = CartFrames;
        
        if( Trapped && TrapCartFrame >= 0 && TrapCartFrame < CartFrames )
        {
            PerfFrames = TrapCartFrame;
            Out += Format( "(only frames 0-%d, before the red screen)\n", max( 0, PerfFrames - 1 ) );
        }

        if( PerfFrames > 0 )
        {
            uint64_t Sum = 0, GPUSum = 0;
            uint32_t Max = 0, OverBudget = 0, Overran = 0, Errors = 0;

            for( int32_t i = 0; i < PerfFrames; i++ )
            {
                const FrameRecord& R = Frames[ CartFrame0 + i ];
                Sum += R.Cycles;
                GPUSum += R.GPULoad;
                Max = max( Max, R.Cycles );
                if( (int32_t)R.Cycles > Budget ) OverBudget++;
                if( R.Flags & FrameOverran ) Overran++;
                if( R.Flags & FrameCPUError ) Errors++;
            }

            Out += Format( "cycles/frame from frame 0: avg %llu max %u, %u of %d frames over %d\n",
                           (unsigned long long)(Sum / PerfFrames), (unsigned)Max, (unsigned)OverBudget, PerfFrames, Budget );
            Out += Format( "frames where the cartridge missed its WAIT (dropped frames): %u; frames with a CPU error: %u; average GPU load %llu%%\n",
                           (unsigned)Overran, (unsigned)Errors, (unsigned long long)(GPUSum / PerfFrames) );

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
                uint32_t RowMax = 0, RowDropped = 0, RowGPU = 0;

                for( int32_t i = Start; i < End; i++ )
                {
                    const FrameRecord& R = Frames[ CartFrame0 + i ];
                    RowSum += R.Cycles;
                    RowMax = max( RowMax, R.Cycles );
                    RowGPU = max< uint32_t >( RowGPU, R.GPULoad );
                    if( R.Flags & FrameOverran ) RowDropped++;
                }

                Out += Format( "  %6d-%-6d %7llu %7u %4u %4u%%\n", Start, End - 1, (unsigned long long)(RowSum / (End - Start)),
                               (unsigned)RowMax, (unsigned)RowDropped, (unsigned)RowGPU );
            }
        }
        else
          Out += "the cartridge never ran\n";

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

            Callbacks::LogLine( "Trace written to " + Path );
        }

        catch( const exception& e )
        {
            Callbacks::LogLine( string("Trace: failed to write the report: ") + e.what() );
        }
    }
}
