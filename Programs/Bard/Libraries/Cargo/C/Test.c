#include "Cargo.h"

#include <stdio.h>

typedef struct XY
{
  double x;
  double y;
} XY;

XY XY_create( double x, double y )
{
  XY result = {x,y};
  return result;
}

#define LIST_TYPE XY

void print( LIST_TYPE value )
{
  printf( "(%.2lf,%.2lf)\n", value.x, value.y );
}

void test1()
{
  CargoList* list = CargoList_create( LIST_TYPE );
  printf( "data:%d\n",     (list->data != 0) );
  printf( "size:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  CargoList_add( list, LIST_TYPE, XY_create(1,2) );
  printf( "\nsize:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  CargoList_add( list, LIST_TYPE, XY_create(3,4) );
  printf( "\nsize:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  CargoList_add( list, LIST_TYPE, XY_create(5,6) );
  printf( "\nsize:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  CargoList_add( list, LIST_TYPE, XY_create(7,8) );
  printf( "\nsize:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  CargoList_add( list, LIST_TYPE, XY_create(9,10) );
  printf( "\nsize:%d\n",     list->element_size );
  printf( "count:%d\n",    list->count );
  printf( "capacity:%d\n", list->capacity );

  printf( "list[0] = " );
  print( CargoList_get( list, LIST_TYPE, 0 ) );
  printf( "list[1] = " );
  print( CargoList_get( list, LIST_TYPE, 1 ) );
  printf( "list[4] = " );
  print( CargoList_get( list, LIST_TYPE, 4 ) );
  printf( "\n" );
}

void test2()
{
  CargoList* list = CargoList_create( int );
  CargoList_add( list, int, 4 );
  CargoList_add( list, int, 5 );
  CargoList_add( list, int, 6 );
  CargoList_add( list, int, 7 );
  CargoList_add( list, int, 8 );

  CargoList_discard_at( list, 2 );
  CargoList_discard_at( list, 0 );
  CargoList_discard_at( list, list->count-1 );
  CargoList_discard_at( list, 0 );
  CargoList_discard_at( list, 0 );

  for (int i=0; i<list->count; ++i)
  {
    printf( "%d ", CargoList_get(list,int,i) );
  }
  printf( "\n" );
}

void test3()
{
  CargoTable* table;
  table = CargoTable_create( char*, 16, 0, 0 );
  CargoTable_retire( table );
}

int main()
{
  //test1();
  test2();
  test3();
  return 0;
}
