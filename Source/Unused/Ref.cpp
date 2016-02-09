// a Ref smart pointer for possible later use with Rogue

#include <iostream>
#include <cstdio>
using namespace std;

struct Data
{
  int reference_count;

  Data() : reference_count(0) { printf("+Creating data\n"); }
  ~Data() { printf( "-Deleting data\n" ); }
};

template <class DataType>
struct Ref
{
  DataType* pointer;

  Ref<DataType>() : pointer(0) { printf("ref 1\n"); }

  Ref<DataType>( DataType* pointer )
  {
    printf( "ref2\n" );
    if ((this->pointer = pointer)) ++pointer->reference_count;
  }

  Ref<DataType>( const Ref<DataType>& existing )
  {
    printf( "ref3\n" );
    if ((pointer = existing.pointer))
    {
      ++pointer->reference_count;
    }
  }

  ~Ref<DataType>()
  {
    if (pointer && !(--pointer->reference_count) ) delete pointer;
  }

  Ref<DataType>& operator=( const Ref<DataType>& other )
  {
    printf( "ref4\n" );
    if (other.pointer) ++other.pointer->reference_count;
    if (pointer && !(--pointer->reference_count)) delete pointer;
    pointer = other.pointer;
    return *this;
  }

  Ref<DataType>& operator=( DataType* new_pointer )
  {
    printf( "ref5\n" );
    if (new_pointer) ++new_pointer->reference_count;
    if (pointer && !(--pointer->reference_count)) delete pointer;
    pointer = new_pointer;
    return *this;
  }

  DataType* operator->()
  {
    return pointer;
  }
};

Ref<Data> new_ref()
{
  return new Data();
}

void print_ref( Ref<Data> ref )
{
  printf( "data ref count: %d\n", ref->reference_count );
}

int main()
{
  printf( "Ready\n" );


  printf( "Done\n" );
  return 0;
}

