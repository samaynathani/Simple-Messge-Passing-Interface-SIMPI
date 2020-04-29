all : mpi user
clean : 
	rm user mpi
	rm /dev/shm/simpi_shared_mem
mpi : mpi.cpp user
	g++ -gstabs mpi.cpp -o mpi -std=c++11 -lrt
user : user.cpp
	g++ -gstabs user.cpp -o user -std=c++11 -lrt