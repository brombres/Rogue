#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "Crom.h"

/*
CromType* type_Node;

typedef struct Node
{
  CromInfo     header;

  int          value;
  struct Node* left;
  struct Node* right;
} Node;

Node* Node_create( CromType* type, int value )
{
  Node* node = (Node*) CromType_create_object( type, 0 );
  node->value = value;
  return node;
}

void Node_clean( void* obj )
{
  printf( "Cleaning up node %d\n", ((Node*)obj)->value );
}

void print_tree( Node* cur )
{
  if ( !cur ) return;
  print_tree( cur->left );
  printf( "%d ", cur->value );
  print_tree( cur->right );
}
*/

int main()
{
  /*
  Crom* crom = Crom_create();

  type_Node = Crom_define_object_type( crom, 0, sizeof(Node), 2, 0, Node_clean );
  CromType_add_reference_offset( type_Node, Crom_calculate_offset(Node,left) );
  CromType_add_reference_offset( type_Node, Crom_calculate_offset(Node,right) );

  Node* root = Node_create( type_Node, 5 );
  root->left = Node_create( type_Node, 3 );
  root->left->left  = Node_create( type_Node, 1 );
  root->left->left->right = Node_create( type_Node, 2 );
  root->left->right = Node_create( type_Node, 4 );
  root->right = Node_create( type_Node, 7 );
  root->right->left = Node_create( type_Node, 6 );

  printf( "Tree: " );
  print_tree( root );
  printf( "\n" );
  Crom_print_state( crom );

  root->left->left = 0;

  CromObject_reference( root );
  Crom_collect_garbage( crom );

  printf( "Tree: " );
  print_tree( root );
  printf( "\n" );

  //Crom_print_state( crom );

  CromObject_reference( root );
  Crom_collect_garbage( crom );
  //Crom_print_state( crom );

  Crom_add_global_reference( crom, root );

  Crom_collect_garbage( crom );
  //Crom_print_state( crom );

  Crom_collect_garbage( crom );
  //Crom_print_state( crom );

  Crom_remove_global_reference( crom, root );

  Crom_collect_garbage( crom );
  Crom_print_state( crom );

  Crom_collect_garbage( crom );
  Crom_print_state( crom );

  crom = Crom_destroy( crom );
  */
  printf( "Done\n" );
  return 0;
}
