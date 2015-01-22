#pragma once

//=============================================================================
//  RogueSystemList.h
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.18 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================
#include <string.h>

template <class DataType>
struct RogueSystemList
{
  DataType* data;
  int       count;
  int       capacity;

  RogueSystemList( int capacity=0 ) : count(0)
  {
    this->capacity = capacity;
    if (capacity)
    {
      data = new DataType[capacity];
      memset( data, 0, sizeof(DataType)*capacity );
    }
    else data = 0;
    count = 0;
  }

  ~RogueSystemList()
  {
    if (data) 
    {
      delete data;
      data = 0;
      count = 0;
      capacity = 0;
    }
  }

  RogueSystemList<DataType>& add( DataType value )
  {
    if (count == capacity) ensure_capacity( capacity ? capacity*2 : 10 );
    data[count++] = value;
    return *this;
  }

  RogueSystemList<DataType>& clear()
  {
    count = 0;
    return *this;
  }

  RogueSystemList<DataType>& ensure_capacity( int c )
  {
    if (capacity >= c) return *this;

    if (capacity > 0)
    {
      int double_capacity = (capacity << 1);
      if (double_capacity > c) c = double_capacity;
    }

    capacity = c;

    int bytes = sizeof(DataType) * capacity;

    if ( !data )
    {
      data = new DataType[capacity];
      memset( data, 0, sizeof(DataType) * capacity );
    }
    else
    {
      int old_bytes = sizeof(DataType) * count;

      DataType* new_data = new DataType[capacity];
      memset( ((char*)new_data) + old_bytes, 0, bytes - old_bytes );
      memcpy( new_data, data, old_bytes );

      delete data;
      data = new_data;
    }

    return *this;
  }

  inline DataType& operator[]( int index ) { return data[index]; }

  bool remove( DataType value )
  {
    for (int i=count-1; i>=0; --i)
    {
      if (data[i] == value)
      {
        remove_at(i);
        return true;
      }
    }
    return false;
  }

  DataType remove_at( int index )
  {
    if (count == 1 || index == count-1)
    {
      return data[--count];
    }
    else
    {
      DataType result = data[index];
      --count;
      while (index < count)
      {
        data[index] = data[index+1];
        ++index;
      }
      return result;
    }
  }

  DataType remove_first()
  {
    return remove_at(0);
  }

  DataType remove_last()
  {
    return remove_at( count - 1 );
  }
};

