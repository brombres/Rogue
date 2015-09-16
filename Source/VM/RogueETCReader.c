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
  RogueVM* vm = THIS->vm;

  RogueETCReader_read_byte(THIS);  // 'E'
  RogueETCReader_read_byte(THIS);  // 'T'
  RogueETCReader_read_byte(THIS);  // 'C'
  RogueETCReader_read_integer_x(THIS);  // Version 1

  {
    RogueInteger class_definition_count = RogueETCReader_read_integer_x( THIS );
    printf( "Class definition count: %d\n", class_definition_count );
  }

  {
    RogueInteger i;
    RogueInteger count = RogueETCReader_read_integer_x( THIS );

    RogueVMList_reserve( vm->immediate_commands->statements, count );

    for (i=0; i<count; ++i)
    {
      RogueCmd* cmd = RogueETCReader_load_statement( THIS );
      RogueVMList_add( vm->immediate_commands->statements, cmd );
    }
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

RogueString* RogueETCReader_read_string( RogueETCReader* THIS )
{
  RogueInteger i;
  RogueInteger count = RogueETCReader_read_integer_x( THIS );
  RogueString* value = RogueString_create( THIS->vm, count );
  RogueCharacter* dest = value->characters - 1;
  for (i=0; i<count; ++i)
  {
    *(++dest) = (RogueCharacter) RogueETCReader_read_integer_x( THIS );
  }

  return RogueVM_consolidate_string( THIS->vm, RogueString_update_hash_code(value) );
}

RogueCmdList*  RogueETCReader_load_statement_list( RogueETCReader* THIS )
{
  RogueInteger count = RogueETCReader_read_integer_x( THIS );
  RogueCmdList* list = RogueCmdList_create( THIS->vm, count );
  int i;

  for (i=0; i<count; ++i)
  {
    RogueCmd* cmd = RogueETCReader_load_statement( THIS );
    RogueVMList_add( list->statements, cmd );
  }

  return list;
}

void* RogueETCReader_load_statement( RogueETCReader* THIS )
{
  RogueVM*     vm     = THIS->vm;
  RogueInteger opcode = RogueETCReader_read_integer_x( THIS );
  switch (opcode)
  {
    case ROGUE_CMD_LOG_VALUE:
    {
      RogueCmd* operand = RogueETCReader_load_expression( THIS );
      RogueType* type = RogueCmd_type( operand );

      if (type == vm->type_Integer)     opcode = ROGUE_CMD_LOG_INTEGER;
      else if (type == vm->type_String) opcode = ROGUE_CMD_LOG_STRING;
      else RogueETCReader_throw_type_error( THIS, type, opcode );

      return RogueCmdUnaryOp_create( vm, opcode, operand );
    }

    default:
    {
      RogueCmd* expression = RogueETCReader_load_expression( THIS );
      return expression;
    }
  }
}

void* RogueETCReader_load_expression( RogueETCReader* THIS )
{
  RogueVM*     vm     = THIS->vm;
  RogueInteger opcode = RogueETCReader_read_integer_x( THIS );
  switch (opcode)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return RogueCmdLiteralInteger_create( vm, RogueETCReader_read_integer_x(THIS) );

    case ROGUE_CMD_LITERAL_STRING:
      return RogueCmdLiteralString_create( vm, RogueETCReader_read_string(THIS) );

    case ROGUE_CMD_ADD:
    {
      RogueCmd* left  = RogueETCReader_load_expression( THIS );
      RogueCmd* right = RogueETCReader_load_expression( THIS );
      return RogueCmdBinaryOp_create( vm, ROGUE_CMD_ADD, left, right );
    }

    default:
    {
      RogueVM* vm = THIS->vm;
      vm->error_filepath = THIS->filepath;
      RogueStringBuilder_print_c_string( &vm->error_message_builder, "Invalid expression opcode: " );
      RogueStringBuilder_print_integer( &vm->error_message_builder, opcode );
      ROGUE_THROW( vm, "." );
    }
  }
}

void RogueETCReader_throw_type_error( RogueETCReader* THIS, RogueType* type, RogueCmdType opcode )
{
  RogueVM* vm = THIS->vm;
  vm->error_filepath = THIS->filepath;
  RogueStringBuilder_print_c_string( &vm->error_message_builder, "Invalid type " );
  if (type)
  {
    RogueStringBuilder_print_c_string( &vm->error_message_builder, type->name );
  }
  RogueStringBuilder_print_c_string( &vm->error_message_builder, "for opcode " );
  RogueStringBuilder_print_integer( &vm->error_message_builder, opcode );
  ROGUE_THROW( vm, "." );
}

