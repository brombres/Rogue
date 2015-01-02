#include <iostream>
using namespace std;

struct Tethra
{
  Tethra()
  {
    cout << "Tethra!" << endl;
  }
};


class Alpha : Tethra
{
};


int main()
{
  Alpha a;
  return 0;
}
