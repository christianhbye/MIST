#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#define PORT 8080
#define SA struct sockaddr
#define BILLION 1000000000L
#define DATAPOINTS 16384 //number of datapoints per time window
#define TW 9156 //number of time windows
//#define DATAPOINTS 10
//#define TW 2

/*
[Tested on Ubuntu 16.04 and 18.04, Fedora 26]
To compile: gcc server.c -o server
To run: ./server

This program can connect to a client and transfer data to it given a prompt. It can start up at boot time and run indefinetely,
connecting to the client when the client is executed. The program currently generates random data in the function buff_generator(),
but this can be changed to taking input data from an instrument. It then sends the data to client and, if desired by the client,
checks accuracy. This is done by making the client send all the data back and the server will compare the data sent to the data generated.
(In the future the result of this test should be sent to the client.)

To change number of datapoints to be sent per timwindow, change DATAPOINTS defined in the header to desired value. Number of
timewindows is controlled by TW, which may also be changed in the header. These changes have to be made on the client side too!

Data can be generated once and be sent for all timewindows or new can data can be generated for every time window. The former is default. For the latter,
comment out the line "buff_generator();" (line 80, above the count-loop) and uncomment the line in the count-loop.

There are time measurements for writing data to socket and for the entire process of reading the prompt to send data, generate/read in
data, send and check accuracy.
*/

unsigned short int buff[DATAPOINTS]; //array to be sent
unsigned short int data_sent[TW*DATAPOINTS]; //array to store sent data
unsigned short int data_back[TW * DATAPOINTS]; //array to store data received for accuracy check
unsigned short int data_back_tot[TW * DATAPOINTS]; //for each read(), the content of "data_back" is copied here before "data_back" is overwritten by a new call of read()


void buff_generator()
{
	bzero(buff, sizeof(buff));
	for (int i = 0; i < DATAPOINTS; i++)
	{
		buff[i] = (unsigned short int) rand();
//		printf("buff[%d] = %hu \n", i, buff[i]);
	}
}

void func(int sockfd) //function for sending and receiving data
{
	int input = 0; //determines if data is to be sent or not: 0 to exit, 1 to send
	int acc_check = 0; //determines if accuracy check is to be performed or not: 0 for no, 1 for yes

	for (;;) { //infinite loop for sending data
		read(sockfd, &input, 1); //"input" is chosen on the client side, this reads what the client chose
		if (input == 0) {
			printf("Exiting ... \n");
			break; //exits the loop if input == 0
		}
		else if (input == 1) {
			read(sockfd, &acc_check, 1); //reads whether client wants accuracy check or not
			uint64_t timetot = 0; //the total time it takes to send data and check accuracy (if chosen)
			struct timespec func_start, func_end; //start and end time for timetot
			clock_gettime(CLOCK_MONOTONIC, &func_start);
			uint64_t diff_tot = 0; //total (all time windows) time it takes to actually send the data (calling write())
			uint64_t diff = 0; //time per timewindow to send the data
			struct timespec start, end; //start and end time for diff
			int x_cum = 0; //total number of bytes written
			int sent_index = 0; //index count for data_sent
			int bytes2copy = DATAPOINTS * sizeof(unsigned short int); //bytes of data in one time window
			buff_generator(); //generating data once
			//sending data, TW number of timewindows, DATAPOINTS integers per TW:
			for (int count=0; count<TW; count++) { //send one time window per iteration
	//			buff_generator(); //generating new data for every time window
				clock_gettime(CLOCK_MONOTONIC, &start);
				int x = write(sockfd, buff, sizeof(buff)); //send data, x = bytes sent
				if (x == -1)
				{
					printf("server disconnect \n");
					exit(1);
				}
				clock_gettime(CLOCK_MONOTONIC, &end);
				diff = BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
	//			printf("TW %d writes in %llu nanoseconds = %f seconds \n", count, (long long unsigned int) diff, (double) diff/1000000000);
	//			printf("Bytes written: %d \n", x);
				diff_tot += diff; //add to the cumulative time measurement
				x_cum += x; //add to cumuluative byte count
				if (acc_check == 1) //accuracy check
				{
					//memcpy(data_sent, buff, bytes2copy);
					for (int j=0; j<DATAPOINTS; j++)
					{
						data_sent[sent_index++] = buff[j]; //copies each sent datapoint to data_sent
					}
				}
			}
			printf("Total bytes written: %d \n", x_cum);
			printf("Total writing time = %llu nanoseconds = %f seconds \n", (long long unsigned int) diff_tot, (double) diff_tot/1000000000);
		//	printf("Data sent: /n %s /n", data_sent);

			//accuracy check, client sends back all the data it received, server compares with what it sent originally
			if (acc_check == 1)
			{
				printf("acc_check = 1 \n");
				int read_bytes_back = 0; //bytes read per iteration
				int total_read_bb = 0; //bytes read in total
				int iterations = 0; //number of iterations required to read data
				int data_back_index = 0; //index count for data_back_tot
				//printf("\nData back is: \n");
				do {
					bzero(data_back, sizeof(data_back)); //set all elements of data_back to 0
					read_bytes_back = read(sockfd, data_back, sizeof(data_back)); //read data sent from client
					total_read_bb += read_bytes_back; //add to cumulative byte count
					iterations ++; //counting iterations
					int buff_size = read_bytes_back/sizeof(unsigned short int); //number of elements that were read in the current iteration
					for (int l = 0; l<buff_size; l++)
					{
						data_back_tot[data_back_index++] = data_back[l]; //copy each element to data_back_tot
						//printf("%hu \n", data_back[l]);
					}
				} while (total_read_bb < TW * bytes2copy); //continue until equally many bytes have been sent and read
				printf("Total bytes received: %d \n", total_read_bb);
				printf("Iterations: %d \n", iterations);

				//comparing data_sent with data_back_tot
				int diff_index = 0; //number of indices data_sent and data_back_tot differ at
				for (int k = 0; k < TW*DATAPOINTS; k++)
				{
//					printf("Back: %hu \n", data_back_tot[k]);
//					printf("Sent: %hu \n", data_sent[k]);
					if (data_back_tot[k] != data_sent[k])
					{
					//	printf("Differs at index %d! \n", k);
						diff_index++; //count if they are different
					}
					else
					{
						continue;
					}
				}
				printf("Differs at %d indices \n", diff_index);
			}
			clock_gettime(CLOCK_MONOTONIC, &func_end);
			timetot = BILLION * (func_end.tv_sec - func_start.tv_sec) + (func_end.tv_nsec - func_start.tv_nsec);
			printf("Total time elapsed is %f seconds \n", (double) timetot/1000000000);
		}
		else {
			input = 0;
			continue;
		}
	}
}

// Driver function
int main()
{
//	signal(SIG)
	int sockfd, connfd, len;
	struct sockaddr_in servaddr, cli;

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		perror("socket");
		exit(0);
	}
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr));

	// assign IP, PORT
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(PORT);

	// Allowing reuse of address
	int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		printf("Reuse address failed \n");
		perror("setcokopt");
		exit(0);
	}
	else
		printf("Reusing address! \n");

	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) { 
		printf("socket bind failed...\n");
		perror("bind");
		exit(0);
	}
	else
		printf("Socket successfully binded..\n"); 

	// Now server is ready to listen and verification 
	if ((listen(sockfd, 5)) != 0) { 
		printf("Listen failed...\n"); 
		perror("listen");
		exit(0);
	}
	else
		printf("Server listening..\n");
	len = sizeof(cli);

	// Accept the data packet from client and verification
	connfd = accept(sockfd, (SA*)&cli, &len);
	if (connfd < 0) {
		printf("server acccept failed...\n");
		perror("accept");
		exit(0);
	}
	else
		printf("server acccepts the client ...\n");

	// Function for transferring data between client and server
	func(connfd);
	printf("Regular close\n");

	// After data transfer, close the socket
	close(sockfd);
}


