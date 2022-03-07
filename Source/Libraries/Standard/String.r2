class String
  PROPERTIES
    byte_count      : Int32  # in UTF-8 bytes
    character_count : Int32  # in whole characters
    cursor_offset   : Int32  # in bytes
    cursor_index    : Int32  # in characters
    hash_code       : Int32
    must_free       : Logical
    nativeC  "char* utf8;"
    nativeC  "char  buffer[78];"

  NATIVE
    nativeCHeader @|RogueString* RogueString__create( const char* cstring, int count );

    nativeCCode  @|RogueString* RogueString__create( const char* cstring, int count )
                  |{
                  |  if (count == -1) count = (int) strlen(cstring);
                  |  ROGUE_CREATE_OBJECT( RogueString, result );
                  |  return result;
                  |}
endClass

