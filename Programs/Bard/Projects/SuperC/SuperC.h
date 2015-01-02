#ifndef SUPER_C_H
#define SUPER_C_H

#ifdef __cplusplus
#  define SUPER_C_BEGIN_HEADER extern "C" {
#  define SUPER_C_END_HEADER   }
#else
#  define SUPER_C_BEGIN_HEADER
#  define SUPER_C_END_HEADER
#endif

SUPER_C_BEGIN_HEADER

#define SUPER_C_INITIAL_ALLOCATION_LIST_CAPACITY 1

//=============================================================================
//  SuperCAllocation
//=============================================================================
typedef struct SuperCAllocation
{
  struct SuperCAllocation* next_allocation;
  struct SuperCAllocation* previous_allocation;
  double data[1];
} SuperCAllocation;


//=============================================================================
//  SuperC
//=============================================================================
typedef struct SuperC
{
  SuperCAllocation* allocations;
} SuperC;

SuperC* SuperC_create();
SuperC* SuperC_destroy( SuperC* super_c );

void*   SuperC_allocate( SuperC* super_c, int size );
void*   SuperC_free( SuperC* super_c, void* data );

char*   SuperC_clone_c_string( SuperC* super_c, const char* st );

SUPER_C_END_HEADER

#endif // SUPER_C_H
