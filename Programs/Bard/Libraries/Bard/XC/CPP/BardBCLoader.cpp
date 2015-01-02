#include "BardXC.h"

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>


int BardBCLoader_load_bc_from_reader( BardVM* vm, BardFileReader* reader )
{
  BARD_TRY(vm)
    if ( !BardFileReader_consume(reader,"BARDCODE") )
    {
      BardVM_throw_error( vm, 1, "Not a BARDCODE file." );
    }

    {
      int bc_version = BardFileReader_read_integer( reader );
      if (bc_version != BARD_VM_CURRENT_BC_VERSION)
      {
        char st[80];
        sprintf( st, "Unsupported BC version: %d", bc_version );
        BardVM_throw_error( vm, 1, st );
      }
    }

    // Load code chunk
    {
      int i;
      vm->code_size = BardBCLoader_load_count( vm, reader );

      vm->code = (BardXCInteger*) BARD_ALLOCATE( sizeof(BardXCInteger) * vm->code_size );
      for (i=0; i<vm->code_size; ++i)
      {
        vm->code[i] = BardFileReader_read_integer( reader );
      }

      if (vm->code[0] != 0) BardVM_throw_error( vm, 1, "code[0] must be HALT." );
    }

    // Read identifiers
    {
      int i;
      vm->identifier_count = BardFileReader_read_integer( reader );
      vm->identifiers = (char**) BARD_ALLOCATE( sizeof(char*) * vm->identifier_count );
      for (i=0; i<vm->identifier_count; ++i)
      {
        char* id = BardFileReader_read_id( reader );
        vm->identifiers[i] = id;
      }
    }

    // Read type declarations
    {
      int i;
      vm->type_count = BardFileReader_read_integer( reader );
      vm->types = (BardType**) BARD_ALLOCATE( sizeof(BardType*) * vm->type_count );

      // Allocate placeholder objects for every type so that "forward references" to types
      // work during loading.
      for (i=0; i<vm->type_count; ++i)
      {
        vm->types[i] = BardType_create( vm );
      }

      for (i=0; i<vm->type_count; ++i)
      {
        BardType* type = vm->types[i];
        char*     name = BardBCLoader_load_indexed_id( vm, reader );

        type = BardType_init( type, name );

        // Read attribute flags
        type->attributes = BardFileReader_read_integer( reader );

        // Load attribute tags
        type->tag_count = BardBCLoader_load_count( vm, reader );

        if (type->tag_count > 0)
        {
          int t;
          type->tags = (char**) BARD_ALLOCATE( sizeof(char*) * type->tag_count );
          for (t=0; t<type->tag_count; ++t)
          {
            type->tags[t] = BardBCLoader_load_indexed_id( vm, reader );
          }
        }

        // Load base types
        type->base_type_count = BardBCLoader_load_count( vm, reader );
        if (type->base_type_count > 0)
        {
          int b;
          type->base_types = (BardType**) BARD_ALLOCATE( sizeof(BardType*) * type->base_type_count );
          for (b=0; b<type->base_type_count; ++b)
          {
            type->base_types[b] = BardBCLoader_load_indexed_type( vm, reader );
          }
        }

        type->element_type = BardBCLoader_load_indexed_type( vm, reader );

        // Load settings list
        type->settings_count = BardBCLoader_load_count( vm, reader );
        if (type->settings_count > 0)
        {
          int s;
          type->settings = (BardProperty*) BARD_ALLOCATE( sizeof(BardProperty) * type->settings_count );
          for (s=0; s<type->settings_count; ++s)
          {
            BardProperty* property = type->settings + s;
            property->name = BardBCLoader_load_indexed_id( vm, reader );
            property->type = BardBCLoader_load_indexed_type( vm, reader );
          }
        }

        // Load property list
        type->property_count = BardBCLoader_load_count( vm, reader );
        if (type->property_count > 0)
        {
          int p;
          type->properties = (BardProperty*) BARD_ALLOCATE( sizeof(BardProperty) * type->property_count );
          for (p=0; p<type->property_count; ++p)
          {
            BardProperty* property = type->properties + p;
            property->name = BardBCLoader_load_indexed_id( vm, reader );
            property->type = BardBCLoader_load_indexed_type( vm, reader );
          }
        }
      }
    }

    vm->main_class = BardBCLoader_load_indexed_type( vm, reader );

    BardVM_set_sizes_and_offsets( vm );

    // Read method definitions
    {
      int i;
      vm->method_count = BardFileReader_read_integer( reader );
      vm->methods = (BardMethod**) BARD_ALLOCATE( sizeof(BardMethod*) * vm->method_count );
      for (i=0; i<vm->method_count; ++i)
      {
        BardType* type_context;
        BardType* return_type;
        char*     name;
        int       parameter_count;
        int       local_count;

        type_context = BardBCLoader_load_indexed_type( vm, reader );

        name = BardBCLoader_load_indexed_id( vm, reader );

        parameter_count = BardBCLoader_load_count( vm, reader );
        return_type = BardBCLoader_load_indexed_type( vm, reader );
        local_count = BardBCLoader_load_count( vm, reader );

        BardMethod* m = BardMethod_create( i, type_context, name, parameter_count, return_type, local_count );
        vm->methods[i] = m;

        {
          int p;
          for (p=0; p<local_count; ++p)
          {
            BardType* local_type = BardBCLoader_load_indexed_type( vm, reader );
            m->local_types[p] = local_type;

            //if (p > 0) printf(",");
            //printf("%s",local_type->name);
          }
        }

        m->ip_start = BardFileReader_read_integer(reader);
        m->ip_limit = BardFileReader_read_integer(reader);
        m->ip = vm->code + m->ip_start - 1;
        m->line_info_index = BardFileReader_read_integer( reader );

        // Load method attributes

        // Flags
        m->attributes = BardFileReader_read_integer( reader );

        // Load attribute tags
        m->tag_count = BardBCLoader_load_count( vm, reader );

        if (m->tag_count > 0)
        {
          int t;
          m->tags = (char**) BARD_ALLOCATE( sizeof(char*) * m->tag_count );
          for (t=0; t<m->tag_count; ++t)
          {
            m->tags[t] = BardBCLoader_load_indexed_id( vm, reader );
          }
        }

        // Load exception handler table
        m->exception_handler_count = BardBCLoader_load_count( vm, reader );
        if (m->exception_handler_count)
        {
          int e;
          int size = sizeof(BardVMExceptionHandlerInfo) * m->exception_handler_count;
          m->exception_handlers = (BardVMExceptionHandlerInfo*) BARD_ALLOCATE( size );
          for (e=0; e<m->exception_handler_count; ++e)
          {
            BardVMExceptionHandlerInfo* info = m->exception_handlers + e;
            info->ip_start = vm->code + BardBCLoader_load_count( vm, reader );
            info->ip_limit = vm->code + BardBCLoader_load_count( vm, reader );
            info->catch_count = BardBCLoader_load_count( vm, reader );
            if (info->catch_count)
            {
              int c;
              info->catches = (BardVMExceptionHandlerCatchInfo*) BARD_ALLOCATE( sizeof(BardVMExceptionHandlerCatchInfo) * info->catch_count );
              for (c=0; c<info->catch_count; ++c)
              {
                BardVMExceptionHandlerCatchInfo* cur_catch = info->catches + c;
                cur_catch->catch_type = BardBCLoader_load_indexed_type( vm, reader );

                cur_catch->handler_ip = vm->code + (BardBCLoader_load_count( vm, reader ) - 1);
                cur_catch->local_slot_index = BardFileReader_read_integer( reader );
              }
            }
            else
            {
              info->catches = NULL;
            }
          }
        }
      }
    }

    // Read per-type method tables
    {
      int i;
      int type_count = BardFileReader_read_integer( reader );
      if (type_count != vm->type_count)
      {
        BardVM_throw_error( vm, 1, "Mismatched type count for type declarations vs. type definitions." );
      }

      for (i=0; i<type_count; ++i)
      {
        BardType* type = vm->types[i];

        // Load method list
        type->method_count = BardBCLoader_load_count( vm, reader );
        if (type->method_count > 0)
        {
          int m;
          type->methods = (BardMethod**) BARD_ALLOCATE( sizeof(BardMethod*) * type->method_count );
          for (m=0; m<type->method_count; ++m)
          {
            type->methods[m] = BardBCLoader_load_indexed_method( vm, reader );
          }
        }

        // Load aspect call tables
        {
          type->aspect_count = BardBCLoader_load_count( vm, reader );
          if (type->aspect_count > 0)
          {
            int a;

            type->aspect_types = (BardType**) BARD_ALLOCATE( sizeof(BardType*) * type->aspect_count );
            type->aspect_call_tables = (BardMethod***) BARD_ALLOCATE( sizeof(BardMethod**) * type->aspect_count );

            for (a=0; a<type->aspect_count; ++a)
            {
              int m_count, i;
              type->aspect_types[a] = BardBCLoader_load_indexed_type( vm, reader );
              m_count = BardBCLoader_load_count( vm, reader );
              type->aspect_call_tables[a] = (BardMethod**) BARD_ALLOCATE( sizeof(BardMethod*) * m_count );

              for (i=0; i<m_count; ++i)
              {
                type->aspect_call_tables[a][i] = BardBCLoader_load_indexed_method( vm, reader );
              }
            }
          }
        }
      }
    }

    // Load Real literals
    {
      int i;
      vm->literal_real_count = BardBCLoader_load_count( vm, reader );

      if (vm->literal_real_count)
      {
        vm->literal_reals = (BardXCReal*)BARD_ALLOCATE( sizeof(BardXCReal) * vm->literal_real_count);

        for(i = 0; i < vm->literal_real_count; ++i)
        {
          BardXCInteger upper32_of_real = (BardXCInteger)BardFileReader_read_integer( reader );
          BardXCInteger lower32_of_real = (BardXCInteger)BardFileReader_read_integer( reader );
          BardXCInt64 real_as_int64 = ((BardXCInt64)upper32_of_real << 32) | (lower32_of_real & 0xFFFFFFFF);

          BardXCReal* real = (BardXCReal*)&real_as_int64;
          vm->literal_reals[i] = *real;
        }
      }
    }

    // Load filenames used
    {
      int i;
      vm->filename_count = BardFileReader_read_integer( reader );
      vm->filenames = (char**) BARD_ALLOCATE( sizeof(char*) * vm->filename_count );
      for (i=0; i<vm->filename_count; ++i)
      {
        char* id = BardFileReader_read_id( reader );
        vm->filenames[i] = id;
      }
    }

    // Load line info
    {
      vm->line_info_count = BardBCLoader_load_count( vm, reader );
      if (vm->line_info_count)
      {
        int i;
        int count = vm->line_info_count;
        vm->line_info = (unsigned char*) BARD_ALLOCATE( vm->line_info_count );
        for (i=0; i<count; ++i)
        {
          vm->line_info[i] = BardFileReader_read_byte( reader );
        }
      }
    }

    BardVM_organize( vm );

    // Load string literals
    {
      int i;
      vm->literal_string_count = BardBCLoader_load_count( vm, reader );

      if (vm->literal_string_count)
      {
        vm->literal_strings = (BardObject**) BARD_ALLOCATE( sizeof(BardObject*) * vm->literal_string_count );

        for (i=0; i<vm->literal_string_count; ++i)
        {
          int string_count = BardBCLoader_load_count( vm, reader );
          BardString* obj = BardString_create_with_size( vm, string_count );
          obj->count = string_count;
          BardXCCharacter* dest = obj->characters - 1;
          while (--string_count >= 0)
          {
            *(++dest) = BardFileReader_read_integer( reader );
          }
          vm->literal_strings[i] = (BardObject*) BardString_set_hash_code( obj );
        }
      }
    }

    BardVM_create_singletons( vm );

    return 1;

  BARD_CATCH_ERROR
    printf( "ERROR: %s\n", vm->error_message );
    return 0;

  BARD_END_TRY(vm)
}

int BardBCLoader_load_count( BardVM* vm, BardFileReader* reader )
{
  int result = BardFileReader_read_integer( reader );
  if (result < 0) BardVM_throw_error( vm, 1, "Negative element count." );
  return result;
}

char* BardBCLoader_load_indexed_id( BardVM* vm, BardFileReader* reader )
{
  int id_index = BardFileReader_read_integer( reader );
  if (id_index < 0 || id_index >= vm->identifier_count)
  {
    BardVM_throw_error( vm, 1, "ID index out of bounds." );
  }

  return (char*) vm->identifiers[ id_index ];
}

BardType* BardBCLoader_load_indexed_type( BardVM* vm, BardFileReader* reader )
{
  int type_index = BardFileReader_read_integer( reader );
  if (type_index == -1) return NULL;

  if (type_index < 0 || type_index >= vm->type_count)
  {
    BardVM_throw_error( vm, 1, "Type index out of bounds." );
  }

  return (BardType*) vm->types[ type_index ];
}

BardMethod* BardBCLoader_load_indexed_method( struct BardVM* vm, BardFileReader* reader )
{
  int method_index = BardFileReader_read_integer( reader );
  if (method_index == -1) return NULL;

  if (method_index < 0 || method_index >= vm->method_count)
  {
    BardVM_throw_error( vm, 1, "Method index out of bounds." );
  }

  return (BardMethod*) vm->methods[ method_index ];
}
