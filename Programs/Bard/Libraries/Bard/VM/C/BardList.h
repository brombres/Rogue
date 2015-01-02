#ifndef BARD_LIST_H
#define BARD_LIST_H
//=============================================================================
//  BardList.h
//=============================================================================
BardObject*  BardObjectList_create( BardVM* vm, int initial_capacity );
BardObject*  BardRealList_create( BardVM* vm, int initial_capacity );
BardObject*  BardIntegerList_create( BardVM* vm, int initial_capacity );
BardObject*  BardCharacterList_create( BardVM* vm, int initial_capacity );
BardObject*  BardByteList_create( BardVM* vm, int initial_capacity );
BardObject*  BardLogicalList_create( BardVM* vm, int initial_capacity );

void BardList_ensure_capacity_for_additional( BardObject* list, int additional_capacity );
void BardList_ensure_capacity( BardObject* list, int required_capacity );

#endif // BARD_LIST_H
