NativeArray
library CString
  HEADERS
    <string.h>

  FUNCTIONS
    function strlen( st:@Byte[] )->Integer [coerce]

    function strcpy( dest:Byte[], src:@Byte[] )->Byte[]
    
endLibrary

function main( argc:Integer, argv:[[@Byte]] )->Integer
  printf "Hello World!\n"

function clone( st:Byte[] )->Byte[]
  local len = strlen( st )
  return strcpy( Byte[len+1], st )
  
Integer[20]

[[@Byte]][20]
