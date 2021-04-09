#!/bin/bash
g++ -std=c++14 -O3 -DNDEBUG -W -Wall -pedantic -fopenmp -lpthread -lrt -I./seqan/include decision.cpp -o  decision  
g++ -Wall controller.cpp  -o  controller  -lm -g
g++ -Wall -O3 balancer.cpp -o balancer -lm -g -lpthread

