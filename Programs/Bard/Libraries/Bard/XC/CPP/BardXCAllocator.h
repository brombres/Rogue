#ifndef BARD_XC_ALLOCATOR_H
#define BARD_XC_ALLOCATOR_H

//=============================================================================
//  BardXCAllocator global functions
//
//  BardXCAllocator_allocate_permanent( size:int ) : void*
//    Returns memory that is automatically freed at program termination.
//    This is a very efficient page-based allocator with very little overhead 
//    in either memory or time.
//
//  BardXCAllocator_allocate( size:int ) : void*
//    Quickly returns an allocation of the desired size.  It is designed to
//    be especially efficient with small allocations (16, 32, 48, 64, 128, 192,
//    or 256 bytes) while large allocations are acquired using a general
//    malloc() call.
//
//  BardXCAllocator_free( data:void*, size:int )
//    Frees an allocation earlier created with BardXCAllocator_allocate().
//    Small allocations (16, 32, 48, 64, 128, 192, or 256 bytes) are recycled
//    into allocation pools while larger objects are freed with system free().
//
//=============================================================================
void* BardXCAllocator_allocate_permanent( int bytes );
void* BardXCAllocator_allocate( int bytes );
void  BardXCAllocator_free( void* ptr, int bytes );

#endif // BARD_XC_ALLOCATOR_H
