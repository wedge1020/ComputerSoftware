// *****************************************************************************
    // start include guard
    #ifndef V32DEVSERVER_HPP
    #define V32DEVSERVER_HPP

    // include console logic headers
    #include "V32MemoryCardController.hpp"

    // include C/C++ headers
    #include <string>           // [ C++ STL ] Strings
    #include <vector>           // [ C++ STL ] Vectors
    #include <deque>            // [ C++ STL ] Double-ended queues
    #include <memory>           // [ C++ STL ] Smart pointers
    #include <cstdio>           // [ ANSI C ] File I/O
// *****************************************************************************


namespace V32
{
    // =============================================================================
    //      V32DEV EMBEDDED SERVER
    // =============================================================================
    //
    // The v32dev host (what tools/v32dev.py is) built into the console. It serves
    // the 4 channels of the memory card mailbox to the program, but instead of
    // going through the card FILE it reads and writes the card MEMORY directly, from
    // inside the frame loop (see V32Console::SyncMemoryCardMailbox). The protocol is
    // exactly the one in docs/v32dev/PROTOCOL.md, so programs can't tell the two
    // hosts apart, apart from the latency being the minimum (one frame) always.
    //
    // Everything here is single threaded and runs between frames: the program never
    // runs while the server touches the mailbox, so there are no races and no
    // ordering rules to follow (the file ordering rules only exist for readers in
    // other processes).
    //
    // A device only reaches what the user configured it for (a file chosen in the
    // menu, a loopback port); the program can't ask for anything else.


    namespace DEV
    {
        // control word indices inside a host-owned part (the host writes them)
        const int32_t HP_Type       = 0;
        const int32_t HP_Flags      = 1;
        const int32_t HP_Beat       = 2;
        const int32_t HP_H2CHead    = 3;
        const int32_t HP_C2HTail    = 4;
        const int32_t HP_Session    = 5;
        const int32_t HP_Status     = 6;
        const int32_t HP_Descriptor = 8;        // 8 words

        // control word indices inside a cart-owned part (the program writes them)
        const int32_t CP_Flags      = 0;
        const int32_t CP_C2HHead    = 1;
        const int32_t CP_H2CTail    = 2;
        const int32_t CP_Beat       = 3;
        const int32_t CP_Session    = 4;
        const int32_t CP_Status     = 5;

        const uint32_t HostFlagAttached = 1;
        const uint32_t StatusReady = 1;
        const uint32_t StatusError = 2;
        const uint32_t StatusHangUp = 4;

        const int32_t MessageMaxWords = 2020;   // most payload words in a message

        // device classes and kinds (type word = class << 24 | kind)
        const uint32_t ClassBlock = 1, ClassChar = 2;
        inline uint32_t MakeType( uint32_t Class, uint32_t Kind ) { return (Class << 24) | Kind; }

        // the channels the program can use
        const int32_t MaxChannels = DEV::Channels;


        // -------------------------------------------------------------------------
        //   A CHANNEL, SEEN FROM THE HOST SIDE
        // -------------------------------------------------------------------------
        // Messages are [N][opcode][N payload words]; a message is published all at
        // once (data first, then the ring index).

        struct Message
        {
            uint32_t Opcode;
            std::vector< uint32_t > Payload;
        };

        class ServerChannel
        {
            private:
                V32Word* Card;                      // word 0 of the card memory
                int32_t HostPart, CartPart;         // word index of each part
                std::vector< uint32_t > Pending;    // received words not yet made into messages
                uint32_t CartSessionSeen;           // the program's session, as of the last check

            public:
                int BadEvents;                      // corrupt ring indices or lengths seen

            private:
                uint32_t& HostWord( int32_t Index ) { return Card[ HostPart + Index ].AsBinary; }
                uint32_t& CartWord( int32_t Index ) { return Card[ CartPart + Index ].AsBinary; }

            public:
                ServerChannel();
                void Bind( V32Word* CardMemory, int Channel );
                bool IsBound() const { return Card != nullptr; }

                // state
                void Announce( uint32_t TypeWord, const std::vector< uint32_t >& Descriptor, uint32_t Status );
                void Withdraw();
                void Beat();
                void SetStatus( uint32_t Status );
                uint32_t CartStatus();
                void Reset();
                bool CartIsOpen();
                bool CheckCart();           // true if the program opened the channel again

                // host to cart ring (the host produces)
                int32_t FreeWords();
                bool Write( const uint32_t* Words, size_t Count );
                bool Send( uint32_t Opcode, const std::vector< uint32_t >& Payload );

                // cart to host ring (the host consumes)
                size_t Read( std::vector< uint32_t >& Words );
                void Receive( std::vector< Message >& Messages );
        };


        // -------------------------------------------------------------------------
        //   DEVICES: WHAT IS AT THE HOST END OF A CHANNEL
        // -------------------------------------------------------------------------

        class Device
        {
            public:
                uint64_t WordsFromProgram;
                uint64_t WordsToProgram;

                Device(): WordsFromProgram( 0 ), WordsToProgram( 0 ) {}
                virtual ~Device() {}

                virtual uint32_t TypeWord() const = 0;
                virtual std::vector< uint32_t > Descriptor() const { return std::vector< uint32_t >(); }
                virtual std::string Describe() const = 0;

                // called when the channel is announced
                virtual void Start( ServerChannel& Channel ) {}

                // called every frame; returns true if it did some work
                virtual bool Poll( ServerChannel& Channel ) = 0;
        };


        // what the user asked for on a channel, as text (the same words as the
        // host tool's --chN option, plus serial:tcp):
        //   storage:FILE[,bytes][,ro][,sector=N][,create=SECTORS]
        //   serial:echo[,hup=N]
        //   serial:stdio
        //   serial:tcp=PORT        (listens on 127.0.0.1 only)
        // The options are recognized from the right, so FILE can have commas.
        struct DeviceSpec
        {
            enum Kinds { None, Storage, SerialEcho, SerialStdio, SerialTcp } Kind;

            std::string Path;
            bool BytesMode;
            bool ReadOnly;
            int  SectorWords;           // 0 = default for the mode
            long CreateSectors;         // 0 = do not create
            long HangUpAfter;           // echo: hang up after this many words (<0 = never)
            int  Port;

            DeviceSpec(): Kind( None ), BytesMode( false ), ReadOnly( false ), SectorWords( 0 ),
                          CreateSectors( 0 ), HangUpAfter( -1 ), Port( 0 ) {}

            static bool Parse( const std::string& Text, DeviceSpec& Result, std::string& Error );
            std::string ToString() const;
        };

        // creates the device a spec asks for (opens its file, binds its port...)
        std::unique_ptr< Device > CreateDevice( const DeviceSpec& Spec, std::string& Error );


        // -------------------------------------------------------------------------
        //   THE SERVER: 4 CHANNELS, THEIR CONFIGURATION AND THEIR DEVICES
        // -------------------------------------------------------------------------

        // what the user configured: stored in the settings, set from the menu
        struct ServerConfig
        {
            bool Enabled;
            std::string Specs[ DEV::Channels ];     // "" = nothing on the channel

            ServerConfig(): Enabled( false ) {}
        };

        // what the menu shows about a channel
        struct ChannelInfo
        {
            std::string Spec;                       // what was asked for
            std::string Description;                // what is being served ("" = nothing)
            std::string Error;                      // why the channel is not served
            bool Serving;
            bool ProgramOpen;                       // the program has opened the channel
            uint64_t WordsFromProgram;
            uint64_t WordsToProgram;

            ChannelInfo(): Serving( false ), ProgramOpen( false ), WordsFromProgram( 0 ), WordsToProgram( 0 ) {}
        };

        class Server
        {
            private:
                ServerConfig Active;                // what is in effect
                ServerConfig Stored;                // what the settings file has, while
                                                    // the environment controls Active
                bool ControlledByEnvironment;

                bool Attached;
                ServerChannel Channels[ DEV::Channels ];
                std::unique_ptr< Device > Devices[ DEV::Channels ];
                std::string Errors[ DEV::Channels ];

            private:
                void StartChannel( int Channel );
                void StopChannel( int Channel );

            public:
                Server();
               ~Server();

                // V32DEV_SERVER=0|1 and V32DEV_CH0..V32DEV_CH3 (the same specs) take
                // over the configuration and keep the settings file out of it
                void ApplyEnvironment();
                bool IsControlledByEnvironment() const { return ControlledByEnvironment; }

                // configuration
                bool IsEnabled() const { return Active.Enabled; }
                void SetEnabled( bool Enabled ) { Active.Enabled = Enabled; }
                std::string GetSpec( int Channel ) const;
                bool SetSpec( int Channel, const std::string& Spec, std::string& Error );

                // what the settings file keeps (see ControlledByEnvironment)
                ServerConfig GetStoredConfig() const;
                void SetStoredConfig( const ServerConfig& Config );

                // attaching to a card mailbox. CardMemory is word 0 of the card
                bool IsAttached() const { return Attached; }
                void Attach( V32Word* CardMemory );
                void Detach();

                // once per frame, before the program runs
                void Poll();

                // information for the menu
                ChannelInfo GetChannelInfo( int Channel );
        };
    }
}


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
