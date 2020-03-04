#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <signal.h>
#define PORT 8080
#define SA struct sockaddr
#define DATAPOINTS 16384 //number of datapoints per timewindow
#define TW 9156 //number of timewindows
//#define DATAPOINTS 10
//#define TW 2


/*
To compile: gcc client.c -o client
To run: ./client
[Tested on Ubuntu 16.04 and 18.04, Fedora 26]

Note: the server has to be exectued before the client program.

This is the client side of the network program. Given a user input, this program can request data from a server, which will be
read and stored in an array on this side. The array will be overwritten for each request.

The client requests DATAPOINTS number of datapoints per time window and TW number of time windows of data. DATAPOINTS and TW may
be changed in the header, but in that case they have to be changed in the server file too.

If desired, an accuracy check can be performed, in which the client sends the data back to the server for the server to compare with
the data it sent orignally.

To change what ip-adress to connect to, change the first line of this program. To transfer data locally instead of over a network,
change in main():
From: servaddr.sin_addr.s_addr = inet_addr(ip_adress);
To: servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

To terminate the program one might use Ctrl+C, this will close the socket properly on both ends.

*/

char ip_adress[12] = "192.168.2.10"; //The ip-adress of the server

void sigintHandler() //this function handles Ctrl+C by closing the socket properly
{
	signal(SIGINT, sigintHandler);
	printf("\nExiting ... \n");
	exit(1);
}


unsigned short int buff[DATAPOINTS]; //array that stores the data read for each time window
unsigned short int data_recv[TW*DATAPOINTS]; //array that saves a copy of all the data from all time windows

void func(int sockfd) //function for data transfer
{
	int check; //checks if input is valid or not
	int input = 0; //determines if data is to be sent or not: 0 = no, 1 = yes
	for(;;) { //infinite loop for data transfer
		printf("Enter 1 to send data or 0 to quit: ");
		check = scanf("%d", &input);
		while (check != 1) { //check == 1 if the input is an integer
			printf("Invalid input. \n");
			getchar();
			printf("Enter 1 to send data or 0 to quit: ");
			check = scanf("%d", &input);
		}
		write(sockfd, &input, 1); //tells the server what the value of input is
		if (input == 0) {
			printf("Exiting ... \n");
			break; //exits loop if no data is to be sent
		}
		else if (input == 1) {
			printf("Do you want to check accuracy? Type 0 for no, 1 for yes: ");
			int acc_check;
			check = 0;
			check = scanf("%d", &acc_check); //stores user's answer to accuracy check
			while (check != 1 || (acc_check != 0 && acc_check != 1)) {
				printf("\nInvalid input. \n");
				getchar();
				printf("Type 0 for no, 1 for yes: ");
				check = scanf("%d", &acc_check);
			}
			write(sockfd, &acc_check, 1); //tells server if accuracy check or not
			int r_cum = 0; //total number of bytes read
			int r; //bytes read per iteration
			int recv_index = 0; //index count for data_recv
			int count = 0;

			//read data
	//		printf("Data received: \n");
			do {
				bzero(buff, sizeof(buff)); //sets all elements in buff to 0
				r = read(sockfd, buff, sizeof(buff)); //read data sent from server, r = number of bytes read
				//printf("r = %d, count = %d \n", r, count);
				count ++;
				r_cum += r; //add to the cumulative bytes read count
				int buff_size = r/sizeof(unsigned short int); //number of elements read = ratio of bytes read to bytes per datapoint
				//memcpy(data_recv, buff, sizeof(buff));
				for (int j=0; j<buff_size; j++)
				{
					data_recv[recv_index++] = buff[j]; //updates data_recv with the buff read in this iteration
//					printf("%hu \n", buff[j]);
				}
			} while(r_cum < DATAPOINTS * TW *sizeof(unsigned short int)); //continue until all data has been read
			printf("\nTotal bytes read: %d \n", r_cum);

			//accuracy check: send back data to server
			if (acc_check == 1)
			{
				int write_byte = 0; //bytes sent
				write_byte = write(sockfd, data_recv, sizeof(data_recv)); //sending data
				printf("Bytes sent back: %d \n", write_byte);
			}
		}
		else {
			continue;
		}
	}
}

int main()
{
	int sockfd, connfd;
	struct sockaddr_in servaddr, cli;

	signal(SIGINT, sigintHandler);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		perror("socket");
		exit(0);
	}
	else
		printf("Socket successfully created..\n");
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;



//	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); //for local transfer
	servaddr.sin_addr.s_addr = inet_addr(ip_adress); //for network transfer

	servaddr.sin_port = htons(PORT);

	if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
		printf("connection with the server failed...\n");
		perror("connect");
		exit(0);
	}
	else
		printf("connected to the server..\n");
	func(sockfd);
	close(sockfd);
}


