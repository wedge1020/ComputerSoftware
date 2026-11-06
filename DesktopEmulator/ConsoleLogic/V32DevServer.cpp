// large files on 32 bit systems (must come before any system header)
#ifndef _FILE_OFFSET_BITS
  #define _FILE_OFFSET_BITS 64
#endif

// *****************************************************************************
    // include console logic headers
    #include "V32DevServer.hpp"

    // include C/C++ headers
    #include <algorithm>        // [ C++ STL ] Algorithms
    #include <cstdlib>          // [ ANSI C ] General utilities
    #include <cstring>          // [ ANSI C ] Strings
    #include <cerrno>           // [ ANSI C ] Error codes
    #include <ctime>            // [ ANSI C ] Time

    // platform specific headers: files, standard input, sockets
    #if defined(_WIN32)
      #ifndef NOMINMAX
        #define NOMINMAX            // (windows.h would define min and max as macros)
      #endif
      #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
      #endif
      #include <winsock2.h>
      #include <ws2tcpip.h>
      #include <io.h>
    #else
      #include <unistd.h>
      #include <fcntl.h>
      #include <poll.h>
      #include <sys/types.h>
      #include <sys/socket.h>
      #include <netinet/in.h>
      #include <netinet/tcp.h>
      #include <arpa/inet.h>
    #endif

    // declare used namespaces
    using namespace std;
// *****************************************************************************


namespace V32
{
namespace DEV
{
    // =============================================================================
    //      SMALL HELPERS
    // =============================================================================


    namespace
    {
        // 64 bit seek and sync that work in every supported platform
        int SeekFile( FILE* File, uint64_t Offset )
        {
          #if defined(_WIN32)
            return _fseeki64( File, (long long)Offset, SEEK_SET );
          #else
            return fseeko( File, (off_t)Offset, SEEK_SET );
          #endif
        }

        bool SyncFile( FILE* File )
        {
            if( fflush( File ) != 0 )
              return false;

          #if defined(_WIN32)
            return _commit( _fileno( File ) ) == 0;
          #else
            return fsync( fileno( File ) ) == 0;
          #endif
        }

        // the file formats are little endian on every host
        void PutWord( uint8_t* Bytes, uint32_t Word )
        {
            Bytes[ 0 ] = (uint8_t)(Word);
            Bytes[ 1 ] = (uint8_t)(Word >> 8);
            Bytes[ 2 ] = (uint8_t)(Word >> 16);
            Bytes[ 3 ] = (uint8_t)(Word >> 24);
        }

        uint32_t GetWord( const uint8_t* Bytes )
        {
            return (uint32_t)Bytes[ 0 ] | ((uint32_t)Bytes[ 1 ] << 8) | ((uint32_t)Bytes[ 2 ] << 16) | ((uint32_t)Bytes[ 3 ] << 24);
        }

        vector< string > Split( const string& Text, char Separator )
        {
            vector< string > Parts;
            size_t Start = 0;

            while( true )
            {
                size_t End = Text.find( Separator, Start );

                if( End == string::npos )
                {
                    Parts.push_back( Text.substr( Start ) );
                    return Parts;
                }

                Parts.push_back( Text.substr( Start, End - Start ) );
                Start = End + 1;
            }
        }

        // strict number parsing: the whole text must be a number in range
        bool ParseNumber( const string& Text, long Minimum, long Maximum, long& Result )
        {
            if( Text.empty() )
              return false;

            char* End = nullptr;
            errno = 0;
            long Value = strtol( Text.c_str(), &End, 0 );

            if( errno != 0 || *End != '\0' || Value < Minimum || Value > Maximum )
              return false;

            Result = Value;
            return true;
        }

        bool StartsWith( const string& Text, const char* Prefix )
        {
            return Text.compare( 0, strlen( Prefix ), Prefix ) == 0;
        }

        // sessions only need to differ from the previous one
        uint32_t NewSession()
        {
            static uint32_t Counter = (uint32_t)time( nullptr ) * 2654435761u;
            Counter += 0x9E3779B1u;
            return (Counter & 0x7FFFFFFFu) | 1u;
        }
    }


    // =============================================================================
    //      A CHANNEL, SEEN FROM THE HOST SIDE
    // =============================================================================


    ServerChannel::ServerChannel(): Card( nullptr ), HostPart( 0 ), CartPart( 0 ), CartSessionSeen( 0 ), BadEvents( 0 )
    {
    }

    // -----------------------------------------------------------------------------

    void ServerChannel::Bind( V32Word* CardMemory, int Channel )
    {
        Card = CardMemory;
        HostPart = DEV::HostAreaStart + Channel * DEV::PartWords;
        CartPart = DEV::CartAreaStart + Channel * DEV::PartWords;
        Pending.clear();
        BadEvents = 0;
        CartSessionSeen = CartWord( CP_Session );
    }

    // -----------------------------------------------------------------------------

    // Starts serving: empties the rings, declares the device (descriptor and
    // status first, the "attached" flag last)
    void ServerChannel::Announce( uint32_t TypeWord, const vector< uint32_t >& Descriptor, uint32_t Status )
    {
        HostWord( HP_Flags ) = 0;
        Reset();
        HostWord( HP_Session ) = NewSession();

        for( int i = 0; i < 8; i++ )
          HostWord( HP_Descriptor + i ) = (i < (int)Descriptor.size()? Descriptor[ i ] : 0);

        HostWord( HP_Status ) = Status;
        HostWord( HP_Type ) = TypeWord;
        Beat();
        HostWord( HP_Flags ) = HostFlagAttached;
    }

    // -----------------------------------------------------------------------------

    void ServerChannel::Withdraw()
    {
        HostWord( HP_Flags ) = 0;
        HostWord( HP_Status ) = 0;
    }

    // -----------------------------------------------------------------------------

    void ServerChannel::Beat()
    {
        HostWord( HP_Beat )++;
    }

    // -----------------------------------------------------------------------------

    void ServerChannel::SetStatus( uint32_t Status )
    {
        HostWord( HP_Status ) = Status;
    }

    // -----------------------------------------------------------------------------

    uint32_t ServerChannel::CartStatus()
    {
        return CartWord( CP_Status );
    }

    // -----------------------------------------------------------------------------

    // Empties both rings, moving the end of each that the host owns
    void ServerChannel::Reset()
    {
        HostWord( HP_H2CHead ) = CartWord( CP_H2CTail ) % RingWords;
        HostWord( HP_C2HTail ) = CartWord( CP_C2HHead ) % RingWords;
        Pending.clear();
    }

    // -----------------------------------------------------------------------------

    bool ServerChannel::CartIsOpen()
    {
        return (CartWord( CP_Flags ) & 1) != 0;
    }

    // -----------------------------------------------------------------------------

    // True if the program opened the channel again since the last check (it
    // restarted, or came back after losing the host). Opening empties the rings
    // on the program's side by itself (each side moves the end it owns), and the
    // program may already have sent its first request, so the rings are NOT touched
    // here: only the words of an unfinished message from the old session are dropped
    bool ServerChannel::CheckCart()
    {
        uint32_t Session = CartWord( CP_Session );

        if( Session == CartSessionSeen )
          return false;

        CartSessionSeen = Session;
        Pending.clear();
        return true;
    }

    // -----------------------------------------------------------------------------

    // words that fit in the host to cart ring
    int32_t ServerChannel::FreeWords()
    {
        uint32_t Head = HostWord( HP_H2CHead );
        uint32_t Tail = CartWord( CP_H2CTail );

        // the program owns the tail: never trust it
        if( Head >= (uint32_t)RingWords || Tail >= (uint32_t)RingWords )
        {
            BadEvents++;
            return 0;
        }

        return (int32_t)((Tail + RingWords - Head - 1) % RingWords);
    }

    // -----------------------------------------------------------------------------

    // Appends words to the host to cart ring. All or nothing: false if it is full
    bool ServerChannel::Write( const uint32_t* Words, size_t Count )
    {
        if( (int64_t)Count > FreeWords() )
          return false;

        if( Count == 0 )
          return true;

        uint32_t Head = HostWord( HP_H2CHead );
        V32Word* Ring = &Card[ HostPart + DEV::ControlWords ];

        for( size_t i = 0; i < Count; i++ )
        {
            Ring[ Head ].AsBinary = Words[ i ];
            Head = (Head + 1) % RingWords;
        }

        // the index is written after the data
        HostWord( HP_H2CHead ) = Head;
        return true;
    }

    // -----------------------------------------------------------------------------

    bool ServerChannel::Send( uint32_t Opcode, const vector< uint32_t >& Payload )
    {
        if( (int32_t)Payload.size() > MessageMaxWords )
          return false;

        vector< uint32_t > Words;
        Words.reserve( Payload.size() + 2 );
        Words.push_back( (uint32_t)Payload.size() );
        Words.push_back( Opcode );
        Words.insert( Words.end(), Payload.begin(), Payload.end() );

        return Write( &Words[ 0 ], Words.size() );
    }

    // -----------------------------------------------------------------------------

    // Takes every word currently available in the cart to host ring
    size_t ServerChannel::Read( vector< uint32_t >& Words )
    {
        uint32_t Head = CartWord( CP_C2HHead );
        uint32_t Tail = HostWord( HP_C2HTail );

        // the program owns the head: never trust it
        if( Head >= (uint32_t)RingWords || Tail >= (uint32_t)RingWords )
        {
            BadEvents++;
            return 0;
        }

        size_t Count = (Head + RingWords - Tail) % RingWords;
        V32Word* Ring = &Card[ CartPart + DEV::ControlWords ];

        for( size_t i = 0; i < Count; i++ )
          Words.push_back( Ring[ (Tail + i) % RingWords ].AsBinary );

        HostWord( HP_C2HTail ) = Head;
        return Count;
    }

    // -----------------------------------------------------------------------------

    // Returns the complete messages received so far
    void ServerChannel::Receive( vector< Message >& Messages )
    {
        Read( Pending );
        size_t Position = 0;

        while( Pending.size() - Position >= 2 )
        {
            uint32_t Length = Pending[ Position ];

            // an impossible length means the stream is corrupt. There is no framing
            // to find the next message by, so what was received is dropped (the
            // program starts its next message at a boundary anyway)
            if( Length > (uint32_t)MessageMaxWords )
            {
                BadEvents++;
                Pending.clear();
                return;
            }

            if( Pending.size() - Position < Length + 2 )
              break;

            Message Received;
            Received.Opcode = Pending[ Position + 1 ];
            Received.Payload.assign( Pending.begin() + Position + 2, Pending.begin() + Position + 2 + Length );
            Messages.push_back( Received );
            Position += Length + 2;
        }

        Pending.erase( Pending.begin(), Pending.begin() + Position );
    }


    // =============================================================================
    //      STORAGE DEVICE (BLOCK CLASS)
    // =============================================================================
    // A file on this computer, served as a device of sectors (see storage.h).
    //
    // Word mode (default): every word is stored as 4 bytes, little endian, and a
    // sector is 128 words (512 bytes). Byte mode: every word is one byte (the program
    // gets 0..255, and only the low 8 bits of what it writes are kept), and a sector
    // is 512 words (512 bytes). Byte mode serves ordinary files, such as text files
    // or FAT images, as they are. A trailing part of the file that does not fill a
    // sector is not accessible.


    namespace
    {
        const uint32_t StorageOpRead = 1, StorageOpWrite = 2, StorageOpFlush = 3;
        const uint32_t StorageReply = 128;
        const uint32_t StatusOk = 0, StatusInvalid = 1, StatusRange = 2, StatusReadOnly = 3, StatusIO = 4;
        const uint32_t StorageFlagReadOnly = 1, StorageFlagBytes = 2;

        class StorageDevice: public Device
        {
            private:
                string Path;
                FILE* File;
                bool BytesMode, ReadOnly;
                uint32_t SectorWords;
                uint32_t WordSize;
                uint64_t SectorBytes;
                uint64_t Sectors;
                uint32_t MaxSectors;
                deque< Message > Outbox;        // replies the ring had no room for

            public:
                uint64_t SectorsRead, SectorsWritten;

            public:
                StorageDevice(): File( nullptr ), BytesMode( false ), ReadOnly( false ), SectorWords( 0 ), WordSize( 0 ),
                                 SectorBytes( 0 ), Sectors( 0 ), MaxSectors( 0 ), SectorsRead( 0 ), SectorsWritten( 0 ) {}

               ~StorageDevice()
                {
                    if( File )
                      fclose( File );
                }

                bool Open( const DeviceSpec& Spec, string& Error )
                {
                    Path = Spec.Path;
                    BytesMode = Spec.BytesMode;
                    ReadOnly = Spec.ReadOnly;
                    SectorWords = (uint32_t)(Spec.SectorWords? Spec.SectorWords : (BytesMode? 512 : 128));
                    WordSize = (BytesMode? 1 : 4);
                    SectorBytes = (uint64_t)SectorWords * WordSize;
                    MaxSectors = (uint32_t)((MessageMaxWords - 4) / SectorWords);

                    if( MaxSectors < 1 )
                    {
                        Error = "sector size is too big (at most " + to_string( MessageMaxWords - 4 ) + " words)";
                        return false;
                    }

                    // create the file if asked to (only when it does not exist yet)
                    if( Spec.CreateSectors > 0 )
                    {
                        FILE* Existing = fopen( Path.c_str(), "rb" );

                        if( Existing )
                            fclose( Existing );
                        else
                        {
                            FILE* Created = fopen( Path.c_str(), "wb" );
                            uint8_t Zero = 0;

                            if( !Created
                              || SeekFile( Created, (uint64_t)Spec.CreateSectors * SectorBytes - 1 ) != 0
                              || fwrite( &Zero, 1, 1, Created ) != 1 )
                            {
                                Error = "cannot create " + Path + ": " + strerror( errno );
                                if( Created ) fclose( Created );
                                return false;
                            }

                            fclose( Created );
                        }
                    }

                    File = fopen( Path.c_str(), ReadOnly? "rb" : "r+b" );

                    if( !File )
                    {
                        Error = "cannot open " + Path + ": " + strerror( errno );
                        return false;
                    }

                    // geometry
                    if( SeekFile( File, 0 ) != 0 || fseek( File, 0, SEEK_END ) != 0 )
                    {
                        Error = "cannot find the size of " + Path;
                        return false;
                    }

                  #if defined(_WIN32)
                    long long Size = _ftelli64( File );
                  #else
                    long long Size = (long long)ftello( File );
                  #endif

                    if( Size < 0 )
                    {
                        Error = "cannot find the size of " + Path;
                        return false;
                    }

                    Sectors = (uint64_t)Size / SectorBytes;
                    return true;
                }

                virtual uint32_t TypeWord() const { return MakeType( ClassBlock, 1 ); }

                virtual vector< uint32_t > Descriptor() const
                {
                    vector< uint32_t > Words;
                    Words.push_back( SectorWords );
                    Words.push_back( (uint32_t)min< uint64_t >( Sectors, 0xFFFFFFFFu ) );
                    Words.push_back( (ReadOnly? StorageFlagReadOnly : 0) | (BytesMode? StorageFlagBytes : 0) );
                    Words.push_back( MaxSectors );
                    return Words;
                }

                virtual string Describe() const
                {
                    string Text = "storage: " + Path + " (" + to_string( (unsigned long long)Sectors ) + " sectors of " + to_string( SectorWords ) + " words";
                    if( BytesMode ) Text += ", bytes";
                    if( ReadOnly ) Text += ", read only";
                    return Text + ")";
                }

                // Returns the reply to a request
                Message Handle( const Message& Request )
                {
                    Message Reply;
                    Reply.Opcode = StorageReply + Request.Opcode;
                    const vector< uint32_t >& Payload = Request.Payload;

                    if( Payload.empty() )
                    {
                        Reply.Payload.assign( 2, 0 );
                        Reply.Payload[ 1 ] = StatusInvalid;
                        return Reply;
                    }

                    uint32_t Tag = Payload[ 0 ];
                    Reply.Payload.push_back( Tag );

                    if( Request.Opcode == StorageOpFlush )
                    {
                        Reply.Payload.push_back( SyncFile( File )? StatusOk : StatusIO );
                        return Reply;
                    }

                    if( (Request.Opcode != StorageOpRead && Request.Opcode != StorageOpWrite) || Payload.size() < 3 )
                    {
                        Reply.Payload.push_back( StatusInvalid );
                        return Reply;
                    }

                    uint64_t Sector = Payload[ 1 ];
                    uint64_t Count = Payload[ 2 ];

                    if( Count < 1 || Count > MaxSectors )
                    {
                        Reply.Payload.push_back( StatusInvalid );
                        return Reply;
                    }

                    // (64 bit math: a huge sector number must not wrap around)
                    if( Sector + Count > Sectors )
                    {
                        Reply.Payload.push_back( StatusRange );
                        return Reply;
                    }

                    size_t NumberOfWords = (size_t)(Count * SectorWords);
                    vector< uint8_t > Bytes( (size_t)(Count * SectorBytes) );

                    if( SeekFile( File, Sector * SectorBytes ) != 0 )
                    {
                        Reply.Payload.push_back( StatusIO );
                        return Reply;
                    }

                    if( Request.Opcode == StorageOpRead )
                    {
                        if( Payload.size() != 3 )
                        {
                            Reply.Payload.push_back( StatusInvalid );
                            return Reply;
                        }

                        if( fread( &Bytes[ 0 ], 1, Bytes.size(), File ) != Bytes.size() )
                        {
                            clearerr( File );
                            Reply.Payload.push_back( StatusIO );
                            return Reply;
                        }

                        Reply.Payload.push_back( StatusOk );
                        Reply.Payload.reserve( 2 + NumberOfWords );

                        for( size_t i = 0; i < NumberOfWords; i++ )
                          Reply.Payload.push_back( BytesMode? Bytes[ i ] : GetWord( &Bytes[ i * 4 ] ) );

                        SectorsRead += Count;
                        return Reply;
                    }

                    // write
                    if( ReadOnly )
                    {
                        Reply.Payload.push_back( StatusReadOnly );
                        return Reply;
                    }

                    if( Payload.size() != 3 + NumberOfWords )
                    {
                        Reply.Payload.push_back( StatusInvalid );
                        return Reply;
                    }

                    for( size_t i = 0; i < NumberOfWords; i++ )
                    {
                        if( BytesMode )
                          Bytes[ i ] = (uint8_t)(Payload[ 3 + i ] & 0xFF);
                        else
                          PutWord( &Bytes[ i * 4 ], Payload[ 3 + i ] );
                    }

                    if( fwrite( &Bytes[ 0 ], 1, Bytes.size(), File ) != Bytes.size() || fflush( File ) != 0 )
                    {
                        clearerr( File );
                        Reply.Payload.push_back( StatusIO );
                        return Reply;
                    }

                    SectorsWritten += Count;
                    Reply.Payload.push_back( StatusOk );
                    return Reply;
                }

                virtual bool Poll( ServerChannel& Channel )
                {
                    bool Did = false;

                    // the program opened the channel again: forget replies that nobody awaits
                    if( Channel.CheckCart() )
                    {
                        Outbox.clear();
                        Did = true;
                    }

                    // replies that did not fit before
                    while( !Outbox.empty() )
                    {
                        if( !Channel.Send( Outbox.front().Opcode, Outbox.front().Payload ) )
                          return Did;

                        WordsToProgram += Outbox.front().Payload.size() + 2;
                        Outbox.pop_front();
                        Did = true;
                    }

                    vector< Message > Requests;
                    Channel.Receive( Requests );

                    for( size_t i = 0; i < Requests.size(); i++ )
                    {
                        WordsFromProgram += Requests[ i ].Payload.size() + 2;
                        Outbox.push_back( Handle( Requests[ i ] ) );
                        Did = true;
                    }

                    while( !Outbox.empty() )
                    {
                        if( !Channel.Send( Outbox.front().Opcode, Outbox.front().Payload ) )
                          break;

                        WordsToProgram += Outbox.front().Payload.size() + 2;
                        Outbox.pop_front();
                    }

                    return Did;
                }
        };
    }


    // =============================================================================
    //      SERIAL DEVICE (CHAR CLASS) AND ITS ENDPOINTS
    // =============================================================================
    // A stream of words in both directions. What is at the other end is up to the
    // endpoint: itself (echo), the terminal that started the emulator (stdio) or
    // whoever connects to a port on this computer (tcp).


    namespace
    {
        class Endpoint
        {
            public:
                virtual ~Endpoint() {}

                // words the program wrote
                virtual void Output( const vector< uint32_t >& Words ) = 0;

                // adds the words that are available for the program (at most MaxWords)
                virtual void Pump( deque< uint32_t >& Inbox, size_t MaxWords ) = 0;

                // no more words will come (once what was sent has been read)
                virtual bool HungUp() const = 0;

                // the program opened the channel again: a new conversation
                virtual void Restart() {}

                virtual string Describe() const = 0;
        };

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        class EchoEndpoint: public Endpoint
        {
            private:
                deque< uint32_t > Echoed;
                long Received;
                long HangUpAfter;

            public:
                EchoEndpoint( long HangUpAfterWords ): Received( 0 ), HangUpAfter( HangUpAfterWords ) {}

                virtual void Output( const vector< uint32_t >& Words )
                {
                    Received += Words.size();
                    Echoed.insert( Echoed.end(), Words.begin(), Words.end() );
                }

                virtual void Pump( deque< uint32_t >& Inbox, size_t MaxWords )
                {
                    while( !Echoed.empty() && Inbox.size() < MaxWords )
                    {
                        Inbox.push_back( Echoed.front() );
                        Echoed.pop_front();
                    }
                }

                virtual bool HungUp() const { return HangUpAfter >= 0 && Received >= HangUpAfter && Echoed.empty(); }
                virtual void Restart() { Received = 0; Echoed.clear(); }

                virtual string Describe() const
                {
                    return string( "serial: echo" ) + (HangUpAfter >= 0? ", hang up after " + to_string( HangUpAfter ) + " words" : "");
                }
        };

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        // Standard input and output of the process (a terminal when the emulator is
        // started from one). Only one channel can have it. Reading never blocks.
        bool StdioInUse = false;

        class StdioEndpoint: public Endpoint
        {
            private:
                bool EndOfInput;

            public:
                StdioEndpoint(): EndOfInput( false ) { StdioInUse = true; }
               ~StdioEndpoint() { StdioInUse = false; }

                virtual void Output( const vector< uint32_t >& Words )
                {
                    string Text;

                    for( size_t i = 0; i < Words.size(); i++ )
                      Text += (char)(Words[ i ] & 0xFF);

                    fwrite( Text.data(), 1, Text.size(), stdout );
                    fflush( stdout );
                }

                virtual void Pump( deque< uint32_t >& Inbox, size_t MaxWords )
                {
                  #if !defined(_WIN32)
                    if( EndOfInput || Inbox.size() >= MaxWords )
                      return;

                    struct pollfd Descriptor;
                    Descriptor.fd = 0;
                    Descriptor.events = POLLIN;
                    Descriptor.revents = 0;

                    if( poll( &Descriptor, 1, 0 ) <= 0 )
                      return;

                    unsigned char Buffer[ 256 ];
                    size_t Wanted = min< size_t >( sizeof( Buffer ), MaxWords - Inbox.size() );
                    ssize_t Count = read( 0, Buffer, Wanted );

                    if( Count > 0 )
                      for( ssize_t i = 0; i < Count; i++ )
                        Inbox.push_back( Buffer[ i ] );

                    else if( Count == 0 || (errno != EINTR && errno != EAGAIN) )
                      EndOfInput = true;
                  #endif
                }

                virtual bool HungUp() const { return EndOfInput; }
                virtual string Describe() const { return "serial: console (standard input and output)"; }
        };

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        #if defined(_WIN32)
          typedef SOCKET SocketHandle;
          const SocketHandle NoSocket = INVALID_SOCKET;
          #define CLOSE_SOCKET( s ) closesocket( s )
          #define SOCKET_WOULD_BLOCK() (WSAGetLastError() == WSAEWOULDBLOCK)
        #else
          typedef int SocketHandle;
          const SocketHandle NoSocket = -1;
          #define CLOSE_SOCKET( s ) close( s )
          #define SOCKET_WOULD_BLOCK() (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        #endif

        #ifndef MSG_NOSIGNAL
          #define MSG_NOSIGNAL 0
        #endif

        bool SetNonBlocking( SocketHandle Socket )
        {
          #if defined(_WIN32)
            u_long Mode = 1;
            return ioctlsocket( Socket, FIONBIO, &Mode ) == 0;
          #else
            int Flags = fcntl( Socket, F_GETFL, 0 );
            return Flags >= 0 && fcntl( Socket, F_SETFL, Flags | O_NONBLOCK ) == 0;
          #endif
        }

        void StartNetworking()
        {
          #if defined(_WIN32)
            static bool Started = false;

            if( !Started )
            {
                WSADATA Data;
                WSAStartup( MAKEWORD( 2, 2 ), &Data );
                Started = true;
            }
          #endif
        }

        void NoSigPipe( SocketHandle Socket )
        {
          #if defined(SO_NOSIGPIPE)
            int One = 1;
            setsockopt( Socket, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&One, sizeof( One ) );
          #else
            (void)Socket;
          #endif
        }

        // Listens on 127.0.0.1 only. One connection at a time: a new one replaces
        // the old. Output while nobody is connected is dropped. When the connection
        // ends the program sees a hang up, until the next one arrives.
        class TcpEndpoint: public Endpoint
        {
            private:
                int Port;
                SocketHandle Listener;
                SocketHandle Client;
                bool EverConnected;
                string Pending;                 // output the socket did not take yet

                static const size_t MaxPending = 64 * 1024;

            private:
                void CloseClient()
                {
                    if( Client != NoSocket )
                      CLOSE_SOCKET( Client );

                    Client = NoSocket;
                    Pending.clear();
                }

                void Flush()
                {
                    while( Client != NoSocket && !Pending.empty() )
                    {
                        int Sent = (int)send( Client, Pending.data(), (int)Pending.size(), MSG_NOSIGNAL );

                        if( Sent > 0 )
                            Pending.erase( 0, Sent );
                        else if( Sent < 0 && SOCKET_WOULD_BLOCK() )
                            return;
                        else
                            CloseClient();
                    }
                }

                void Accept()
                {
                    while( true )
                    {
                        SocketHandle New = accept( Listener, nullptr, nullptr );

                        if( New == NoSocket )
                          return;

                        if( !SetNonBlocking( New ) )
                        {
                            CLOSE_SOCKET( New );
                            continue;
                        }

                        NoSigPipe( New );
                        int One = 1;
                        setsockopt( New, IPPROTO_TCP, TCP_NODELAY, (const char*)&One, sizeof( One ) );

                        CloseClient();
                        Client = New;
                        EverConnected = true;
                    }
                }

            public:
                TcpEndpoint( int PortNumber ): Port( PortNumber ), Listener( NoSocket ), Client( NoSocket ), EverConnected( false ) {}

               ~TcpEndpoint()
                {
                    CloseClient();

                    if( Listener != NoSocket )
                      CLOSE_SOCKET( Listener );
                }

                bool Open( string& Error )
                {
                    StartNetworking();
                    Listener = socket( AF_INET, SOCK_STREAM, 0 );

                    if( Listener == NoSocket )
                    {
                        Error = "cannot create a socket";
                        return false;
                    }

                  #if !defined(_WIN32)
                    // (on Windows this option would let others share the port)
                    int One = 1;
                    setsockopt( Listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&One, sizeof( One ) );
                  #endif

                    struct sockaddr_in Address;
                    memset( &Address, 0, sizeof( Address ) );
                    Address.sin_family = AF_INET;
                    Address.sin_port = htons( (unsigned short)Port );
                    Address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );

                    if( bind( Listener, (struct sockaddr*)&Address, sizeof( Address ) ) != 0
                     || listen( Listener, 2 ) != 0
                     || !SetNonBlocking( Listener ) )
                    {
                        Error = "cannot listen on 127.0.0.1:" + to_string( Port ) + " (is the port in use?)";
                        CLOSE_SOCKET( Listener );
                        Listener = NoSocket;
                        return false;
                    }

                    return true;
                }

                virtual void Output( const vector< uint32_t >& Words )
                {
                    if( Client == NoSocket )
                      return;

                    for( size_t i = 0; i < Words.size() && Pending.size() < MaxPending; i++ )
                      Pending += (char)(Words[ i ] & 0xFF);

                    Flush();
                }

                virtual void Pump( deque< uint32_t >& Inbox, size_t MaxWords )
                {
                    Accept();
                    Flush();

                    if( Client == NoSocket || Inbox.size() >= MaxWords )
                      return;

                    unsigned char Buffer[ 512 ];
                    size_t Wanted = min< size_t >( sizeof( Buffer ), MaxWords - Inbox.size() );
                    int Count = (int)recv( Client, (char*)Buffer, (int)Wanted, 0 );

                    if( Count > 0 )
                      for( int i = 0; i < Count; i++ )
                        Inbox.push_back( Buffer[ i ] );

                    else if( Count == 0 || !SOCKET_WOULD_BLOCK() )
                      CloseClient();
                }

                virtual bool HungUp() const { return EverConnected && Client == NoSocket; }

                virtual string Describe() const
                {
                    return "serial: TCP 127.0.0.1:" + to_string( Port ) + (Client != NoSocket? " (connected)" : " (waiting for a connection)");
                }
        };

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        class SerialDevice: public Device
        {
            private:
                unique_ptr< Endpoint > Source;
                deque< uint32_t > Inbox;
                uint32_t CurrentStatus;

                static const size_t InboxLimit = 16 * 1024;

            public:
                SerialDevice( unique_ptr< Endpoint >& Where ): Source( move( Where ) ), CurrentStatus( StatusReady ) {}

                virtual uint32_t TypeWord() const { return MakeType( ClassChar, 1 ); }
                virtual string Describe() const { return Source->Describe(); }

                virtual bool Poll( ServerChannel& Channel )
                {
                    bool Did = false;

                    // the program opened the channel again: a new conversation
                    if( Channel.CheckCart() )
                    {
                        Inbox.clear();
                        Source->Restart();
                        Did = true;
                    }

                    // what the program wrote
                    vector< uint32_t > Words;

                    if( Channel.Read( Words ) > 0 )
                    {
                        WordsFromProgram += Words.size();
                        Source->Output( Words );
                        Did = true;
                    }

                    // what is for the program
                    Source->Pump( Inbox, InboxLimit );

                    if( !Inbox.empty() )
                    {
                        size_t Count = min< size_t >( Inbox.size(), (size_t)Channel.FreeWords() );

                        if( Count > 0 )
                        {
                            vector< uint32_t > Chunk( Inbox.begin(), Inbox.begin() + Count );

                            if( Channel.Write( &Chunk[ 0 ], Chunk.size() ) )
                            {
                                Inbox.erase( Inbox.begin(), Inbox.begin() + Count );
                                WordsToProgram += Count;
                                Did = true;
                            }
                        }
                    }

                    // hang up once the other end is gone and everything has been delivered
                    uint32_t Status = StatusReady | ((Source->HungUp() && Inbox.empty())? StatusHangUp : 0);

                    if( Status != CurrentStatus )
                    {
                        CurrentStatus = Status;
                        Channel.SetStatus( Status );
                        Did = true;
                    }

                    return Did;
                }
        };
    }


    // =============================================================================
    //      DEVICE SPECIFICATIONS
    // =============================================================================


    bool DeviceSpec::Parse( const string& Text, DeviceSpec& Result, string& Error )
    {
        DeviceSpec Spec;
        size_t Colon = Text.find( ':' );
        string Kind = (Colon == string::npos? Text : Text.substr( 0, Colon ));
        string Rest = (Colon == string::npos? "" : Text.substr( Colon + 1 ));

        if( Kind == "storage" )
        {
            Spec.Kind = Storage;
            vector< string > Parts = Split( Rest, ',' );

            // the options are taken from the right, what is left is the file name
            // (which can have commas)
            while( Parts.size() > 1 )
            {
                const string& Option = Parts.back();
                long Value = 0;

                if( Option == "bytes" )
                    Spec.BytesMode = true;
                else if( Option == "ro" )
                    Spec.ReadOnly = true;
                else if( StartsWith( Option, "sector=" ) )
                {
                    if( !ParseNumber( Option.substr( 7 ), 1, MessageMaxWords - 4, Value ) )
                    {
                        Error = "bad sector size in \"" + Option + "\"";
                        return false;
                    }

                    Spec.SectorWords = (int)Value;
                }
                else if( StartsWith( Option, "create=" ) )
                {
                    if( !ParseNumber( Option.substr( 7 ), 1, 0x7FFFFFFF, Value ) )
                    {
                        Error = "bad sector count in \"" + Option + "\"";
                        return false;
                    }

                    Spec.CreateSectors = Value;
                }
                else
                    break;

                Parts.pop_back();
            }

            for( size_t i = 0; i < Parts.size(); i++ )
              Spec.Path += (i > 0? "," : "") + Parts[ i ];

            if( Spec.Path.empty() )
            {
                Error = "storage needs a file name";
                return false;
            }
        }

        else if( Kind == "serial" )
        {
            vector< string > Parts = Split( Rest, ',' );
            long Value = 0;

            if( Parts[ 0 ] == "echo" )
            {
                Spec.Kind = SerialEcho;

                for( size_t i = 1; i < Parts.size(); i++ )
                {
                    if( !StartsWith( Parts[ i ], "hup=" ) || !ParseNumber( Parts[ i ].substr( 4 ), 0, 0x7FFFFFFF, Value ) )
                    {
                        Error = "unknown serial echo option \"" + Parts[ i ] + "\"";
                        return false;
                    }

                    Spec.HangUpAfter = Value;
                }
            }

            else if( Parts[ 0 ] == "stdio" && Parts.size() == 1 )
                Spec.Kind = SerialStdio;

            else if( StartsWith( Parts[ 0 ], "tcp=" ) && Parts.size() == 1 )
            {
                if( !ParseNumber( Parts[ 0 ].substr( 4 ), 1, 65535, Value ) )
                {
                    Error = "bad TCP port in \"" + Parts[ 0 ] + "\"";
                    return false;
                }

                Spec.Kind = SerialTcp;
                Spec.Port = (int)Value;
            }

            else
            {
                Error = "serial needs echo[,hup=N], stdio or tcp=PORT";
                return false;
            }
        }

        else
        {
            Error = "unknown device \"" + Text + "\"";
            return false;
        }

        Result = Spec;
        return true;
    }

    // -----------------------------------------------------------------------------

    string DeviceSpec::ToString() const
    {
        switch( Kind )
        {
            case Storage:
            {
                string Text = "storage:" + Path;
                if( BytesMode ) Text += ",bytes";
                if( ReadOnly ) Text += ",ro";
                if( SectorWords > 0 ) Text += ",sector=" + to_string( SectorWords );
                if( CreateSectors > 0 ) Text += ",create=" + to_string( CreateSectors );
                return Text;
            }

            case SerialEcho:
                return "serial:echo" + (HangUpAfter >= 0? ",hup=" + to_string( HangUpAfter ) : string());

            case SerialStdio:
                return "serial:stdio";

            case SerialTcp:
                return "serial:tcp=" + to_string( Port );

            default:
                return "";
        }
    }

    // -----------------------------------------------------------------------------

    unique_ptr< Device > CreateDevice( const DeviceSpec& Spec, string& Error )
    {
        switch( Spec.Kind )
        {
            case DeviceSpec::Storage:
            {
                unique_ptr< StorageDevice > Created( new StorageDevice );

                if( !Created->Open( Spec, Error ) )
                  return unique_ptr< Device >();

                return unique_ptr< Device >( Created.release() );
            }

            case DeviceSpec::SerialEcho:
            {
                unique_ptr< Endpoint > Where( new EchoEndpoint( Spec.HangUpAfter ) );
                return unique_ptr< Device >( new SerialDevice( Where ) );
            }

            case DeviceSpec::SerialStdio:
            {
              #if defined(_WIN32)
                Error = "the console is not available in Windows builds (use serial:tcp)";
                return unique_ptr< Device >();
              #else
                if( StdioInUse )
                {
                    Error = "the console is already used by another channel";
                    return unique_ptr< Device >();
                }

                unique_ptr< Endpoint > Where( new StdioEndpoint );
                return unique_ptr< Device >( new SerialDevice( Where ) );
              #endif
            }

            case DeviceSpec::SerialTcp:
            {
                unique_ptr< TcpEndpoint > Listening( new TcpEndpoint( Spec.Port ) );

                if( !Listening->Open( Error ) )
                  return unique_ptr< Device >();

                unique_ptr< Endpoint > Where( Listening.release() );
                return unique_ptr< Device >( new SerialDevice( Where ) );
            }

            default:
                Error = "no device";
                return unique_ptr< Device >();
        }
    }


    // =============================================================================
    //      THE SERVER
    // =============================================================================


    Server::Server(): ControlledByEnvironment( false ), Attached( false )
    {
    }

    // -----------------------------------------------------------------------------

    Server::~Server()
    {
        // devices release their files and ports; the card memory may be gone already
        for( int i = 0; i < DEV::Channels; i++ )
          Devices[ i ].reset();
    }

    // -----------------------------------------------------------------------------

    void Server::ApplyEnvironment()
    {
        const char* Setting = getenv( "V32DEV_SERVER" );

        if( Setting == nullptr )
          return;

        ControlledByEnvironment = true;
        Active.Enabled = (strcmp( Setting, "0" ) != 0);

        for( int i = 0; i < DEV::Channels; i++ )
        {
            string Name = "V32DEV_CH" + to_string( i );
            const char* Value = getenv( Name.c_str() );

            if( Value == nullptr || *Value == '\0' )
              continue;

            string Error;

            if( !SetSpec( i, Value, Error ) )
              Errors[ i ] = Name + ": " + Error;
        }
    }

    // -----------------------------------------------------------------------------

    string Server::GetSpec( int Channel ) const
    {
        if( Channel < 0 || Channel >= DEV::Channels )
          return "";

        return Active.Specs[ Channel ];
    }

    // -----------------------------------------------------------------------------

    // Sets what a channel serves ("" for nothing). If the server is attached the
    // channel changes at once. False if the spec is wrong or its device can't be
    // created (the spec is kept in the second case, and Error says why)
    bool Server::SetSpec( int Channel, const string& Spec, string& Error )
    {
        if( Channel < 0 || Channel >= DEV::Channels )
        {
            Error = "no such channel";
            return false;
        }

        DeviceSpec Parsed;

        if( !Spec.empty() )
          if( !DeviceSpec::Parse( Spec, Parsed, Error ) )
            return false;

        Active.Specs[ Channel ] = Parsed.ToString();

        if( Attached )
        {
            StopChannel( Channel );
            StartChannel( Channel );

            if( !Errors[ Channel ].empty() )
            {
                Error = Errors[ Channel ];
                return false;
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------------

    ServerConfig Server::GetStoredConfig() const
    {
        return ControlledByEnvironment? Stored : Active;
    }

    // -----------------------------------------------------------------------------

    void Server::SetStoredConfig( const ServerConfig& Config )
    {
        if( ControlledByEnvironment )
        {
            Stored = Config;
            return;
        }

        Active.Enabled = Config.Enabled;

        for( int i = 0; i < DEV::Channels; i++ )
        {
            string Error;
            SetSpec( i, Config.Specs[ i ], Error );
        }
    }

    // -----------------------------------------------------------------------------

    void Server::StartChannel( int Channel )
    {
        Errors[ Channel ].clear();
        Devices[ Channel ].reset();

        if( Active.Specs[ Channel ].empty() )
          return;

        DeviceSpec Spec;
        string Error;

        if( !DeviceSpec::Parse( Active.Specs[ Channel ], Spec, Error ) )
        {
            Errors[ Channel ] = Error;
            return;
        }

        unique_ptr< Device > Created = CreateDevice( Spec, Error );

        if( !Created )
        {
            Errors[ Channel ] = Error;
            return;
        }

        Devices[ Channel ] = move( Created );
        Channels[ Channel ].Announce( Devices[ Channel ]->TypeWord(), Devices[ Channel ]->Descriptor(), StatusReady );
    }

    // -----------------------------------------------------------------------------

    void Server::StopChannel( int Channel )
    {
        if( Devices[ Channel ] )
        {
            Channels[ Channel ].Withdraw();
            Devices[ Channel ].reset();
        }
    }

    // -----------------------------------------------------------------------------

    void Server::Attach( V32Word* CardMemory )
    {
        if( Attached )
          Detach();

        Attached = true;

        for( int i = 0; i < DEV::Channels; i++ )
        {
            Channels[ i ].Bind( CardMemory, i );
            StartChannel( i );
        }
    }

    // -----------------------------------------------------------------------------

    void Server::Detach()
    {
        if( !Attached )
          return;

        for( int i = 0; i < DEV::Channels; i++ )
          StopChannel( i );

        Attached = false;
    }

    // -----------------------------------------------------------------------------

    void Server::Poll()
    {
        if( !Attached )
          return;

        for( int i = 0; i < DEV::Channels; i++ )
        {
            if( !Devices[ i ] )
              continue;

            // the program tells the host is alive when this changes
            Channels[ i ].Beat();
            Devices[ i ]->Poll( Channels[ i ] );
        }
    }

    // -----------------------------------------------------------------------------

    ChannelInfo Server::GetChannelInfo( int Channel )
    {
        ChannelInfo Info;

        if( Channel < 0 || Channel >= DEV::Channels )
          return Info;

        Info.Spec = Active.Specs[ Channel ];
        Info.Error = Errors[ Channel ];

        if( Devices[ Channel ] )
        {
            Info.Serving = true;
            Info.Description = Devices[ Channel ]->Describe();
            Info.ProgramOpen = Channels[ Channel ].CartIsOpen();
            Info.WordsFromProgram = Devices[ Channel ]->WordsFromProgram;
            Info.WordsToProgram = Devices[ Channel ]->WordsToProgram;
        }

        return Info;
    }
}
}
