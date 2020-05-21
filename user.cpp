#include <signal.h>
#include <string.h>
#include <fstream>

#include "simpi.h"
#define MATRIX_DIMENSION_X 2
#define MATRIX_DIMENSION_Y 2

int par_id;

void segfault_printer(int dummy)
{
  char buf[20];
  sprintf(buf, "%d: segfaulted\n", par_id);
  write(STDOUT_FILENO, buf, strlen(buf));
  exit(1);
}

int main(int argc, char* argv[])
{
  par_id = atoi(argv[1]);
  int synch_size = atoi(argv[2]);
  simpi my_simpi(par_id, synch_size);
  matrix A(my_simpi, MATRIX_DIMENSION_X, MATRIX_DIMENSION_Y);
  matrix D(my_simpi, MATRIX_DIMENSION_X, MATRIX_DIMENSION_Y);
  vector C(my_simpi, 10);
  my_simpi.synch();
  /*
  for (int y = 0; y < MATRIX_DIMENSION_Y; y++) {
    for (int x = 0; x < MATRIX_DIMENSION_X; x++) {
      A.get(x, y) = (double)x + y;
    }
  }
  */
  A.get(0,0) = (double)4;
  A.get(0,1) = (double)7;
  A.get(1,0) = (double)2;
  A.get(1,1) = (double)6;
  std::cout << A;
  D = A.inverse();
  std::cout << D; 
}
