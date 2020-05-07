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
#include <fcntl.h>
#include <sys/mman.h>

extern "C" {
	#include "hps.h"
	#include "hps_0.h"
	#include "terasic_os.h"
}

#define HW_REGS_BASE (ALT_STM_OFST)
#define HW_REGS_SPAN (0x04000000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)

#define H2F_BASE (0xC0000000)
#define H2F_SPAN (0x40000000)
#define H2F_MASK (H2F_SPAN - 1)

#define IORD(base, index) (*(((uint32_t *)base)+index))
#define IOWR(base, index, data) (*(((uint32_t *)base)+index) = data)

// bit mask for write control
#define START_BIT_MASK 0x80000000
#define DUMMY_DATA_BIT_MASK 0x40000000

// bit mask for read status
#define DONE_BIT_MASK 0x00000001

//declared in terasic_os.h
uint32_t OS_GetTickCount()
{

    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint32_t OS_TicksPerSecond(){
	return 1000;
}


#define PORT 8080
#define SA struct sockaddr

#define BILLION 1000000000L
#define DATAPOINTS 16384 //number of datapoints per time window (16384 = 2^14)
#define TW 9156 //number of time windows (chosen to make DATAPOINTS * TW be as close to 150 000 000 as possible)
//#define DATAPOINTS 10
//#define TW 2

/*
To compile: g++ server_adc_A.cpp -o server_adc_A
To run: ./server_adc_A

This program can connect to a client and transfer data to it given a prompt. It can start up at boot time and run indefinetely,
connecting to the client when the client is executed. The program captures data from the Terasic ADC-SoC board, partly using
code provided by Terasic. It then sends the data to client and, if desired by the client,checks accuracy. This is done by making
the client send all the data back and the server will compare the data sent to the data generated. (In the future the result of
this test could be sent to the client.)

Note: This version captures data from channel A on the ADC only. Data can be captured from channel B with the program server_adc_B.cpp
or from both channels with the program server_adc_AB.cpp.

To change number of datapoints to be sent per timwindow, change DATAPOINTS defined in the header to desired value. Number of
timewindows is controlled by TW, which may also be changed in the header. These changes have to be made on the client side too!

There are time measurements for writing data to socket and for the entire process of reading the prompt to send data, generate/read in
data, send and check accuracy.
*/

unsigned short int buff[DATAPOINTS]; //array to be sent
unsigned short int data_sent[TW*DATAPOINTS]; //array to store sent data
unsigned short int data_back[TW * DATAPOINTS]; //array to store data received for accuracy check
unsigned short int data_back_tot[TW * DATAPOINTS]; //for each read(), the content of "data_back" is copied here before "data_back" is overwritten by a new call of read()


void buff_generator()
/*This function randomly generates data (an alternative to capturing from the ADC), for this option: uncomment "buff_generator()" and comment out "data_2_buff()"
right above the count-loop.*/
{
	bzero(buff, sizeof(buff));
	for (int i = 0; i < DATAPOINTS; i++)
	{
		buff[i] = (unsigned short int) rand();
//		printf("buff[%d] = %hu \n", i, buff[i]);
	}
}


//This fucntion captures data from the ADC
void ADC_CAPTURE(uint32_t *controller_A, uint32_t *controller_B,
int16_t *mem_base_A, int16_t *mem_base_B, int nSampleNum) {
	uint32_t Control, status_A, status_B;
	int is_done = 0, is_timeout = 0; //i
	//int16_t Value_A, Value_B;
	uint32_t Timeout;
	// start capture
	Control = nSampleNum;
	//Control |= DUMMY_DATA_BIT_MASK; for test only
	IOWR(controller_A, 0x00, Control);
	IOWR(controller_B, 0x00, Control);
	Control |= START_BIT_MASK;
	IOWR(controller_A, 0x00, Control); // rising edge to trigger
	IOWR(controller_B, 0x00, Control); // rising edge to trigger
	//printf("Start Capture... PROBANDO COMPILACIO00N\n");
	// wait done
	Timeout = OS_GetTickCount() + OS_TicksPerSecond();
	is_done = 0;
	while (is_done != 0x3 && !is_timeout) {
		status_A = IORD(controller_A, 0x00);
		if (status_A & DONE_BIT_MASK)
		is_done |= 0x1;
		status_B = IORD(controller_B, 0x00);
		if (status_B & DONE_BIT_MASK)
		is_done |= 0x2;
		else if (OS_GetTickCount() > Timeout)
		is_timeout = 1;
	}
	//printf("Channel A status: 0x%xh\n", status_A);
	//printf("Channel B status: 0x%xh\n", status_B);
	// dump adc data

	if (is_timeout) {
		printf("Timeout\r\n");
	} else {
		int i;
		unsigned short int Value_A, Value_B;
		bzero(buff, sizeof(buff));
		for (i = 0; i < nSampleNum; i++) {
			Value_A = *(mem_base_A + i);
			Value_B = *(mem_base_B + i);
			buff[i] = Value_A; //copying ch. A only.
		}
		//printf("All data is copied to buffer! \n");
	}
}

//driver function for data capturing
int data_2_buff() {
	const int nSampleNum = DATAPOINTS;
	int fd;
	int i;
	int j;
	void *h2f_lw_virtual_base;
	void *h2f_virtual_base;
	uint32_t *h2p_lw_led_addr = NULL;
	uint32_t *h2p_lw_sw_addr = NULL;
	uint32_t *ad9254_a_controller_addr = NULL;
	uint32_t *ad9254_b_controller_addr = NULL;
	int16_t *ad9254_a_data_addr = NULL;
	int16_t *ad9254_b_data_addr = NULL;
	j = 0;
	if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
		//printf("ERROR: could not open \"/dev/mem\"...\n");
		return (1);
	}
	h2f_lw_virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE),
	MAP_SHARED, fd, HW_REGS_BASE);
	if (h2f_lw_virtual_base == MAP_FAILED) {
		//printf("ERROR: mmap() failed...\n");
		close(fd);
		return (1);
	}
	h2f_virtual_base = mmap(NULL, H2F_SPAN, (PROT_READ | PROT_WRITE),
	MAP_SHARED, fd, H2F_BASE);
	if (h2f_virtual_base == MAP_FAILED) {
		//printf("ERROR: axi mmap() failed...\n");
		close(fd);
		return (1);
	}

	h2p_lw_led_addr = (uint32_t*) ((uint8_t*) h2f_lw_virtual_base
			+ ((ALT_LWFPGASLVS_OFST + LED_PIO_BASE) & HW_REGS_MASK));
	h2p_lw_sw_addr = (uint32_t*) ((uint8_t*) h2f_lw_virtual_base
			+ ((ALT_LWFPGASLVS_OFST + DIPSW_PIO_BASE) & HW_REGS_MASK));
	*h2p_lw_led_addr = *h2p_lw_sw_addr;
	ad9254_a_controller_addr = (uint32_t*) ((uint8_t*) h2f_virtual_base
			+ (TERASIC_AD9254_A_BASE & H2F_MASK));
	ad9254_b_controller_addr = (uint32_t*) ((uint8_t*) h2f_virtual_base
			+ (TERASIC_AD9254_B_BASE & H2F_MASK));
	ad9254_a_data_addr = (int16_t*) ((uint8_t*) h2f_virtual_base
			+ (ONCHIP_MEMORY2_ADC_A_BASE & H2F_MASK));
	ad9254_b_data_addr = (int16_t*) ((uint8_t*) h2f_virtual_base
			+ (ONCHIP_MEMORY2_ADC_B_BASE & H2F_MASK));
	for(i=0;;i++) {
		if (j == 10000) {
			printf("ITERACION NUMERO: %i....\n",i);
			j = 0;
		}
		j++;
		ADC_CAPTURE(ad9254_a_controller_addr, ad9254_b_controller_addr,
		ad9254_a_data_addr, ad9254_b_data_addr, nSampleNum);
	}
	if (munmap(h2f_virtual_base, H2F_SPAN) != 0) {
		//printf("ERROR: munmap() failed...\n");
		close(fd);
		return (1); }
	if (munmap(h2f_lw_virtual_base, HW_REGS_SPAN) != 0) {
		//printf("ERROR: munmap() fggailed...\n");
		close(fd);
		return (1); }
	close(fd);
	return 0; }


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
			//buff_generator(); //generating random data once
			data_2_buff(); //captures data from ADC
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
	int sockfd, connfd;
	socklen_t len; //changed from int
	//int len;
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





