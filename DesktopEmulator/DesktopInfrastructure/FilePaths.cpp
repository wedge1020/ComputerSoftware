// *****************************************************************************
    // include infrastructure headers
    #include "FilePaths.hpp"
    
    // include C/C++ headers
    #include <iostream>         // [ C++ STL ] I/O Streams
    #include <fstream>          // [ C++ STL ] File streams
    #include <algorithm>        // [ C++ STL ] Algorithms
    #include <sys/stat.h>       // [ ANSI C ] File status
    
    // these includes are only for directory listings
    #if defined(WINDOWS_OS)
      #define WIN32_LEAN_AND_MEAN
      #define NOMINMAX
      #include <windows.h>      // [ WinAPI ] Windows API
    #else
      #include <dirent.h>       // [ POSIX ] Directory entries
    #endif
    
    // these includes are only for unicode conversions
    #if defined(WINDOWS_OS)
      #include <locale>         // [ C++ STL ] Locales
      #include <codecvt>        // [ C++ STL ] Encoding conversions
    #endif
    
    // declare used namespaces
    using namespace std;
// *****************************************************************************


// =============================================================================
//      PATH SEPARATOR CHARACTER
// =============================================================================


// this is dependent on the host operating system
#if defined(WINDOWS_OS)
    const char PathSeparator = '\\';
#else
    const char PathSeparator = '/';
#endif


// =============================================================================
//      FILE PATH MANIPULATION FUNCTIONS
// =============================================================================


string GetFileExtension( const string& FilePath )
{
    size_t DotPosition = FilePath.rfind( '.' );
    
    if( DotPosition == string::npos )
      return "";
    
    return FilePath.substr( DotPosition+1 );
}

// -----------------------------------------------------------------------------

// returns extension without the dot
string GetFileWithoutExtension( const string& FilePath )
{
    size_t DotPosition = FilePath.rfind( '.' );
    
    if( DotPosition == string::npos )
      return "";
    
    return FilePath.substr( 0, DotPosition );
}

// -----------------------------------------------------------------------------

string ReplaceFileExtension( const string& FilePath, const string& NewExtension )
{
    string OriginalExtension = GetFileExtension( FilePath );
    string PathWithoutExtension = FilePath.substr( 0, FilePath.length() - OriginalExtension.length() );
    return PathWithoutExtension + NewExtension;
}

// -----------------------------------------------------------------------------

string GetPathDirectory( const string& FilePath )
{
    size_t SlashPosition = FilePath.rfind( PathSeparator );
    
    // careful, if the path is empty (i.e. current folder)
    // we need to return a dot or else paths will be wrong
    if( SlashPosition == string::npos )
      return string(".") + PathSeparator;
    
    return FilePath.substr( 0, SlashPosition+1 );
}

// -----------------------------------------------------------------------------

string GetPathFileName( const string& FilePath )
{
    size_t SlashPosition = FilePath.rfind( PathSeparator );
    
    if( SlashPosition == string::npos )
      return FilePath;
    
    if( FilePath.size() < (SlashPosition+2) )
      return "";
    
    return FilePath.substr( SlashPosition+1, FilePath.size()-1 );
}


// =============================================================================
//      CHECKING VALIDITY OF PATHS
// =============================================================================


bool IsFileNameValid( const string& FileName )
{
    if( FileName.find('\"') != string::npos )  return false;
    if( FileName.find('\\') != string::npos )  return false;
    if( FileName.find('/')  != string::npos )  return false;
    if( FileName.find('|')  != string::npos )  return false;
    if( FileName.find('<')  != string::npos )  return false;
    if( FileName.find('>')  != string::npos )  return false;
    if( FileName.find(':')  != string::npos )  return false;
    if( FileName.find('?')  != string::npos )  return false;
    if( FileName.find('*')  != string::npos )  return false;
    
    return true;
}

// -----------------------------------------------------------------------------

bool FileExists( const string& FilePath )
{
    #if defined(WINDOWS_OS)
    
      struct _stat Info;
      wstring FilePathUTF16 = ToUTF16( FilePath );
      
      if( _wstat( FilePathUTF16.c_str(), &Info ) != 0 )
        return false;
      
      return !(Info.st_mode & _S_IFDIR);
      
    #else
        
      struct stat Info;
      
      if( stat( FilePath.c_str(), &Info ) != 0 )
        return false;
      
      return !(Info.st_mode & S_IFDIR);
      
    #endif
}

// -----------------------------------------------------------------------------

bool DirectoryExists( const string& Path )
{
    #if defined(WINDOWS_OS)
    
      struct _stat Info;
      wstring PathUTF16 = ToUTF16( Path );
      
      if( _wstat( PathUTF16.c_str(), &Info ) != 0 )
        return false;
      
      return (Info.st_mode & _S_IFDIR);
      
    #else
        
      struct stat Info;
      
      if( stat( Path.c_str(), &Info ) != 0 )
        return false;
      
      return (Info.st_mode & S_IFDIR);
      
    #endif
}

// -----------------------------------------------------------------------------

// auxiliary function for the directory listing: lowercase copy of a string
static string ToLowerCase( string Text )
{
    for( char& Character: Text )
      if( Character >= 'A' && Character <= 'Z' )
        Character += ('a' - 'A');
    
    return Text;
}

// -----------------------------------------------------------------------------

vector< string > ListFilesWithExtension( const string& Directory, const string& Extension )
{
    vector< string > FileNames;
    string WantedExtension = ToLowerCase( Extension );
    
    // make sure that we can append file names to the directory
    string DirectoryWithSlash = Directory;
    
    if( DirectoryWithSlash.empty() || DirectoryWithSlash.back() != PathSeparator )
      DirectoryWithSlash += PathSeparator;
    
    #if defined(WINDOWS_OS)
    
      WIN32_FIND_DATAW FoundData;
      wstring SearchPattern = ToUTF16( DirectoryWithSlash + "*" );
      HANDLE SearchHandle = FindFirstFileW( SearchPattern.c_str(), &FoundData );
      
      if( SearchHandle == INVALID_HANDLE_VALUE )
        return FileNames;
      
      do
      {
          // skip folders
          if( FoundData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            continue;
          
          string FileName = ToUTF8( FoundData.cFileName );
          
          if( ToLowerCase( GetFileExtension( FileName ) ) == WantedExtension )
            FileNames.push_back( FileName );
      }
      while( FindNextFileW( SearchHandle, &FoundData ) );
      
      FindClose( SearchHandle );
      
    #else
      
      DIR* DirectoryHandle = opendir( DirectoryWithSlash.c_str() );
      
      if( !DirectoryHandle )
        return FileNames;
      
      while( dirent* Entry = readdir( DirectoryHandle ) )
      {
          string FileName = Entry->d_name;
          
          // skip folders (this also drops "." and "..")
          if( !FileExists( DirectoryWithSlash + FileName ) )
            continue;
          
          if( ToLowerCase( GetFileExtension( FileName ) ) == WantedExtension )
            FileNames.push_back( FileName );
      }
      
      closedir( DirectoryHandle );
      
    #endif
    
    sort( FileNames.begin(), FileNames.end() );
    return FileNames;
}


// =============================================================================
//      UNICODE STRING CONVERSIONS UTF-8 <-> UTF-16
// =============================================================================


#if defined(WINDOWS_OS)

  wstring ToUTF16( const string& TextUTF8 )
  {
      wstring_convert< codecvt_utf8_utf16< wchar_t > > Converter;
      return Converter.from_bytes( TextUTF8 );
  }
  
  // -----------------------------------------------------------------------------
  
  string ToUTF8( const wstring& TextUTF16 )
  {
      wstring_convert< codecvt_utf8_utf16< wchar_t > > Converter;
      return Converter.to_bytes( TextUTF16 );
  }

#endif


// =============================================================================
//      WRAPPERS FOR PROPER FILE ACCESS ON UNICODE PATHS
// =============================================================================


void OpenInputFile( ifstream& InputFile, const string& FilePathUTF8, ios_base::openmode Mode )
{
    #if defined(WINDOWS_OS)
      wstring FilePathUTF16 = ToUTF16( FilePathUTF8 );
      InputFile.open( FilePathUTF16.c_str(), Mode );
    #else
      InputFile.open( FilePathUTF8.c_str(), Mode );
    #endif
}

// -----------------------------------------------------------------------------

void OpenOutputFile( ofstream& OutputFile, const string& FilePathUTF8, ios_base::openmode Mode )
{
    #if defined(WINDOWS_OS)
      wstring FilePathUTF16 = ToUTF16( FilePathUTF8 );
      OutputFile.open( FilePathUTF16.c_str(), Mode );
    #else
      OutputFile.open( FilePathUTF8.c_str(), Mode );
    #endif
}

// -----------------------------------------------------------------------------

FILE* OpenInputFile( const std::string& FilePathUTF8 )
{
    #if defined(WINDOWS_OS)
      wstring FilePathUTF16 = ToUTF16( FilePathUTF8 );
      return _wfopen( FilePathUTF16.c_str(), L"rb" );
    #else
      return fopen( FilePathUTF8.c_str(), "rb" );
    #endif
}

// -----------------------------------------------------------------------------

FILE* OpenOutputFile( const std::string& FilePathUTF8 )
{
    #if defined(WINDOWS_OS)
      wstring FilePathUTF16 = ToUTF16( FilePathUTF8 );
      return _wfopen( FilePathUTF16.c_str(), L"wb" );
    #else
      return fopen( FilePathUTF8.c_str(), "wb" );
    #endif
}
