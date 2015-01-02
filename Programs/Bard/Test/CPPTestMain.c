#include <stdio.h>
#include <stdlib.h>
#include "Bard.h"

int main( int argc, const char* argv[] )
{
  BardVM* vm = BardVM_create();
#if defined(BARD_VM)
  BardVM_launch( vm, argc, argv );
  BardVM_update_until_finished( vm, 60 );
#endif
  BardVM_free( vm );

  return 0;
}

