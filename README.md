# MIST

This repository contains completed projects for the Mapping of IGM Spin Temperature collaboration.

Directories:
tcp/integer_arrays: a tcp network program with four files, including two .c files with code for the server and client as well as the compiled server and client.

server_adc: the server and client programs, modified to capture data from the Terasic ADC-Soc. The header files are provided
by Tearsic and are necessary for compilation of the programs. The suffix (A, B, AB) to the server program indicates which
channel (AB = both), data will be captured from.
