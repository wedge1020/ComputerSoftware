// *****************************************************************************
    // start include guard
    #ifndef V32MEMORYCARDCONTROLLER_HPP
    #define V32MEMORYCARDCONTROLLER_HPP

    // include console logic headers
    #include "V32Buses.hpp"
    #include "V32Memory.hpp"

    // include C/C++ headers
    #include <string>           // [ C++ STL ] Strings
    #include <vector>           // [ C++ STL ] Vectors
    #include <fstream>          // [ C++ STL ] File streams
// *****************************************************************************


namespace V32
{
    // =============================================================================
    //      STORAGE CONTROLLER DEFINITIONS
    // =============================================================================


    // LOCAL port numbers!
    // (to be added the initial address)
    enum class MEM_LocalPorts: int32_t
    {
        Connected = 0
    };

    // used as limit of local port numbers
    const int32_t MEM_LastPort = (int32_t)MEM_LocalPorts::Connected;


    // =============================================================================
    //      V32DEV EXTERNAL MAILBOX DEFINITIONS
    // =============================================================================


    // The last 64 KiB of the memory card can be formatted by an external tool
    // as a "v32dev mailbox": 4 channels, each one a pair of rings (host to cart,
    // cart to host). When the card file has a valid mailbox header, the emulator
    // exchanges that region with the file every frame so an external process can
    // communicate with the running program. A card without that header behaves
    // as a regular memory card. See v32dev-protocol.md for the complete protocol.
    namespace DEV
    {
        // card geometry (in 32-bit words)
        const int32_t CardWords     = 256 * 1024;
        const int32_t PageShift     = 8;                            // dirty tracking: 256-word pages
        const int32_t PageWords     = 1 << PageShift;
        const int32_t NumberOfPages = CardWords / PageWords;

        // mailbox region, located at the end of the card
        const int32_t RegionWords   = 16 * 1024;                    // 64 KiB
        const int32_t RegionStart   = CardWords - RegionWords;
        const int32_t HeaderWords   = 64;
        const int32_t Channels      = 4;
        const int32_t PartWords     = 2040;                         // per channel and direction
        const int32_t ControlWords  = 16;                           // control words at the start of each part
        const int32_t RingWords     = PartWords - ControlWords;     // data capacity of each ring

        // the region is: header, then host-owned area, then cart-owned area
        // (each area holds one part for every channel, in channel order)
        const int32_t HostAreaStart = RegionStart + HeaderWords;
        const int32_t HostAreaWords = Channels * PartWords;
        const int32_t CartAreaStart = HostAreaStart + HostAreaWords;
        const int32_t CartAreaWords = Channels * PartWords;

        static_assert( HeaderWords + HostAreaWords + CartAreaWords == RegionWords, "v32dev mailbox layout does not fill its region" );

        // header word indices (relative to the region start)
        const int32_t H_Magic           = 0;    // static: DEV::Magic
        const int32_t H_Version         = 1;    // static: DEV::Version
        const int32_t H_RegionWords     = 2;    // static: DEV::RegionWords
        const int32_t H_Channels        = 3;    // static: DEV::Channels
        const int32_t H_PartWords       = 4;    // static: DEV::PartWords
        const int32_t H_ControlWords    = 5;    // static: DEV::ControlWords
        const int32_t H_EmulatorFlags   = 8;    // emulator-owned, never written to the file
        const int32_t H_EmulatorBeat    = 9;    // emulator-owned, never written to the file

        // values for the header
        const int32_t Magic         = 0x31564544;                   // bytes "DEV1"
        const int32_t Version       = 1;
        const int32_t EmuFlagLive   = 1;                            // H_EmulatorFlags bit 0: emulator syncs this mailbox

        // a contiguous range of card words to be written to the card file
        struct SaveSegment
        {
            int32_t FirstWord;
            int32_t NumberOfWords;
            int     Phase;                                          // 1 = data, 2 = commit words
        };
    }


    // =============================================================================
    //      MEMORY CARD CONTROLLER CLASS
    // =============================================================================


    class V32MemoryCardController: public VirconControlInterface, public V32RAM
    {
        public:

            // file save control
            std::fstream LinkedFile;
            bool PendingSave;

            // words modified since the last save, tracked per page
            std::vector< uint8_t > DirtyPages;

            // displayed file name for GUI
            std::string CardFileName;

            // v32dev mailbox state
            bool MailboxActive;
            std::ifstream PollFile;             // separate, unbuffered handle to read the host area
            std::vector< V32Word > PollBuffer;
            uint32_t Heartbeat;

        public:

            // instance handling
            V32MemoryCardController();
           ~V32MemoryCardController();

            // connection to control bus
            virtual bool ReadPort( int32_t LocalPort, V32Word& Result );
            virtual bool WritePort( int32_t LocalPort, V32Word Value );

            // connection to memory bus (overriden)
            virtual bool WriteAddress( int32_t LocalAddress, V32Word Value );

            // v32dev mailbox support
            void ClearDirtyPages();
            bool HeaderIsValid();
            void PlanSave( std::vector< DEV::SaveSegment >& Segments );
    };
}


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
