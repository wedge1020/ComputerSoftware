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
    // the gbtov32 tools understand (V32_INPUT button scripts, "PC profile" and
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

            // trap (red screen) details, as captured when it happened
            bool Trapped;
            bool TrapFromLabel;
            int32_t TrapCartFrame;
            uint32_t TrapRegisters[ 16 ];
            uint32_t TrapBank;
            uint8_t TrapStack[ 16 ];
            std::vector< uint32_t > TrapFrom, TrapTo;

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
            uint32_t DtrapOffset;

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

                    Page[ Offset & 1023 ]++;

                    if( CartFrame0 < 0 )
                      CartStarted();

                    if( Offset == DtrapOffset )
                      OnTrapEntry();
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
            void OnTrapEntry();
            void CaptureTrap( bool FromLabel );
            void PushTransfer( uint32_t From, uint32_t To );
            int32_t CurrentCartFrame() const;
            void AddEvent( const std::string& Text );

            // labels
            void LoadLabels();
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
