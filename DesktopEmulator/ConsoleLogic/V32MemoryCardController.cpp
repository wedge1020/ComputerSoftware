// *****************************************************************************
    // include console logic headers
    #include "V32MemoryCardController.hpp"
// *****************************************************************************


namespace V32
{
    // =============================================================================
    //      V32 MEMORY CARD CONTROLLER: OWN METHODS
    // =============================================================================


    V32MemoryCardController::V32MemoryCardController()
    {
        PendingSave = false;
        MailboxActive = false;
        Heartbeat = 0;
        DirtyPages.assign( DEV::NumberOfPages, 0 );
    }

    // -----------------------------------------------------------------------------

    V32MemoryCardController::~V32MemoryCardController()
    {
        // ensure we always close the files
        if( LinkedFile.is_open() )
          LinkedFile.close();

        if( PollFile.is_open() )
          PollFile.close();
    }

    // -----------------------------------------------------------------------------

    bool V32MemoryCardController::ReadPort( int32_t LocalPort, V32Word& Result )
    {
        // check range
        if( LocalPort > MEM_LastPort )
          return false;

        // provide value
        if( LocalPort == (int32_t)MEM_LocalPorts::Connected )
          Result.AsInteger = (MemorySize > 0? 1 : 0);

        return true;
    }

    // -----------------------------------------------------------------------------

    bool V32MemoryCardController::WritePort( int32_t LocalPort, V32Word Value )
    {
        // all memory card ports are read-only!
        return false;
    }


    // =============================================================================
    //      V32 MEMORY CARD CONTROLLER: METHODS OVERRIDEN FROM RAM
    // =============================================================================


    bool V32MemoryCardController::WriteAddress( int32_t LocalAddress, V32Word Value )
    {
        // check that the normal RAM write is successful
        if( !V32RAM::WriteAddress( LocalAddress, Value ) )
          return false;

        // remember which page was modified
        if( LocalAddress >= 0 && (size_t)(LocalAddress >> DEV::PageShift) < DirtyPages.size() )
          DirtyPages[ LocalAddress >> DEV::PageShift ] = 1;

        // data is now pending to save
        PendingSave = true;

        return true;
    }


    // =============================================================================
    //      V32 MEMORY CARD CONTROLLER: V32DEV MAILBOX SUPPORT
    // =============================================================================


    void V32MemoryCardController::ClearDirtyPages()
    {
        DirtyPages.assign( DEV::NumberOfPages, 0 );
    }

    // -----------------------------------------------------------------------------

    // tells if the card contents have a v32dev header that matches this build's layout
    bool V32MemoryCardController::HeaderIsValid()
    {
        if( MemorySize != DEV::CardWords || Memory.size() != (size_t)DEV::CardWords )
          return false;

        const V32Word* Header = &Memory[ DEV::RegionStart ];

        return Header[ DEV::H_Magic        ].AsInteger == DEV::Magic
            && Header[ DEV::H_Version      ].AsInteger == DEV::Version
            && Header[ DEV::H_RegionWords  ].AsInteger == DEV::RegionWords
            && Header[ DEV::H_Channels     ].AsInteger == DEV::Channels
            && Header[ DEV::H_PartWords    ].AsInteger == DEV::PartWords
            && Header[ DEV::H_ControlWords ].AsInteger == DEV::ControlWords;
    }

    // -----------------------------------------------------------------------------

    // Decides what has to be written to the card file for the pages modified
    // since the last save. Without a mailbox these are just the modified pages.
    // With an active mailbox:
    //   - the header and the whole host-owned area are never written: the
    //     external host owns those words, and our copy may be stale
    //   - the control words of each cart-owned part ("commit words": ring
    //     indices and flags) are written in a second phase, after all data, so
    //     an external reader that sees an updated index also sees its data
    void V32MemoryCardController::PlanSave( std::vector< DEV::SaveSegment >& Segments )
    {
        Segments.clear();

        // words that are not saved together with the rest of the dirty pages
        struct Range { int32_t First; int32_t End; };
        std::vector< Range > Excluded;

        if( MailboxActive )
        {
            // header and host-owned area are contiguous
            Range HeaderAndHostArea = { DEV::RegionStart, DEV::CartAreaStart };
            Excluded.push_back( HeaderAndHostArea );

            for( int c = 0; c < DEV::Channels; c++ )
            {
                int32_t PartStart = DEV::CartAreaStart + c * DEV::PartWords;
                Range CommitWords = { PartStart, PartStart + DEV::ControlWords };
                Excluded.push_back( CommitWords );
            }
        }

        // PHASE 1: all modified words except the excluded ones
        int Page = 0;

        while( Page < DEV::NumberOfPages )
        {
            if( !DirtyPages[ Page ] )
            {
                Page++;
                continue;
            }

            // find the end of this run of modified pages
            int FirstPage = Page;

            while( Page < DEV::NumberOfPages && DirtyPages[ Page ] )
              Page++;

            int32_t RunStart = FirstPage * DEV::PageWords;
            int32_t RunEnd = Page * DEV::PageWords;

            // emit the run, skipping any excluded ranges inside it
            // (these are sorted and don't overlap)
            int32_t Cursor = RunStart;

            for( size_t i = 0; i < Excluded.size(); i++ )
            {
                if( Excluded[ i ].End <= Cursor )
                  continue;

                if( Excluded[ i ].First >= RunEnd )
                  break;

                if( Excluded[ i ].First > Cursor )
                {
                    DEV::SaveSegment Segment = { Cursor, Excluded[ i ].First - Cursor, 1 };
                    Segments.push_back( Segment );
                }

                if( Excluded[ i ].End > Cursor )
                  Cursor = Excluded[ i ].End;
            }

            if( Cursor < RunEnd )
            {
                DEV::SaveSegment Segment = { Cursor, RunEnd - Cursor, 1 };
                Segments.push_back( Segment );
            }
        }

        // PHASE 2: commit words of every cart-owned part that was touched
        if( !MailboxActive )
          return;

        for( int c = 0; c < DEV::Channels; c++ )
        {
            int32_t PartStart = DEV::CartAreaStart + c * DEV::PartWords;
            int32_t PartEnd = PartStart + DEV::PartWords;
            bool Touched = false;

            for( int p = (PartStart >> DEV::PageShift); p <= ((PartEnd - 1) >> DEV::PageShift); p++ )
              if( DirtyPages[ p ] )
                Touched = true;

            if( Touched )
            {
                DEV::SaveSegment Segment = { PartStart, DEV::ControlWords, 2 };
                Segments.push_back( Segment );
            }
        }
    }
}
