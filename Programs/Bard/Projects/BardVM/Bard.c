#include "Bard.h"
#include <stdio.h>
#include <stdlib.h>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

int main( int argc, const char* argv[] )
{
  const char* filename;
  BardVM* vm = BardVM_create();

  BardStandardLibrary_configure( vm );

  if (argc < 2)
  {
    printf( "USAGE\n\n  bard filename.bc [args]\n" );
    return 1;
  }

  filename = argv[1];

  if (BardVM_load_bc_file(vm,filename))
  {
    int i;

    //printf("Loaded %s\n", filename );
    for (i=2; i<argc; ++i)
    {
      BardVM_add_command_line_argument( vm, argv[i] );
    }

    BardVM_create_main_object( vm );

    double start_time = BardUtil_time();
    BardVM_collect_garbage( vm );
    double elapsed = BardUtil_time() - start_time;
    //printf( "GC 1: %lf seconds, %d ms\n", elapsed, (int)(elapsed * 1000.0));

    start_time = BardUtil_time();
    BardVM_collect_garbage( vm );
    elapsed = BardUtil_time() - start_time;
    //printf( "GC 2: %lf seconds, %d ms\n", elapsed, (int)(elapsed * 1000.0));

    //printf( "REFERENCED OBJECTS\n" );
    /*
    BardObject* referenced_objects = BardVM_collect_referenced_objects( vm );
    int referenced_object_count = 0;
    while (referenced_objects)
    {
      ++referenced_object_count;
      //printf( "%s\n", referenced_objects->type->name );
      referenced_objects->size = ~referenced_objects->size;
      referenced_objects = referenced_objects->next_reference;
    }
    printf( "%d referenced objects\n", referenced_object_count );
    */

    while (BardVM_update(vm))
    {
      //printf("updating\n");
      usleep( 1000000/60 );
    }
  }
  else
  {
    printf("Failed to load %s\n", filename );
  }

  // Print Cycle Count
  //{
  //  int i;
  //  int cycle_threshold = 0;
  //  for (i=0; i<vm->method_count; ++i)
  //  {
  //    BardMethod* m = vm->methods[i];
  //    if (m->cycle_count > cycle_threshold) cycle_threshold = m->cycle_count;
  //  }

  //  cycle_threshold /= 5;
  //  for (i=0; i<vm->method_count; ++i)
  //  {
  //    BardMethod* m = vm->methods[i];
  //    if (m->cycle_count >= cycle_threshold)
  //    {
  //      printf( "%010d,%s::%s\n", m->cycle_count, m->type_context->name, BardMethod_signature( m ) );
  //    }
  //  }
  //}

  BardVM_destroy( vm );

  return 0;
}

