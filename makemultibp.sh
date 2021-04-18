#!/bin/bash
g++ -std=c++14 -O3 -DNDEBUG -W -Wall -pedantic -fopenmp -lpthread -lrt -I./ decision.cpp -o  decision  
g++ -Wall -w controller.cpp  -o  controller  -lm -g
g++ -Wall -w -O3 balancer.cpp -o balancer -lm -g -lpthread

