//=============================================================================
//  RogueETCLoader.c
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  ETCReader Object
//-----------------------------------------------------------------------------
RogueETCLoader* RogueETCLoader_create_with_file( RogueVM* vm, RogueString* filepath )
{
  RogueETCLoader* reader = RogueVMObject_create( vm, sizeof(RogueETCLoader) );
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

RogueLogical RogueETCLoader_has_another( RogueETCLoader* THIS )
{
  return (THIS->position < THIS->count);
}

void RogueETCLoader_load( RogueETCLoader* THIS )
{
  RogueVM* vm = THIS->vm;

  vm->is_resolved = 0;

  THIS->type_list = RogueVMList_create( vm, 50, RogueVMTraceType );
  THIS->strings = RogueVMList_create( vm, 100, 0 );

  RogueETCLoader_read_byte(THIS);  // 'E'
  RogueETCLoader_read_byte(THIS);  // 'T'
  RogueETCLoader_read_byte(THIS);  // 'C'
  RogueETCLoader_read_integer_x(THIS);  // Version 1

  {
    RogueInteger class_definition_count = RogueETCLoader_read_integer_x( THIS );
    printf( "Class definition count: %d\n", class_definition_count );
  }

  {
    RogueInteger i;
    RogueInteger count = RogueETCLoader_read_integer_x( THIS );

    RogueVMList_reserve( vm->immediate_commands->statements, count );

    for (i=0; i<count; ++i)
    {
      RogueCmd* cmd = RogueETCLoader_load_statement( THIS );
      RogueVMList_add( vm->immediate_commands->statements, cmd );
    }
  }
}

RogueInteger RogueETCLoader_read_byte( RogueETCLoader* THIS )
{
  int index;
  if ((index = ++THIS->position) > THIS->count)
  {
    --THIS->position;
    return -1;
  }
  return THIS->data->bytes[index-1];
}

RogueInteger RogueETCLoader_read_integer( RogueETCLoader* THIS )
{
  RogueInteger result = RogueETCLoader_read_byte( THIS ) << 24;
  result |= RogueETCLoader_read_byte( THIS ) << 16;
  result |= RogueETCLoader_read_byte( THIS ) << 8;
  return (result | RogueETCLoader_read_byte( THIS ));
}

RogueInteger RogueETCLoader_read_integer_x( RogueETCLoader* THIS )
{
  RogueInteger b1 = RogueETCLoader_read_byte( THIS );
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
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCLoader_read_byte( THIS );
        if (result <= 0x7FF) return result;  // 0 or positive
        return result - 0x1000;  // 0x800..0xFFF -> -0x800...-1
      }
      
      case 0x81:
      {
        // 1001bbbb  cccccccc  dddddddd - 20-bit SIGNED value in 3 bytes
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCLoader_read_byte( THIS );
        result |= RogueETCLoader_read_byte( THIS );
        if (result <= 0x7FFFF) return result;  // 0 or positive
        return result - 0x100000;  // 0x80000..0xFFFFF -> -0x80000...-1
      }

      case 0x82:
      {
        // 1010aaaa bbbbbbbb  cccccccc  dddddddd - 28-bit SIGNED value in 4 bytes
        RogueInteger result = ((b1 & 0xF) << 8) | RogueETCLoader_read_byte( THIS );
        result |= RogueETCLoader_read_byte( THIS );
        result |= RogueETCLoader_read_byte( THIS );
        if (result <= 0x7FFFFFF) return result;  // 0 or positive
        return result - 0x10000000;  // 0x8000000..0xFFFFFFF -> -0x8000000...-1
      }

      default:
      {
        // 1010xxxx aaaaaaaa bbbbbbbb  cccccccc  dddddddd - 32-bit signed value in 5 bytes
        RogueInteger result = RogueETCLoader_read_byte( THIS );
        result |= RogueETCLoader_read_byte( THIS );
        result |= RogueETCLoader_read_byte( THIS );
        return (result | RogueETCLoader_read_byte(THIS));
      }
    }
  }
}

RogueReal RogueETCLoader_read_real( RogueETCLoader* THIS )
{
  RogueLong result = RogueETCLoader_read_integer( THIS );
  result = (result << 32LL) | (((RogueLong) RogueETCLoader_read_integer(THIS)) & 0xFFFFffff);
  return *((RogueReal*)&result);
}

RogueString* RogueETCLoader_read_string( RogueETCLoader* THIS )
{
  RogueInteger index = RogueETCLoader_read_integer_x( THIS );
  if (index >= 0 && index < THIS->strings->count)
  {
    // A string with this index has already been defined
    return THIS->strings->array->objects[ index ];
  }
  else if (index == THIS->strings->count)
  {
    // First occurrence of this string index; definition follows
    RogueInteger i;
    RogueInteger count = RogueETCLoader_read_integer_x( THIS );
    RogueString* value = RogueString_create( THIS->vm, count );
    RogueCharacter* dest = value->characters - 1;

    for (i=0; i<count; ++i)
    {
      *(++dest) = (RogueCharacter) RogueETCLoader_read_integer_x( THIS );
    }

    value = RogueVM_consolidate_string( THIS->vm, RogueString_update_hash_code(value) );
    RogueVMList_add( THIS->strings, value );

    return value;
  }
  else
  {
    ROGUE_THROW( THIS->vm, "Literal string index out of bounds." );
  }
}

RogueCmdList*  RogueETCLoader_load_statement_list( RogueETCLoader* THIS )
{
  RogueInteger count = RogueETCLoader_read_integer_x( THIS );
  RogueCmdList* list = RogueCmdList_create( THIS->vm, count );
  int i;

  for (i=0; i<count; ++i)
  {
    RogueCmd* cmd = RogueETCLoader_load_statement( THIS );
    RogueVMList_add( list->statements, cmd );
  }

  return list;
}

void* RogueETCLoader_load_statement( RogueETCLoader* THIS )
{
  RogueVM*     vm     = THIS->vm;
  RogueInteger opcode = RogueETCLoader_read_integer_x( THIS );
  switch (opcode)
  {
    case ROGUE_CMD_LOG_VALUE:
    {
      RogueCmd* operand = RogueETCLoader_load_expression( THIS );
      return RogueCmdUnaryOp_create( vm, ROGUE_CMD_LOG_VALUE, operand );
    }

    default:
    {
      RogueCmd* expression = RogueETCLoader_load_expression( THIS );
      return expression;
    }
  }
}

void* RogueETCLoader_load_expression( RogueETCLoader* THIS )
{
  RogueVM*     vm     = THIS->vm;
  RogueInteger opcode = RogueETCLoader_read_integer_x( THIS );
  switch (opcode)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return RogueCmdLiteralInteger_create( vm, RogueETCLoader_read_integer_x(THIS) );

    case ROGUE_CMD_LITERAL_REAL:
      return RogueCmdLiteralReal_create( vm, RogueETCLoader_read_real(THIS) );

    case ROGUE_CMD_LITERAL_STRING:
      return RogueCmdLiteralString_create( vm, RogueETCLoader_read_string(THIS) );

    case ROGUE_CMD_ADD:
    {
      RogueCmd* left  = RogueETCLoader_load_expression( THIS );
      RogueCmd* right = RogueETCLoader_load_expression( THIS );
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

void RogueETCLoader_throw_type_error( RogueETCLoader* THIS, RogueType* type, RogueCmdType opcode )
{
  RogueVM* vm = THIS->vm;
  vm->error_filepath = THIS->filepath;
  RogueStringBuilder_print_c_string( &vm->error_message_builder, "Invalid type " );
  if (type)
  {
    RogueStringBuilder_print_c_string( &vm->error_message_builder, type->name );
  }
  RogueStringBuilder_print_c_string( &vm->error_message_builder, " for opcode " );
  RogueStringBuilder_print_integer( &vm->error_message_builder, opcode );
  ROGUE_THROW( vm, "." );
}

