// *****************************************************************************
    // start include guard
    #ifndef V32TRACE_HPP
    #define V32TRACE_HPP

    // include common Vircon32 headers
    #include "../VirconDefinitions/Constants.hpp"

    // include C/C++ headers
    #include <cstdint>          // [ ANSI C ] Fixed size integers
    #include <string>           // [ C++ STL ] Strings
    #include <vector>           // [ C++ STL ] Vectors
// *****************************************************************************


namespace V32
{
    class V32Console;


    // =============================================================================
    //      PROFILING TRACE RECORDER
    // =============================================================================


    // Records what a cartridge does while it is played, as a text report that
    // the Vircon32 tools understand (V32_INPUT button scripts, "PC profile" and
    // "TRAP at frame" lines in the same format v32sim prints). It never changes
    // what the console does: it only counts and copies.
    //
    // The cost while recording is one table increment per executed instruction;
    // while not recording the CPU only tests a null pointer.

    class V32Trace
    {
        public:

            // user switches
            bool Armed;         // record every power-on / reset session
            bool Recording;     // a session is being recorded right now
            bool ContinueAfterTrap;     // restart the cartridge after a trap, so one session can collect several

            // where reports go (folder, with trailing separator) and
            // where the labels.txt of the translator is looked for (set by
            // the emulator; labels are optional but make the report readable)
            std::string OutputFolder;
            std::string LastReportPath;
            
            // full path of the loaded cartridge (set by the console), used
            // to find the labels.txt that the translator writes beside it
            std::string CartridgePath;

        private:

            // console being observed
            V32Console* Console;

            // cartridge facts, captured when a session begins
            uint32_t CartWords;
            uint32_t CartHash;
            std::string CartName;
            std::string CartTitle;
            std::string BiosName;
            std::string StartedAt;
            std::string BaseName;

            // execution counters: one 32-bit counter per instruction start of
            // the cartridge program, in pages that exist only once executed
            std::vector< uint32_t* > Pages;

            // control flow tracking
            bool HavePrevious;
            uint32_t PreviousAddress;
            uint32_t ExpectedAddress;

            // ring of the last control transfers (source -> destination)
            static const uint32_t RingSize = 512;
            uint32_t RingFrom[ RingSize ];
            uint32_t RingTo[ RingSize ];
            uint32_t RingCount;
            bool RingFrozen;

            // per frame records
            struct FrameRecord
            {
                uint32_t Cycles;
                uint16_t Pad;
                uint8_t  Flags;
                uint8_t  GPULoad;
                uint32_t HostUs;        // wall time of the frame in the emulator (0 when not measured, e.g. headless)
                uint16_t Quads;         // quads the GPU was asked to draw
                uint16_t Flushes;       // batches the video queue had to send (see V32GPU::StatChange)
            };

            std::vector< FrameRecord > Frames;
            int32_t CartFrame0;         // index in Frames of cartridge frame 0, or -1
            bool StartedMidRun;

            // markers set by the player
            struct Marker
            {
                int32_t CartFrame;
                std::string Note;
                std::vector< uint64_t > LabelSnapshot;
            };

            std::vector< Marker > Markers;

            // events
            struct Event
            {
                int32_t CartFrame;
                std::string Text;
            };

            std::vector< Event > Events;

            // traps (the cartridge stopped) and soft events (the cartridge went on), as captured when they happened;
            // identical ones (same name and same non-volatile captures) are counted, not repeated
            struct Hit
            {
                bool IsEvent;
                std::string Name;               // "trap", or the event name
                std::string Key;                // what makes two hits the same
                std::string Kind;               // name of the trap kind (meta `kinds`), or ""
                int32_t CartFrame;              // first occurrence
                uint32_t Count;
                uint32_t Regs[ 16 ];
                std::vector< std::vector< uint32_t > > Values;      // per capture
                std::vector< uint32_t > From, To;                   // jump trail
            };

            std::vector< Hit > Hits;
            uint32_t HitsDropped;
            bool Trapped;                       // the cartridge is stopped in a trap routine right now
            int32_t HeldFrames;                 // frames spent in the current trap
            bool ResumeGaveUp;                  // the same trap came back three times in a row: stop restarting
            uint32_t Restarts;
            std::string LastTrapKey;
            int RepeatRun;

            // translator labels (word offsets into the cartridge)
            struct Label
            {
                uint32_t Offset;
                std::string Name;
                int32_t Block;          // translated block this label is part of, or -1
            };

            struct Block
            {
                uint32_t Key;
                std::string Display;
            };

            std::vector< Label > Labels;
            std::vector< Block > Blocks;
            bool LabelsFromFile;
            std::string LabelsPath;

            // labels the recorder watches: trap routines and soft events
            struct Watch
            {
                uint32_t Offset;
                bool IsTrap;
                std::string Name;
            };

            struct WatchSpec
            {
                bool IsTrap;
                std::string Name;
                std::string Label;
            };

            std::vector< WatchSpec > WatchSpecs;
            std::vector< Watch > Watches;
            std::vector< uint32_t > WatchBits;      // one bit per cartridge word
            bool HaveResume;
            std::string ResumeLabel;
            uint32_t ResumeOffset;                  // where the cartridge restarts after a trap (word offset)
            bool HaveHash;
            uint32_t ExpectedHash;

            // what the translator says about itself (<cart>.v32trace.meta or v32trace.meta, see the V32TRACE SPEC).
            // Without that file the recorder still reports cycles, host cost and hot spots, but knows no guest.
            //   arch NAME             name used in the report (nes, gb, ...)
            //   trap LABEL            a label whose entry means "the cartridge stopped" (up to 2)
            //   block FORMAT          sscanf format of a block label with two hex fields (bank, address), e.g. _b%x_%x
            //   continue PREFIX       labels with this prefix + a digit belong to the block before them
            //   capture NAME ADDR N   N RAM words at hex word address ADDR, copied when the trap is entered
            struct Capture
            {
                std::string Name;
                uint32_t Address;
                uint32_t Count;
                std::vector< uint32_t > Values;
                int Reg;                // >= 0: the value of CPU register R<Reg> (masked) instead of RAM
                int IndexReg;           // >= 0: RAM address is Address + (R<IndexReg> & Mask)
                uint32_t Mask;
                bool Volatile;          // left out of the key that decides whether two traps are the same
            };

            struct Series
            {
                std::string Name;
                uint32_t Address;
                std::vector< int32_t > Values;      // one per recorded frame
            };

            std::vector< Series > SeriesList;
            std::vector< std::pair< std::string, std::vector< std::pair< uint32_t, std::string > > > > KindNames;   // capture -> value names
            bool MetaStale;
            std::string MetaProblem;
            uint32_t MetaWords;

            bool HaveMeta;
            std::string Arch;
            struct BlockPattern
            {
                std::string Format;     // scanf pattern of a block's first label
                bool Packed;            // one number = bank << 16 | address
                int32_t FixedBank;      // one number = address in this bank (-1: pattern gives bank and address)
                uint32_t BankAdd;       // added to the bank (for translators that number code variants above the real banks)
            };

            std::vector< BlockPattern > BlockPatterns;
            std::vector< std::string > ContinuePrefixes;
            std::vector< Capture > Captures;

            // saving
            bool WritePending;

        public:

            V32Trace();
           ~V32Trace();

            // session control
            void Attach( V32Console* Owner );
            void Arm( bool On );
            void BeginSession( V32Console& Owner );
            void EndSession( const char* Reason );
            void AddMarker( const std::string& Note );

            // hooks called by the console
            void BeginFrame();
            void EndFrame();
            void OnCPUError( uint32_t Code, uint32_t Address );

            // hook called by the emulator after it ran a frame: how long that took on the host
            // and whether it was a catch-up frame (the loop owed frames and ran several in a row)
            void HostFrame( uint32_t Microseconds, bool CatchUp );

            // hook called by the CPU for every executed instruction
            // (after fetching it, before running it)
            inline void OnInstruction( uint32_t Address, bool UsesImmediate )
            {
                uint32_t Offset = Address - (uint32_t)Constants::CartridgeProgramROMFirstAddress;

                if( Offset < CartWords )
                {
                    uint32_t*& Page = Pages[ Offset >> 10 ];

                    if( !Page )
                      NewPage( Page );

                    if( !Trapped )
                      Page[ Offset & 1023 ]++;

                    if( CartFrame0 < 0 )
                      CartStarted();

                    if( !WatchBits.empty() && ((WatchBits[ Offset >> 5 ] >> (Offset & 31)) & 1) )
                      OnWatch( Offset );
                }

                if( HavePrevious && Address != ExpectedAddress && !RingFrozen )
                  PushTransfer( PreviousAddress, Address );

                HavePrevious = true;
                PreviousAddress = Address;
                ExpectedAddress = Address + (UsesImmediate? 2 : 1);
            }

        private:

            // session internals
            void ResetSessionData();
            void NewPage( uint32_t*& Page );
            void CartStarted();
            void OnWatch( uint32_t Offset );
            void CaptureHit( bool IsTrap, const std::string& Name );
            void TryResume();
            void PushTransfer( uint32_t From, uint32_t To );
            int32_t CurrentCartFrame() const;
            void AddEvent( const std::string& Text );

            // labels
            void LoadLabels();
            void LoadMeta( const std::string& Folder, const std::string& Stem );
            bool ParseMetaBlock( const std::string& Name, uint32_t& Key ) const;
            std::string SummarizeSeries( int32_t FirstFrame, int32_t LastFrame ) const;
            void ReadCapture( const Capture& C, const uint32_t* Regs, std::vector< uint32_t >& Out ) const;
            void LiveRegisters( uint32_t* Regs ) const;
            std::string CaptureText( const Capture& C ) const;
            std::string CaptureNow( const Capture& C ) const;
            std::string BuildTrapLines() const;
            std::string DescribeTrail( const Hit& H, int Limit, const char* Separator ) const;
            std::string SummarizeHost( int32_t FirstFrame, int32_t LastFrame ) const;
            void BuildSyntheticLabels();
            void AssignBlocks();
            int32_t FindLabel( uint32_t Offset ) const;
            std::string DescribeAddress( uint32_t Address ) const;
            std::string DescribeBlock( uint32_t Address ) const;

            // report generation
            void Aggregate( std::vector< uint64_t >& ByLabel ) const;
            void AppendProfile( std::string& Out, const std::vector< uint64_t >& ByLabel,
                                int32_t FirstFrame, int32_t LastFrame, int TopCount ) const;
            std::string BuildInputScript( int32_t& CartFrames ) const;
            std::string BuildReport( const char* Reason ) const;
            std::string BuildBlockList() const;
            void WriteReport( const char* Reason );
    };
}


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
