//=============================================================================
//  RogueETCReader.c
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  ETCReader Object
//-----------------------------------------------------------------------------
RogueETCReader* RogueETCReader_create_with_file( RogueVM* vm, RogueString* filepath )
{
  RogueETCReader* reader = RogueVMObject_create( vm, sizeof(RogueETCReader) );
  reader->vm = vm;
  reader->filepath = RogueVM_consolidate_string( vm, filepath );

  {
    RogueInteger size;
    FILE* fp = fopen( RogueString_to_c_string(filepath), "rb" );
    if ( !fp )
    {
      vm->error_filepath = filepath;
      ROGUE_THROW( vm, "Unable to open file for reading." );
    }

    fseek( fp, 0, SEEK_END );
    size = (RogueInteger) ftell( fp );
    fseek( fp, 0, SEEK_SET );

    reader->data = RogueVMArray_create( vm, 1, size );
    fread( reader->data->bytes, 1, size, fp );
    fclose( fp );

    reader->count = size;
  }

  return reader;
}

RogueLogical RogueETCReader_has_another( RogueETCReader* THIS )
{
  return (THIS->position < THIS->count);
}

void RogueETCReader_load( RogueETCReader* THIS )
{
  printf( "-------------------------------------------------------------------------------\n" );
  printf( "%c", RogueETCReader_read_byte(THIS) );
  printf( "%c", RogueETCReader_read_byte(THIS) );
  printf( "%c", RogueETCReader_read_byte(THIS) );
  printf( "\nVersion %d\n", RogueETCReader_read_integer_x(THIS) );
  printf( "-------------------------------------------------------------------------------\n" );

  {
    RogueInteger class_definition_count = RogueETCReader_read_integer_x( THIS );
    printf( "Class definition count: %d\n", class_definition_count );
  }

  {
    RogueCmdStatementList* immediate_commands;

    immediate_commands = RogueETCReader_load_statement_list( THIS );
    printf( "Immediate statement count: %d\n", immediate_commands->statements->count );
  }
}

RogueInteger RogueETCReader_read_byte( RogueETCReader* THIS )
{
  int index;
  if ((index = ++THIS->position) > THIS->count)
  {
    --THIS->position;
    return -1;
  }
  return THIS->data->bytes[index-1];
}

RogueInteger RogueETCReader_read_integer_x( RogueETCReader* THIS )
{
  RogueInteger b1 = RogueETCReader_read_byte( THIS );
  if (b1 <= 0x7F)
  {
    // Use directly as 8-bit signed number (positive)
    // 0ddddddd
    return b1;
  }
  else if ((b1 & 0xc0) == 0xc0)
  {
    // Use directly as 8-bit signed number (negative)
    // 11dddddd
    return (char) b1;
  }
  else
  {
    switch (b1 & 0xf0)
    {
      case 0x80:
      {
        // 1000cccc  dddddddd - 12-bit SIGNED value in 2 bytes
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCReader_read_byte( THIS );
        if (result <= 0x7FF) return result;  // 0 or positive
        return result - 0x1000;  // 0x800..0xFFF -> -0x800...-1
      }
      
      case 0x81:
      {
        // 1001bbbb  cccccccc  dddddddd - 20-bit SIGNED value in 3 bytes
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCReader_read_byte( THIS );
        result |= RogueETCReader_read_byte( THIS );
        if (result <= 0x7FFFF) return result;  // 0 or positive
        return result - 0x100000;  // 0x80000..0xFFFFF -> -0x80000...-1
      }

      case 0x82:
      {
        // 1010aaaa bbbbbbbb  cccccccc  dddddddd - 28-bit SIGNED value in 4 bytes
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCReader_read_byte( THIS );
        result |= RogueETCReader_read_byte( THIS );
        result |= RogueETCReader_read_byte( THIS );
        if (result <= 0x7FFFFFF) return result;  // 0 or positive
        return result - 0x10000000;  // 0x8000000..0xFFFFFFF -> -0x8000000...-1
      }

      default:
      {
        // 1010xxxx aaaaaaaa bbbbbbbb  cccccccc  dddddddd - 32-bit signed value in 5 bytes
        RogueInteger result = RogueETCReader_read_byte( THIS );
        result |= RogueETCReader_read_byte( THIS );
        result |= RogueETCReader_read_byte( THIS );
        return (result | RogueETCReader_read_byte(THIS));
      }
    }
  }
}

RogueCmdStatementList*  RogueETCReader_load_statement_list( RogueETCReader* THIS )
{
  RogueCmdStatementList* list = RogueCmdStatementList_create( THIS->vm );

  RogueInteger count = RogueETCReader_read_integer_x( THIS );

  // TODO: read statements

  return list;
}
