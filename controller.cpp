#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <bits/stdc++.h> 
// #include <seqan/alignment_free.h>

#include <iostream>
#include <sstream>
#include <string>
#include <fstream>

using namespace std;

#define MAX_CONFIG_VARIABLE_LEN 30000
#define MAX_IP_LEN 15
#define MAX_BP 3
#define MAX_PORT_LEN 5
#define CONFIG_LINE_BUFFER_SIZE 50000
#define MAX_GPUS 300
#define MAX_OUT_NAME_LEN 256
#define GPU 0
#define IP 1
#define PORT 2
#define SPLIT 3
#define SEQ 4
#define WAIT 5
#define BP 6
#define MODEL 7
#define PROG 8
#define GFLOPS 9
#define BASEPORT 5200
#define ERROR -1

#define READS 1
#define WRITES 2

#define DEBUG 0
#define LIMIT 2



struct config_struct {
    int gpus;
    int blockpruning;
    int breakpoints;
    int waitgpu;
    char ips[MAX_GPUS][MAX_IP_LEN];
    char ports[MAX_GPUS][MAX_IP_LEN];
    int gpu_number[MAX_GPUS];
    int ctrlport[MAX_GPUS];
    int sock[MAX_GPUS];
    int split[MAX_GPUS];
    char seq0[MAX_CONFIG_VARIABLE_LEN];
    char seq1[MAX_CONFIG_VARIABLE_LEN];
    char model[MAX_CONFIG_VARIABLE_LEN];
    char prog[MAX_CONFIG_VARIABLE_LEN];
    char gflops[MAX_GPUS][MAX_IP_LEN];

} config;

int splitnew[MAX_GPUS*MAX_BP];
int splitold[MAX_GPUS*MAX_BP];
int socketfdwrite;
int dyn;
int comma; 
int gflops = 0;

int read_int_from_config_line(char* config_line) {
    char prm_name[MAX_CONFIG_VARIABLE_LEN];
    int val;
    sscanf(config_line, "%s %d\n", prm_name, &val);
    return val;
}


void read_str_from_config_line(char* config_line, int op) {

	char new_line[CONFIG_LINE_BUFFER_SIZE];
	int j = 0;
	while (config_line[j] != ' ')
		j++;
	strncpy(new_line,config_line+j+1,strlen(config_line)-j);
	//printf("\n new = %s \n", new_line);
	//getchar();
	char * tok = strtok(new_line, ",");
    if (op == SEQ) {
    	sscanf(tok, "%s", config.seq0);
    	tok = strtok(NULL, ",");
    	sscanf(tok, "%s", config.seq1);
    }
    else if (op == MODEL)
        sscanf(tok, "%s", config.model);
    else if (op == PROG)
        sscanf(tok, "%s", config.prog);
    else {
    	int count = 0;
    	while ((tok != NULL) && (count < MAX_GPUS)) {
    		if (op == GPU)
    			sscanf(tok, "%d", &config.gpu_number[count]);
    		else if (op == SPLIT)
    			sscanf(tok, "%d", &config.split[count]);
    		else if (op == IP)
    			sscanf(tok, "%s", config.ips[count]);
    		else if (op == PORT)
    			sscanf(tok, "%s", config.ports[count]);
                else if (op == GFLOPS)
                        sscanf(tok, "%s", config.gflops[count]);
    		count++;
    		tok = strtok(NULL, ",");
    	}
        if ((op == SPLIT) && (count == 1))
          for (int t=0; t<MAX_GPUS; t++)
              config.split[t]=1;
    }
}


int read_config_file(char* config_filename) {
    FILE *fp;
    char buf[CONFIG_LINE_BUFFER_SIZE];
    config.blockpruning = 1;
    if ((fp=fopen(config_filename, "r")) == NULL) {
        fprintf(stderr, "Failed to open config file %s", config_filename);
        exit(EXIT_FAILURE);
    }
    while(! feof(fp)) {
        fgets(buf, CONFIG_LINE_BUFFER_SIZE, fp);
        if (buf[0] == '#' || strlen(buf) < 4) {
            continue;
        }
        if (strstr(buf, "GPUS ")) {
            config.gpus = read_int_from_config_line(buf);
        }
        if (strstr(buf, "BP ")) {
            config.blockpruning = read_int_from_config_line(buf);
        }
        if (strstr(buf, "BREAK ")) {
            config.breakpoints = read_int_from_config_line(buf);
        }
        if (strstr(buf, "WAIT ")) {
            config.waitgpu = read_int_from_config_line(buf);
            //printf("WAIT: %d \n", config.waitgpu);
        }
        if (strstr(buf, "GPU# ")) {
            read_str_from_config_line(buf, GPU);
        }
        if (strstr(buf, "IP ")) {
            read_str_from_config_line(buf, IP);
        }
        if (strstr(buf, "PORT ")) {
            read_str_from_config_line(buf, PORT);
        }
        if (strstr(buf, "SPLIT ")) {
            read_str_from_config_line(buf, SPLIT);
        }
        if (strstr(buf, "SEQ ")) {
            read_str_from_config_line(buf, SEQ);
        }
        if (strstr(buf, "MODEL ")) {
            read_str_from_config_line(buf, MODEL);
        }
        if (strstr(buf, "PROG ")) {
            read_str_from_config_line(buf, PROG);
        }
        if (strstr(buf, "GFLOPS ")) {
            read_str_from_config_line(buf, GFLOPS);
        }

    }
    fclose(fp);
    if (strcmp(config.model,"static") == 0)  {
        dyn = 0;
        config.breakpoints = 0;
    }
    else
        dyn = config.gpus;
    printf("\n ### Controller: parameter read from config file: \n");
    printf("GPUs = %d\n", config.gpus);
    //printf("Block Pruning = %d\n", config.blockpruning);
    printf("BPs = %d\n", config.breakpoints);
    //printf("WAIT = %d\n", config.waitgpu);
    printf("SEQ 0 = %s \n", config.seq0);
    printf("SEQ 1 = %s \n", config.seq1);
    printf("MODEL = %s \n", config.model);
    printf("PROG = %s \n", config.prog);
    //printf("DYN = %d \n", dyn);
    /* for (int j=0; j<config.gpus;j++)
       printf("IP[%d] =  %s \n", j, config.ips[j]);
    for (int l=0; l<config.gpus;l++) {
        printf("GFLOP[%d] =  %s \n", l, config.gflops[l]);
        gflops = gflops + atoi(config.gflops[l]);
    } */
    return EXIT_SUCCESS;
}

int connect_agents () {

	struct sockaddr_in agent_addr;

        printf("\n ### Controller: connecting balancers... \n");

	for (int i=0; i<config.gpus; i++) {
	        config.ctrlport[i] = BASEPORT + config.gpu_number[i];
	        if ((config.sock[i] = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	          printf("\n Socket creation error \n");
	          return ERROR;
	        }
	        memset(&agent_addr, '0', sizeof(agent_addr));
	        agent_addr.sin_family = AF_INET;
                agent_addr.sin_port = htons(config.ctrlport[i]);
	        if(inet_pton(PF_INET, config.ips[i], &agent_addr.sin_addr)<=0) {
	          printf("\nInvalid address/ Address not supported \n");
	          return ERROR;
	        }
	        if (connect(config.sock[i], (struct sockaddr *)&agent_addr, sizeof(agent_addr)) < 0) {
	          printf("\nConnection Failed: %d - %s - %d \n", i, config.ips[i], config.ctrlport[i]);
	          return ERROR;
	        }
	 }
   return EXIT_SUCCESS;
}

void rebalance (int start) {
	int k;
	int sum = 0;
	int sumcheck = 0;
        int divisor;

        if (config.gpus < 16) 
           divisor = 1000;
        else
           divisor = 10000;  

	for (int i=start; i<(start+config.gpus);i++) {
	     sum = sum + splitnew[i];
	     //printf ("Soma: %lld \n", sum);
	  }

	for (k=start; k<(start+config.gpus);k++) {
                if (k > (config.gpus -2))
                   printf ("k: %d sum: %d divison: %d split: %d", k, sum, divisor, splitnew[k]);  
                splitnew[k] = (divisor*splitnew[k])/sum;
	   	//printf ("Split k: %d \n", splitnew[k]);
	   	sumcheck = sumcheck + splitnew[k];
	  }
        if (sumcheck != divisor)
	  	splitnew[k-1] = splitnew[k-1] + (divisor-sumcheck);


}

float readlastline(string filename)
{
    std::string result = "";
    double r, psin;

    if (DEBUG) cout << "\n" << filename.c_str() << "\n";

    //ifstream fin(filename.c_str());
     ifstream fin;
     fin.exceptions (ifstream::badbit);
     fin.open (filename.c_str(), std::ifstream::in);
     while (!fin) {
        sleep(6);
        fin.open (filename.c_str(), std::ifstream::in);
     }
    //fin.exceptions (ifstream::failbit | ifstream::badbit | ifstream::eofbit);
    

    if(fin) {
        fin.seekg(0,std::ios_base::end);      //Start at end of file
        char ch = ' ';                        //Init ch not equal to '\n'
        while(ch != '\n') {
            fin.seekg(-2,std::ios_base::cur); //Two steps back, this means we
                                              //will NOT check the last character
            if((int)fin.tellg() <= 0){        //If passed the start of the file,
                fin.seekg(0);                 //this is the start of the line
                break;
            }
            fin.get(ch);                      //Check the next character
        }

        std::getline(fin,result);
        fin.close();

        if (DEBUG) cout << "\n\n" << result << "\n\n";

        string temp;
        stringstream result2;
        result2 << result;

        for (int j=1; j<=14; j++) {
        	temp = "";
        	result2 >> temp;
                //cout << "Temp: " << temp << "\n";
                //printf ("Temp: #%s# \n", temp.c_str());
        }
        psin = strtof(temp.c_str(),0);
        temp = "";
        result2 >> temp;
        temp = "";
        result2 >> temp;
        if (DEBUG) cout << "Temp 2: " << temp << "\n";
        r = strtof(temp.c_str(),0);
        if (filename.find("output") != std::string::npos) {
        	temp = "";
        	result2 >> temp;
                if (DEBUG) cout << "Temp 2: " << temp << "\n";
        	r = strtod(temp.c_str(),0);
                psin = 0;
        }
        if (DEBUG) printf ("\n ### Controller: psin: %f  r: %f \n\n", psin, r);
        return(r + psin);
    }
    else
      printf ("\n ### Controller: file %s not open! \n", filename.c_str());

}



void initSocketWrite() {
     int rc;
     int servSock;                    /* Socket descriptor for server */
     int clntSock;                    /* Socket descriptor for client */
     struct sockaddr_in echoServAddr; /* Local address */
     struct sockaddr_in ClntAddr; /* Client address */
     struct sockaddr_in ControlAddr; /* Controller address */
     unsigned int clntLen;            /* Length of client addrewss data structure */
     int portwrite = BASEPORT + 501;

     if (DEBUG) printf("SocketCellsWriter: create socket\n");

     /* Create socket for incoming connections */
     if ((servSock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
         fprintf(stderr, "ERROR creating server socket; return code from socket() is %d\n", servSock);
         exit(-1);
     }

     int optval = 1;
     setsockopt(servSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));


     /* Construct local address structure */
     memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
     echoServAddr.sin_family = AF_INET;                /* Internet address family */
     echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
     echoServAddr.sin_port = htons(portwrite);      /* Local port */

     /* Bind to the local address */
     if (DEBUG) printf("SocketCellsWriter: Bind to local address %d\n", portwrite);
     if ((rc = bind(servSock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr))) < 0) {
         fprintf(stderr, "ERROR; return code from bind() xxxxxxxxx is %d\n", rc);
         exit(-1);
     }

     /* Mark the socket so it will listen for incoming connections */
     if (DEBUG) printf("SocketCellsWriter: Listening on port %d\n", portwrite);
     if ((rc=listen(servSock, 1)) < 0) {
         fprintf(stderr, "ERROR; return code from listen() is %d\n", rc);
         exit(-1);
     }
     else
       printf ("\n ### Controller: listening for connections on port %d \n", portwrite);


     /* Set the size of the in-out parameter */
     clntLen = sizeof(ClntAddr);

     /* Wait for a client to connect */
     if ((clntSock = accept(servSock, (struct sockaddr *) &ClntAddr, (socklen_t*) &clntLen)) < 0){
         printf("ERROR; return code from accept() is %d\n", clntSock);
         exit(-1);
     }
     

     /* clntSock is connected to a client! */

     printf("\n ### Controller: handling client %s \n\n", inet_ntoa(ClntAddr.sin_addr));

     close(servSock);

     socketfdwrite = clntSock;
 }


int main(int argc, char *argv[]) {

    std::ostringstream ss, ss2;
    struct timeval start, end;

    int ret;
    int vgpu;
    long long int sum = 0;
    long long int sumcheck = 0;
    int part;
    int valread = 0;

    string command;
    string deccom;

    char WORKDIR[100];
    strcpy (WORKDIR,argv[2]);

    char recmessage[10] = {0};

    config.waitgpu = 0;
    ret = read_config_file(argv[1]);
    //vgpu = config.gpus*(config.breakpoints+1);

    // start time counter
    gettimeofday(&start, NULL);

    FILE * dec;
    
    //string deccom;

    if (strcmp(config.model,"decision") == 0) {
       deccom.clear();
       ss.clear();
       ss << (gflops*config.breakpoints);
       deccom = deccom + " " + config.prog + " " + config.seq0 + " " + config.seq1 + " " + ss.str() + " &";
       system(deccom.c_str());
       deccom.clear();
       deccom = "";
       deccom = deccom + config.prog + ".txt";
       //cout << deccom;
       printf ("\n ### Controller: waiting for the decision module: %s... \n", deccom.c_str());
       while ((dec = fopen(deccom.c_str(),"rt")) == NULL)
          usleep(100);
       char op;
       op = fgetc(dec);
       fclose(dec);
       if ((op == '1') || (op == '2')) {
          dyn = 0;
          config.breakpoints = 0;
          printf("\n ### Controller: Warning - dynamic execution disabled by decision module! \n");     
       }    
    }

    vgpu = config.gpus*(config.breakpoints+1);

    // Connect balancer sockets
    if (connect_agents () < 0) {
    	//return ERROR;
        printf ("### Controller: sockect connection error \n");
        return ERROR;
    }   
    // initial weigth distribution: even
    for (int j=0; j< vgpu; j++) {
        if (config.gpus < 16)
    	       splitnew[j] = 1000/config.gpus;
        else
               splitnew[j] = 10000/config.gpus;
    	if (DEBUG) printf ("split: %d \n", splitnew[j]);
    }

    // sum of weights
    for (int i=0; i<config.gpus;i++) {
       splitnew[i] = config.split[i];
      //printf ("Soma: %lld \n", sum);
    }
    // initial rebalance
    for (int i=0; i<=config.breakpoints;i++) 
      rebalance(i*config.gpus);

    //initSocketWrite();
    int socketinitiated = 0;
    

    // start time counter
    //gettimeofday(&start, NULL);
    int i;

    // send initial command execution
    for (int kk=0; kk<=config.breakpoints; kk++) {
       for (i=0; i<config.gpus;i++) {
    	  part = kk*config.gpus + i + 1;
          //command.str("");
          command.clear();
    	  command = "EXEC|./cudalign --blocks=512 --clear --no-flush --stage-1 --shared-dir=";
          ss.str("");
          ss.clear();
          ss << WORKDIR;
          command = command + ss.str() + "/share --work-dir=";
          command = command + ss.str() + "/work"; 
          ss.str("");
    	  ss.clear();
    	  ss << part;
    	  command = command + ss.str();
    	  ss.str("");
    	  ss.clear();
    	  ss << dyn;
    	  command = command + " --dynamic=" + ss.str();

    	  if ((config.waitgpu) && (part > config.gpus)) {
    		ss.str("");
    		ss.clear();
    		ss << (part - config.gpus);
    		command = command + " --wait-part=" + ss.str();
    	  } 

          if (!config.blockpruning)
             command = command + " --no-block-pruning";

    	  command = command + " --split=";
          for (int ii=0; ii<(vgpu-1); ii++) {
    		ss.str("");
    		ss.clear();
    		ss << splitnew[ii];
    		command = command + ss.str() + ",";
    	  }
                     

    	  ss.str("");
    	  ss.clear();
     	  ss << splitnew[vgpu-1];
    	  command = command + ss.str();
    	  ss.str("");
    	  ss.clear();
    	  if (part != 1) {
    	     int x;
    	     x = (part-2)%config.gpus;
    	     if (part%config.gpus == 1) {
                command = command + " --load-column=file://";
                ss.str("");
                ss.clear();
                ss << WORKDIR;
                command = command + ss.str() + "/share/out";
                ss.str("");
                ss.clear();
                ss << part-1;
                command = command + ss.str() + ".bin";
             }
    	     else {
                ss <<  config.ports[x];
      	        command = command + " --load-column=socket://" + config.ips[x] + ":" + ss.str();
                // int po = atoi(config.ports[x]) + 100;
                // ss.str("");
                // ss.clear();
                // ss << po;
                // string sstemp = ss.str();
                // strcpy(config.ports[x],sstemp.c_str());
             }
    	  }
    	
    	  ss.str("");
    	  ss.clear();
    	  if (part != vgpu) {
    	      if (part%config.gpus == 0) {
                  command = command + " --flush-column=file://";
                  ss.str("");
                  ss.clear();
                  ss << WORKDIR;
                  command = command + ss.str() + "/share/out";
                  ss.str("");
                  ss.clear();
                  ss << part;
                  command = command + ss.str() + ".bin";

              }
    	      else {
                 ss <<  config.ports[i];
      	         command = command + " --flush-column=socket://" + config.ips[i] + ":" + ss.str();
             }
    	  }
    	
    	  ss.str("");
    	  ss.clear();
    	  ss << config.gpu_number[i];
    	  command = command + " --gpu=" + ss.str();
          command = command + " --part=";
     	  ss.str("");
    	  ss.clear();
    	  ss << part;
    	  command = command + ss.str() + " " + config.seq0 + " " + config.seq1 + " &";
    	  cout << command << std::endl;

    	  // send mensage to agents
    	  char com[command.size() + 1];
    	  command.copy(com,command.size()+1);
    	  com[command.size()] = '\0';
    	  send(config.sock[i], com, strlen(com), 0);
          printf( "\n ### Controller: exec message sent to GPU %d \n", i); 
       }	
       
      if (!socketinitiated) {
             initSocketWrite();
             socketinitiated = 1;
          }


      // Loop for the number of breakpoints
      if (kk<config.breakpoints) {
          string filename;
          ss2.str("");
          ss2.clear();
          ss2 << WORKDIR;
          filename = ss2.str() + "/work";
    	  ss2.str("");
    	  ss2.clear();
    	  ss2 << part;
    	  filename = filename + ss2.str() + "/dyn.txt";
    	  FILE * fp = NULL;

    	  /* // send READ command to balancer start read thread
    	  if ((part % config.gpus) == 0 && (part != vgpu)) {
    		  ss2.str("");
    		  ss2.clear();
    		  ss2 << READS;
    		  command = "READ|" + ss2.str();
    		  send(config.sock[config.gpus-1], command.c_str(), strlen(command.c_str()), 0);
    	  } */

    	                
          // wait for socket message from balancer which indicates performance counters can be read
    	  printf ("\n ### Controller: waiting for balancer READ message. \n");

    	  //while (valread ==0) {
              memset(recmessage, 0, sizeof(recmessage));
              valread = read(socketfdwrite, recmessage, 4);
          // }
                    
          
    	  printf ("\n ### Controller: balancer message received: %d - %s.\n\n", valread, recmessage);
          memset(recmessage, 0, sizeof(recmessage));
          valread=0;

          //for (int kkk=0;kkk<config.gpus*(config.breakpoints+1);kkk++)
          //   printf ("Original: splitnew[%d]:  %d \n", kkk, splitnew[kkk]);

          int unbalanced = 0;
          char op2;
          if (strcmp(config.model,"decision") == 0) {
             dec = fopen(deccom.c_str(),"rt");
             rewind(dec);
             op2 = fgetc(dec);
             fclose(dec);
             while (op2 == '-') {
               usleep(100);
               dec = fopen(deccom.c_str(),"rt");              
               rewind(dec);
               op2 = fgetc(dec);
               fclose(dec);
               // printf ("\n\n Read op: %c", op2);
             }
             //fclose(dec);
             if ((op2 == '1') || (op2 == '2')) {
                unbalanced = 1;
                printf("\n ### Controller: Warning - dynamic execution disabled by decision module! \n");
             }
           }
          
          FILE * fpr;
          string fprname;
          fprname = WORKDIR;
          fprname = fprname  + "/rebalance.txt";
          fpr = fopen (fprname.c_str(),"at");


          if (!unbalanced)
   	  // read last line of performance log files
    	     for (int jj=0; jj<config.gpus; jj++) { 
                ss2.str("");
                ss2.clear();
                ss2 << WORKDIR;
                filename = ss2.str() + "/work";
    	        ss2.str("");
    	        ss2.clear();
                int part2;
                part2 = (kk*config.gpus)+jj+1;
    	        ss2 << part2;
    	        if ((part2 % config.gpus) == 0)
    	           filename = filename + ss2.str() + "/inputBuffer.log";
    	        else
    	           filename = filename + ss2.str() + "/outputBuffer.log";
    	        float block;
                block = readlastline(filename);
                float newvalue;
                newvalue =  (float)splitnew[part+jj-config.gpus] * block;
                fprintf (fpr, "\n Old index: %d, old value: %d, block: %f, new index: %d, new value: %f \n\n", (part+jj-config.gpus),(splitnew[part+jj-config.gpus]), block, (part+jj), newvalue); 
                if ((int) newvalue != 0) {
                   for (int zz=0; zz<jj; zz++)
                      if (((int) newvalue > LIMIT*splitnew[part+zz]) || ((int) newvalue < splitnew[part+zz]/LIMIT)) 
                         unbalanced = 1;
                   //splitold[part+jj] = splitnew[part+jj];
                   splitnew[part+jj] =  (int) newvalue;
                }
          else
              unbalanced = 1; 
    	  }
    	  for (int kkk=0;kkk<config.gpus*(config.breakpoints+1);kkk++)
             printf ("Before rebalance: splitnew[%d]:  %d \n", kkk, splitnew[kkk]);
          // printf("part: %d \n", part);

          // rebalance weights based on current counters
          if (unbalanced)
             for (int kkk=0;kkk<config.gpus;kkk++)
               splitnew[part+kkk] = splitnew[part+kkk-config.gpus];
               //splitnew[part+kkk] = splitold[part+kkk];
          else
             rebalance(part);
          //FILE * fpr;
          //fpr = fopen ("rebalance.txt","at");
 
          for (int kkk=0;kkk<config.gpus*(config.breakpoints+1);kkk++)
             fprintf (fpr, "After rebalance: splitnew[%d]:  %d \n", kkk, splitnew[kkk]);

          fclose(fpr);

          // wait for socket message from balancer which indicates last GPU finished its job
          printf ("\n ### Controller: waiting for balancer END message. \n");

          while (valread ==0) {
             memset(recmessage, 0, sizeof(recmessage));
             valread = read(socketfdwrite, recmessage, 4);
          }
          printf ("\n ### Controller: balancer message received: %s.\n\n", recmessage);

          //sleep(10);

        }
     }

     

    // wait for socket message from balancer which indicates last GPU finished its job
    printf ("\n ### Controller: waiting for balancer READ message. \n");
    valread=0;
    while (valread ==0) {
      memset(recmessage, 0, sizeof(recmessage));
      valread = read(socketfdwrite, recmessage, 4);
    }

    printf ("\n ### Controller: balancer message received: %s.\n\n", recmessage);

  
    
    // wait for socket message from balancer which indicates last GPU finished its job
    printf ("\n ### Controller: waiting for balancer END message. \n");
    valread=0;
    while ((valread ==0) && (strcmp(recmessage,"END  ")!= 0)) {
      memset(recmessage, 0, sizeof(recmessage));
      valread = read(socketfdwrite, recmessage, 4);
    }

    printf ("\n ### Controller: balancer message received: %s.\n\n", recmessage);
    fflush(stdout);

    

    close(socketfdwrite);

    // end timer 
    gettimeofday(&end, NULL);

     double time_taken;
     time_taken = (end.tv_sec - start.tv_sec)* 1000000LL;
     time_taken = (time_taken + (end.tv_usec - start.tv_usec))/1000.0f;

     printf("\n ### Controller: execution time: %15f \n\n", time_taken);

     FILE * ff;
     string ffname;
     ffname = WORKDIR;
     ffname = ffname  + "/statistics.txt";
     ff = fopen(ffname.c_str(),"wt");
     fprintf(ff, "Execution time: %15f \n\n", time_taken);
     fclose (ff);

     char com2[5] = "END|";
     sleep(1);     
     for (int cont=0; cont<config.gpus; cont++) {
        send(config.sock[cont], com2, strlen(com2), 0); 
        close(config.sock[cont]);
     }


     return EXIT_SUCCESS;
}




