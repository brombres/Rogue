#ifndef BARD_LIST_H
#define BARD_LIST_H
//=============================================================================
//  BardList.h
//=============================================================================
BardObject*  BardObjectList_create( BardVM* vm, int initial_capacity );
BardObject*  BardXCRealList_create( BardVM* vm, int initial_capacity );
BardObject*  BardXCIntegerList_create( BardVM* vm, int initial_capacity );
BardObject*  BardXCCharacterList_create( BardVM* vm, int initial_capacity );
BardObject*  BardXCByteList_create( BardVM* vm, int initial_capacity );
BardObject*  BardXCLogicalList_create( BardVM* vm, int initial_capacity );

void BardList_ensure_capacity_for_additional( BardObject* list, int additional_capacity );
void BardList_ensure_capacity( BardObject* list, int required_capacity );

#endif // BARD_LIST_H
