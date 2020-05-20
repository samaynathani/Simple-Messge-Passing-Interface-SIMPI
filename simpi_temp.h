 
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <iomanip>

#include <iostream>
#include <map>
#include <string>
#include <vector>

#define SYNCH_OBJECT_MEM_NAME "/simpi_shared_mem"
#define UNIQUE_ID_SIZE 23
typedef struct matrix_metadata {
  char unique_id[UNIQUE_ID_SIZE];
  int file_descriptor;
  size_t size;
  double* matrix_data;
} matrix_metadata;

typedef struct synch_object {
  int par_count;
  char last_matrix_id[UNIQUE_ID_SIZE];
  int ready[];
} synch_object;

class simpi {
 public:
  simpi(int id_, size_t synch_size)
  {
    id = id_;
    int fd = shm_open(SYNCH_OBJECT_MEM_NAME, O_RDWR, 0777);
    if (fd == -1) {
      perror("Unable to shm_open synch_object: ");
      exit(1);
    }
    synch_info = (synch_object*)mmap(NULL, synch_size, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, fd, 0);
    if (synch_info == MAP_FAILED) {
      perror("Unable to mmap synch_info: ");
      exit(1);
    }
  }
  ~simpi()
  {
    for (std::pair<std::string, matrix_metadata> matrix : matrix_info) {
      free_matrix(matrix.second.unique_id);
    }
    shm_unlink(SYNCH_OBJECT_MEM_NAME);
  }
  int get_id() { return id; }
  synch_object* get_synch_info() { return synch_info; }

  std::pair<std::string, double*> create_matrix(int x, int y);

  void free_matrix(std::string unique_id);
  void synch();

 private:
  int id;
  synch_object* synch_info;
  std::map<std::string, matrix_metadata> matrix_info;
  std::string sync_shared_mem_name;
  std::string get_shared_mem_name();
};

class matrix  // similar stuff for vector
{
 private:
  int xdim, ydim;
  std::string unique_id;
  simpi* mysimpi = NULL;  // for later reference
 public:
  double* arr;
  void print();
  int get_x() { return xdim; }
  int get_y() { return ydim; }
  int determinant(double* A, int n, int order);
  void adjoint(double* A, double* adj, int order, int par_id, int par_count);
  void getCofactor(double* A, double* temp, int p, int q, int n, int order);
  double get_algbera(int pos) { return arr[pos]; }
  void set(int pos, int val) { arr[pos] = val; }
  matrix& multiply(matrix other);
  matrix& add(matrix other);
  matrix& scalar_matrix_mult(int other);
  matrix& subtract(matrix other);
  matrix& transpose();
  bool matrix_is_equal(matrix other);
  matrix(simpi& simp, int x, int y)  // constructor
  {
    // use simp and init the matrix for all processes. The id is also in simp
    mysimpi = &simp;
    std::pair<std::string, double*> pass_back(mysimpi->create_matrix(x, y));
    unique_id = pass_back.first;
    arr = pass_back.second;
    xdim = x;
    ydim = y;
  }
  ~matrix()  // destructor
  {
    // use mysimpi for getting rid of the mem and unlink stuff
    mysimpi->free_matrix(unique_id);
  }
  double& get(int x, int y) { return arr[x + y * xdim]; }
  matrix& inverse()
  {
    matrix* inverse = new matrix(*mysimpi, xdim, ydim);
    matrix* adj = new matrix(*mysimpi, xdim, ydim);

    // Find determinant of A[][]
    int det = determinant(arr, xdim, xdim);
    if (det == 0) {
      std::cout << "Singular matrix, can't find its inverse";
      return *inverse;
    }
    std::cout << "Determinant is: " << det;
    // std::chrono::steady_clock::time_point end1 =
    // std::chrono::steady_clock::now();

    // std::chrono::steady_clock::time_point begin2 =
    // std::chrono::steady_clock::now();

    // Find adjoint
    // float adj[order*order];
    mysimpi->synch();

    adjoint(arr, adj->arr, xdim, mysimpi->get_id(),
            mysimpi->get_synch_info()->par_count);
    mysimpi->synch();

    // std::chrono::steady_clock::time_point end2 =
    // std::chrono::steady_clock::now();

    // std::chrono::steady_clock::time_point begin3 =
    // std::chrono::steady_clock::now(); Find Inverse using formula "inverse(A)
    // = adj(A)/det(A)"
    for (int i = 0; i < xdim; i++)
      for (int j = 0; j < xdim; j++)
        inverse->get(i, j) = adj->get(i, j) / double(det);

    return *inverse;
  }

  friend std::ostream & operator << (std::ostream &out, const matrix &m){
      if  (m.mysimpi->get_id() == 0)
    {
        for(int i=0; i < m.xdim; i++){
        out << "\n";
        for(int j=0; j < m.ydim; j++){
            out << std::fixed << std::setprecision(2) <<  m.arr[i + j * m.xdim];
            out << ", ";
        }
        }
        out << "\n";
        return out;
    }
    return out;
  }
};

class vector  // similar stuff for vector
{
 private:
  int dim;
  simpi* mysimpi = NULL;  // for later reference
  std::string unique_id;

 public:
  double* arr;
  int get_size() { return dim; }
  void set(int pos, double val) { arr[pos] = val; }
  double& get(int pos) { return arr[pos]; }
  vector& scalar_vector_mult(int other);
  void print();
  vector(simpi& simp, int a)  // constructor
  {
    // use simp and init the matrix for all processes. The id is also in simp
    mysimpi = &simp;
    std::pair<std::string, double*> pass_back(mysimpi->create_matrix(1, a));
    unique_id = pass_back.first;
    arr = pass_back.second;
    dim = a;
  }
  ~vector()  // destructor
  {
    // use mysimpi for getting rid of the mem and unlink stuff
    mysimpi->free_matrix(unique_id);
  }

  friend std::ostream & operator << (std::ostream &out, const vector &V){
    
    for(int i=0; i < V.dim; i++){
      out << std::fixed << std::setprecision(2) <<  V.arr[i];
      out << "\n";
    }
    return out;
  }
};

std::string simpi::get_shared_mem_name()
{
  // gets a unique name for each shared memory based on the time that each was
  // made
  timeval curTime;
  gettimeofday(&curTime, NULL);
  unsigned long micro = curTime.tv_sec * (uint64_t)1000000 + curTime.tv_usec;
  std::string name = "simpi_" + std::to_string(micro);
  return name;
}

void simpi::synch()
{
  int par_count = synch_info->par_count;
  int* ready = synch_info->ready;
  int synchid = ready[par_count] + 1;
  ready[id] = synchid;
  int breakout = 0;
  while (1) {
    breakout = 1;
    for (int i = 0; i < par_count; i++) {
      if (ready[i] < synchid) {
        breakout = 0;
        break;
      }
    }
    if (breakout == 1) {
      ready[par_count] = synchid;
      // and here we increment the additional variable
      break;
    }
  }
}

std::pair<std::string, double*> simpi::create_matrix(int x, int y)
{
  size_t size = x * y * sizeof(double);

  if (id == 0) {
    // generate a uniqueid for matrix
    std::string unique_id = get_shared_mem_name();
    // create a shared mem object
    int fd = shm_open(unique_id.c_str(), O_RDWR | O_CREAT, 0777);
    if (fd == -1) {
      std::string msg = std::to_string(id) + ": Unable to shm_open matrix (" +
                        unique_id + "):  ";
      perror(msg.c_str());
      exit(1);
    }
    ftruncate(fd, size);

    // allocate matrix
    double* matrix =
        (double*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (matrix == MAP_FAILED) {
      perror("Unable to mmap matrix: ");
      exit(1);
    }

    matrix_metadata metadata;
    metadata.size = size;
    metadata.file_descriptor = fd;
    strcpy(metadata.unique_id, unique_id.c_str());
    metadata.matrix_data = matrix;
    matrix_info[unique_id] = metadata;

    // write name to synch_object so that other processes can get the uniqie
    // id
    strcpy(synch_info->last_matrix_id, unique_id.c_str());
    synch();
    std::pair<std::string, double*> pass_back;
    pass_back = std::make_pair(unique_id, matrix);
    return pass_back;
  }
  else {
    // wait for id 0 to create the shared memory for matrix
    synch();
    // get the unique id from the synch object
    std::string unique_id = synch_info->last_matrix_id;
    // open and allocate the shared memory
    int fd = shm_open(unique_id.c_str(), O_RDWR, 0777);
    if (fd == -1) {
      std::string msg = std::to_string(id) + ": Unable to shm_open matrix (" +
                        unique_id + "):  ";
      perror(msg.c_str());
      exit(1);
    }
    double* matrix =
        (double*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (matrix == MAP_FAILED) {
      std::string msg = std::to_string(id) + ": Unable to mmap matrix: ";
      perror(msg.c_str());
      exit(1);
    }

    // create a metadata object
    matrix_metadata metadata;
    strcpy(metadata.unique_id, unique_id.c_str());
    metadata.file_descriptor = fd;
    metadata.matrix_data = matrix;
    matrix_info[unique_id] = metadata;
    std::pair<std::string, double*> pass_back =
        std::make_pair(unique_id, matrix);
    return pass_back;
  }
}

void simpi::free_matrix(std::string unique_id)
{
  // get matrix metadata
  // close fd, shmunmap, munmap
  matrix_metadata metadata = matrix_info[unique_id];
  close(metadata.file_descriptor);
  shm_unlink(metadata.unique_id);
  munmap(metadata.matrix_data, metadata.size);
}

// Functions for Inverse of Matrix
int matrix::determinant(double* A, int n, int order)
{
  int D = 0;  // Initialize result

  //  Base case : if matrix contains single element
  if (n == 1)
    return A[0];

  double temp[order * order];  // To store cofactors

  int sign = 1;  // To store sign multiplier

  // Iterate for each element of first row
  for (int f = 0; f < n; f++) {
    // Getting Cofactor of A[0][f]
    getCofactor(A, temp, 0, f, n, order);
    D += sign * A[0 + f * order] * determinant(temp, n - 1, order);

    // terms are to be added with alternate sign
    sign = -sign;
  }

  return D;
}

void matrix::adjoint(double* A,
                     double* adj,
                     int order,
                     int par_id,
                     int par_count)
{
  if (order == 1) {
    adj[0] = 1;
    return;
  }

  int rpp = order / par_count;
  int start = par_id * rpp;
  int end = start + rpp;

  // temp is used to store cofactors of A[][]
  int sign = 1;
  double temp[order * order];

  for (int i = 0; i < order; i++) {
    for (int j = start; j < end; j++) {
      // Get cofactor of A[i][j]
      getCofactor(A, temp, i, j, order, order);

      // sign of adj[j][i] positive if sum of row
      // and column indexes is even.
      sign = ((i + j) % 2 == 0) ? 1 : -1;

      // Interchanging rows and columns to get the
      // transpose of the cofactor matrix
      adj[j + i * order] = (sign) * (determinant(temp, order - 1, order));
    }
  }
}

void matrix::getCofactor(double* A,
                         double* temp,
                         int p,
                         int q,
                         int n,
                         int order)
{
  int i = 0, j = 0;

  // Looping for each element of the matrix
  for (int row = 0; row < n; row++) {
    for (int col = 0; col < n; col++) {
      //  Copying into temporary matrix only those element
      //  which are not in given row and column
      if (row != p && col != q) {
        temp[(i) + (j++) * order] = A[row + col * order];

        // Row is filled, so increase row index and
        // reset col index
        if (j == n - 1) {
          j = 0;
          i++;
        }
      }
    }
  }
}

void matrix::print()
{
    if  (mysimpi->get_id() == 0)
    {
        for (int i = 0; i < xdim; i++) {
            printf("\n");
            for (int j = 0; j < ydim; j++) {
            printf("%.2f,", get(i, j));
            }
        }
        printf("\n");
    }
    mysimpi->synch();
}

void vector::print()
{
  for (int i = 0; i < dim; i++) {
    printf("%.2f,\n", get(i));
  }
  printf("\n");
}


matrix& matrix::multiply(matrix other)
{
  matrix* result = new matrix(*mysimpi, xdim, other.get_y());
  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }

  int Arow = get_x();
  int Acol = get_y();
  int Brow = other.get_x();
  int Bcol = other.get_y();
  int rpp = Arow / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  
  if (get_x() % number_of_processes != 0){
      int leftover = Arow % number_of_processes;
      if (parId <= leftover){
		            parId += (Arow - leftover); 
                int start = parId;
                int end = start + 1;
                for (int a = start; a < end; a++) {
                  for (int b = 0; b < Arow; b++) {
                    int sum = 0;
                    for (int c = 0; c < Brow; c++) {
                      	sum = sum + other.get_algbera(c + a * Brow) * get_algbera(c * Arow + b);
                    }
                    result->set(a * Arow + b, sum);
                  }
                }              
                    
      }
  }
  if (Acol != Brow) {
    // Send error
    printf("error");
  }
//   mysimpi->synch();
//   mysimpi->synch();
  for (int a = start; a < end; a++) {
    for (int b = 0; b < Arow; b++) {
      int sum = 0;
      for (int c = 0; c < Brow; c++) {
        // new_matrix.arr[a+b*Bcol] +=
        // if (c + a * Brow > last_ndx || c * Arow + b > last_ndx ||
        //     a * Arow + b) {
        //   printf("out of bounds!");
        //   exit(1);
        // }
        sum = sum + other.get_algbera(c + a * Brow) * get_algbera(c * Arow + b);
      }
      result->set(a * Arow + b, sum);
    }
  }
    mysimpi->synch();
  return *result;
}




matrix& matrix::transpose()
  {
  matrix* result = new matrix(*mysimpi, get_y(), get_x());
  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }

  int Arow = get_x();
  int Acol = get_y();

  int rpp = Arow / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  
  for (int i = start; i < end; i++) {
    for (int j = 0; j < Acol; j++) {
      // if(A[j*Arow + i] == 4){
      //     printf("LESSS GOOO = %d\n", j+i*Arow);
      // }
  result->set(j + i * Acol, get_algbera(j * Arow + i));
     
    }
  }
  return *result;
}

matrix& matrix::add(matrix other)
{
  matrix* result = new matrix(*mysimpi, get_x(), get_y());
  
  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }

  int Arow = get_x();
  int Acol = get_y();
  int Brow = other.get_x();
  int Bcol = other.get_y();
  int rpp = Arow / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  
  if (Arow != Brow || Acol != Bcol) {
    // raise error
  }
    if (get_x() % number_of_processes != 0){
      int leftover = Arow % number_of_processes;
      if (parId <= leftover){
		            parId += (Arow - leftover); 
                int start = parId;
                int end = start + 1;
                for (int i = start; i < end; i++) {
                  for (int j = 0; j < Arow; j++) {
					result->set(j + i * Arow,
           				 (other.get_algbera(j + i * Arow) + get_algbera(j + i *Arow)));
                  }
                }              
                    
      }
  }
  for (int i = start; i < end; i++) {
    for (int j = 0; j < Arow; j++) {
      result->set(j + i * Arow,
            (other.get_algbera(j + i * Arow) + get_algbera(j + i * Arow)));
      
    }
  }
  // printf("End of alg\n");
  return *result;
}


matrix& matrix::subtract(matrix other)
{
  matrix* result = new matrix(*mysimpi, get_x(), get_y());

  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }

  int Arow = get_x();
  int Acol = get_y();
  int Brow = other.get_x();
  int Bcol = other.get_y();
  int rpp = Arow / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  

	if (get_x() % number_of_processes != 0){
      int leftover = Arow % number_of_processes;
      if (parId <= leftover){
		            parId += (Arow - leftover); 
                int start = parId;
                int end = start + 1;
                for (int i = start; i < end; i++) {
                  for (int j = 0; j < Arow; j++) {
					result->set(j + i * Arow,
           				 (other.get_algbera(j + i * Arow) - get_algbera(j + i *Arow)));
                  }
                }              
                    
      }
  }
  for (int i = start; i < end; i++) {
    for (int j = 0; j < Arow; j++) {
      result->set(j + i * Arow,
            (other.get_algbera(j + i * Arow) - get_algbera(j + i * Arow)));
      
    }
  }
  return *result;
}

matrix& matrix::scalar_matrix_mult(int scaler){
  matrix* result = new matrix(*mysimpi, get_x(), get_y());

  int size = get_x();
  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }


  int rpp = size / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  
  for (int i = start; i < end; i++) {
      for (int j = 0; j < get_y(); j++) {
        int pos = (get_y() * i + j);
        result -> set(pos, get_algbera(pos) * scaler);
    }
  }
  return *result;
}

bool matrix::matrix_is_equal(matrix other)
{
  int number_of_processes = mysimpi->get_synch_info()->par_count;
  int parId = mysimpi->get_id();
  
  if (number_of_processes > get_x()){
    number_of_processes = get_x();
  }

  int Arow = get_x();
  int Acol = get_y();
  int Brow = other.get_x();
  int Bcol = other.get_y();
  int rpp = Arow / number_of_processes;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  
  if (Arow != Brow || Acol != Bcol) {
    // error
  }

  for (int i = start; i < end; i++) {
    for (int j = 0; j < Acol; j++) {
     
      
            if (get_algbera(j + i * Acol) != other.get_algbera(j + i * Acol)){
              return false;
            }
    }
  }
  return true;
}

vector& vector::scalar_vector_mult(int scaler){
  vector* result = new vector(*mysimpi, get_size());
  int size = get_size();
  int rpp = size / mysimpi->get_synch_info()->par_count;
  int start = rpp * mysimpi->get_id();
  int end = start + rpp;
  for (int i = start; i < end; i++) {
    result->set(i, get(i) * scaler);
  }
  return *result;

}

matrix &operator*(matrix &lhs,matrix &rhs)
{
return lhs.multiply(rhs);
}
matrix &operator*(int lhs,matrix &rhs)
{
return rhs.scalar_matrix_mult(lhs);
}
matrix &operator*(matrix &lhs, int rhs)
{
return lhs.scalar_matrix_mult(rhs);
}
vector &operator*(int lhs,vector &rhs)
{
return rhs.scalar_vector_mult(lhs);
}
vector &operator*(vector &lhs, int rhs)
{
return lhs.scalar_vector_mult(rhs);
}
matrix &operator+(matrix &lhs,matrix &rhs)
{
return lhs.add(rhs);
}
void operator+=(matrix &lhs,matrix &rhs)
{
lhs =  lhs.add(rhs);
}
void operator-=(matrix &lhs,matrix &rhs)
{
lhs =  lhs.subtract(rhs);
}
matrix &operator-(matrix &lhs,matrix &rhs)
{
return lhs.subtract(rhs);
}
bool operator==( matrix &lhs,matrix &rhs)
{
return lhs.matrix_is_equal(rhs);
}

