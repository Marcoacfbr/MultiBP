

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <errno.h>
#include <netdb.h> //hostent
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <sys/ipc.h> 
#include <sys/msg.h> 

using namespace std;

#define BASEPORT 5200
#define CONFIG_LINE_BUFFER_SIZE 50000

#define EXEC 1
#define READ 2
#define CONT 4
#define END 3
#define ERROR -1
#define DEBUG 0

#define READS 1
#define WRITES 2
#define BUS_BASE_SIZE	(1*1024) // 1K cells
#define BUFFER_SIZE (2*BUS_BASE_SIZE)

//#define WORKDIR "/home/users/marcofigueiredo/dynbp/work"
//#define SHAREDIR "/home/users/marcofigueiredo/dynbp/share"

int socketfdread;
int socketfd;
int socketfdwrite;
string hostname = "192.168.0.87";
int port;
int totalwrite = 0;
int totalread = 0;
char controlleradd[16];
int comma = 0;
string filename2;
pthread_mutex_t lock;

typedef struct {
	int h;
	union {
		int f;
		int e;
	};
} __attribute__ ((aligned (8))) cell_t;

struct cell_list {
	cell_t cell;
	cell_list * next;
};

cell_t * bufferin, * bufferout;
cell_list * head, * tail;

int decode(char instruct[4]) {
        //printf ("\n\n ### Balancer: instruct: #%s# \n", instruct);
	if (instruct[1] == 'X')
		return (1);       
	if (instruct[0]  == 'R')
		return (2);
	if (instruct[1] == 'N')
		return (3);
	return (ERROR);
}


void closeSocket(int socketfd) {
    fprintf(stderr, "close(): %d\n", socketfd);
    if (socketfd != -1) {
        close(socketfd);
        socketfd = -1;
    }
}

int readSocket(cell_t* buf, int len) {
    int pos = 0;
    while (pos < len*sizeof(cell_t)) {
    	int ret = recv(socketfdread, (void*)(((unsigned char*)buf)+pos), len*sizeof(cell_t), 0);
        if (ret == -1) {
        	closeSocket(socketfdread);
            fprintf(stderr, "recv: Socket error -1\n");
            break;
        }
        pos += ret;
    }
    return pos/sizeof(cell_t);
}

int writeSocket(const cell_t* buf, int len) {
    int ret = send(socketfdwrite, buf, len*sizeof(cell_t), 0);
    if (ret == -1) {
        fprintf(stderr, "send: Socket error: -1\n");
        return 0;
    }
    return ret;
}


int resolveDNS(const char * hostname , char* ip) {
    struct hostent *he;
    struct in_addr **addr_list;
    int i;

    if ( (he = gethostbyname( hostname ) ) == NULL)
    {
        // get the host info
        herror("gethostbyname");
        return 1;
    }

    addr_list = (struct in_addr **) he->h_addr_list;

    for(i = 0; addr_list[i] != NULL; i++)
    {
        //Return the first one;
        strcpy(ip , inet_ntoa(*addr_list[i]) );
        return 0;
    }

    return 1;
}


 void initSocketRead() {
    int rc;
    int sock;                        // Socket descriptor
    struct sockaddr_in echoServAddr; // Echo server address
    int new_socket, server_fd; 
    struct sockaddr_in address;
    int addrlen = sizeof(controlleradd);

    int opt = 1;


    int portread = BASEPORT + 501;

    //printf (" \n\n *** DEBUG 1 ***");

    // Create a reliable, stream socket using TCP
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		printf("ERROR creating socket: %s\n", strerror(errno));
		sleep(1);
    }

    //printf (" \n\n *** DEBUG 2: %s ***", hostname.c_str());

    // Resolving DNS
    char ip[100];
     //if (resolveDNS(hostname.c_str(), ip)) {
     //   printf("FATAL: cannot resolve hostname: %s\n", hostname.c_str());
     //   exit(-1);
    //} 

    strcpy(ip,"192.168.0.89");
 
    //printf (" \n\n *** DEBUG 3   ip: %s  ***", ip);


    // Construct the server address structure
    memset(&echoServAddr, 0, sizeof(echoServAddr));     // Zero out structure
    echoServAddr.sin_family      = AF_INET;             // Internet address family
    echoServAddr.sin_addr.s_addr = inet_addr(controlleradd);   // Server IP address
    echoServAddr.sin_port        = htons(portread); // Server port

    // Establish the connection to the echo server
    int max_retries = 3000;
    int retries = 0;
    int ok = 0;

    while ((retries < max_retries) && !ok) {
		if ((rc=connect(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr))) < 0) {
			if (retries % 100 == 0) {
				fprintf(stderr, "!!!!! ERROR connecting to Server %s:%d [Retry %d/%d]. %s\n", controlleradd, portread,
						retries, max_retries, strerror(errno));
			}
			retries++;
			usleep(1000);
		} else {
			ok = 1;
		}
	}
	if (!ok) {
		fprintf(stderr, "ERROR connecting to Server. Aborting\n");
		exit(-1);
	}
    fprintf(stderr, "\n *** DEBUG: Connected to Server %s\n", inet_ntoa(echoServAddr.sin_addr));
    

    socketfdread = sock;
}

 void initSocketWrite() {
     int rc;
     int servSock;                    /* Socket descriptor for server */
     int clntSock;                    /* Socket descriptor for client */
     struct sockaddr_in echoServAddr; /* Local address */
     struct sockaddr_in ClntAddr; /* Client address */
     struct sockaddr_in ControlAddr; /* Controller address */
     unsigned int clntLen;            /* Length of client address data structure */
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
       printf ("\n Balancer: listening for connections on port %d \n", portwrite);


     /* Set the size of the in-out parameter */
     clntLen = sizeof(ClntAddr);

     /* Wait for a client to connect */
     if ((clntSock = accept(servSock, (struct sockaddr *) &ClntAddr, (socklen_t*) &clntLen)) < 0){
         printf("ERROR; return code from accept() is %d\n", clntSock);
         exit(-1);
     }
     

     /* clntSock is connected to a client! */

     printf("Balancer: handling client %s \n\n", inet_ntoa(ClntAddr.sin_addr));

     closeSocket(servSock);

     socketfdwrite = clntSock;
 }


void * readcell (void * x ) {

   printf ("Balancer: reading thread initiated! \n");

   initSocketRead();

   int ret;

   int flag = 0;

   while (1) {
           
	   ret = readSocket(bufferin, BUS_BASE_SIZE);
           totalread = totalread + ret;
	   for (int i=0; i<ret; i++) {
		  cell_list * p = (cell_list *) malloc (sizeof(cell_list));
                  if (! p)
                    printf ("\n\n\n ### Balancer: Allocation error! ### \n\n");
		  memcpy(&p->cell, bufferin+i*sizeof(cell_t), sizeof(cell_t));
		  p->next = NULL;
                  if (!flag) {
                     printf ("\n\n Balancer: h read: %d \n", p->cell.h);
                     flag = 1;
                  }

		  pthread_mutex_lock(&lock);

		  if (head == NULL)
			  head = p;
		  else
			  tail->next = p;
		  tail = p;

		  pthread_mutex_unlock(&lock);
	   }
         flag = 0;
         printf ("Balancer: READ %d cells, Total: %d cells \n", ret, totalread);
   }
}




void * writecell (void * x ) {

   printf ("Balancer: writing thread initiated! \n");

   initSocketWrite();

     cell_list * aux;

    int flag = 0;

   while (1) {
      

      int i = 0;

      pthread_mutex_lock(&lock);
      aux = head;
      while ((aux != NULL) && (i < BUS_BASE_SIZE)) {
    	  memcpy (bufferout+i*sizeof(cell_t), &head->cell, sizeof(cell_t));
          if (!flag) {
             printf ("\n\n Balancer: h write: %d \n", head->cell.h);             
             flag = 1;
          }
    	  head = head->next;
    	  free (aux);
    	  aux = head;
    	  i++;
          totalwrite++;
      }
      flag = 0;
      if (aux == NULL)
         tail = NULL;
      pthread_mutex_unlock(&lock);

     int ret = writeSocket(bufferout, i);
     printf("Balancer: WRITE %d cells , Total: %d \n", i, totalwrite);

   }
}


void mqread ()  {

    key_t key; 
    int msgid; 
    char message[10];
  
    // ftok to generate unique key 
    key = ftok("/home/marcoacf/din680", 65); 
  
    // msgget creates a message queue 
    // and returns identifier 
    msgid = msgget(key, 0666 | IPC_CREAT); 
  
    // msgrcv to receive message 
    msgrcv(msgid, &message, sizeof(message), 1, 0); 
  
    // display the message 
    printf("\n\n Balancer: Data Received is : %s \n",  message); 
  
    // to destroy the message queue 
    msgctl(msgid, IPC_RMID, NULL); 


}


int main(int argc, char const *argv[])
{
    int server_fd, new_socket, bal_socket, valread;
    //int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    //int op;
    //int wr = 0;
    //int rd = 0;
    int addrlen = sizeof(address);
    char command[CONFIG_LINE_BUFFER_SIZE] = {0};
    //char hello[CONFIG_LINE_BUFFER_SIZE] = "Hello from server";
    char new_command[CONFIG_LINE_BUFFER_SIZE];
    //char car;
    std::ostringstream np;

    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock, &mutexattr);


    head = NULL;
    tail = NULL;

    //FILE * fpread;
    std::ostringstream ss;
    string filename;
    //int vgpu;

    bufferin = (cell_t*)malloc(BUFFER_SIZE*sizeof(cell_t));
    bufferout = (cell_t*)malloc(BUFFER_SIZE*sizeof(cell_t));

    int gpu = atoi(argv[1]);
    char WORKDIR[100];
    std::ostringstream wk;
    wk  << "/work";
    strcpy (WORKDIR,argv[2]);

    // Creating socket file descriptor
    if ((server_fd = socket(PF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR , &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( BASEPORT+gpu );

    // Forcefully attaching socket to the port 5000
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    else
      printf ("\n ### Balancer: socket accepting connections on port: %d \n" , BASEPORT+gpu);

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    else
      printf ("\n ### Balancer %d: connection accepted! \n\n", gpu );

    close(server_fd);
    strcpy(controlleradd,inet_ntoa(address.sin_addr));
    //cout << "\n ****" << controlleradd << "*** \n";

    int socketinitiated = 0;
    char instruct[4];
    int numpart, numgpus;
    while (1) {
        memset(command, 0, sizeof(command));
    	valread = read( new_socket , command, CONFIG_LINE_BUFFER_SIZE+2);
    	printf("\n *** Balancer %d: command received: %s\n", gpu, command );

    	
        char sendmessage[5] = {'R','E','A','D'};

    	int k = 0;
        memset(instruct,0,sizeof(instruct));
    	while (command[k] != '|') {
                //printf ("\n\n Char: %c \n\n", command[k]);
    		instruct[k] = command[k];
    		k++;

    	}

        //printf ("\n\n\n\n ### Ballancer DEBUG #### \n\n\n");

        if (k<4)
          instruct[k] = ' ';
        memset(new_command, 0, sizeof(new_command));
    	strncpy(new_command,command+k+1,strlen(command)-k);

        //printf("New command: %s \n", new_command);

        char ssearch[13] = "dynamic=";
        char *pstr1 = strstr(new_command,ssearch);
        char pstr[10];
        int cont;
        int pstrlen =1;
        if (pstr1) {
           pstr1 = pstr1+8;
           //int pstrlen = 1;
           while (*(pstr1+pstrlen) != ' ') 
              pstrlen++;
           for (cont =0; cont<pstrlen; cont++) 
              pstr[cont] = *(pstr1+cont);
           pstr[cont]='\0';
           numgpus = atoi(pstr);

           //printf ("\n\n *** 1: %c  len: %d  gpus: %d *** \n", *pstr1, pstrlen, numgpus);
         }
         
         if (numgpus == 0) {
            strcpy(ssearch,"split=");
            char *pstr2 = strstr(new_command,ssearch);
            pstrlen=1;
            while (*(pstr2+pstrlen) != ' ') {
              pstrlen++;
              if (*(pstr2+pstrlen) == ',')
                 comma++;
            }
          numgpus = comma+1;

         }         
           

        // printf ("\n\n\n\n ### Ballancer %d DEBUG #### \n\n\n", gpu);

        strcpy(ssearch,"--part=");
        pstr1 = strstr(new_command,ssearch);
        //int numpart;
        
       //printf ("\n\n\n\n ### Balancer A %d DEBUG #### \n\n\n", gpu);

        if (pstr1) {
           pstr1 = pstr1+7;
           int pstrlen = 1;
           //printf ("\n\n\n\n ### Balancer B %d DEBUG #### \n\n\n", gpu);
           //printf ("pstr1: %s  -  char: %c", pstr1, *(pstr1+pstrlen));
           while (*(pstr1+pstrlen) != ' ')
              pstrlen++;
           //printf ("\n\n\n\n ### Balancer C %d DEBUG #### \n\n\n", gpu);
           for (cont =0; cont<pstrlen; cont++)
              pstr[cont] = *(pstr1+cont);
           //printf ("\n\n\n\n ### Balancer D %d DEBUG #### \n\n\n", gpu);
           pstr[cont]='\0';
           numpart = atoi(pstr);
           //printf ("\n\n\n\n ### Balancer E %d DEBUG #### \n\n\n", gpu);

           //printf ("\n\n *** 1: %c  len: %d  part: %d *** \n", *pstr1, pstrlen, numpart);
         }

       //printf ("\n\n\n\n ### Balancer B %d DEBUG #### \n\n\n", gpu);

       //printf ("\n\n >>> Balancer %d:  Instruct: %s -  %d \n\n", gpu,instruct,decode(instruct));
       //char c = getchar();

       FILE * fpdyn;
        
        
    	switch (decode(instruct)) {
    	case EXEC:
    	    system(new_command);
            //string filename2;
            if (DEBUG) printf ("\n\n *** numpart: %d, numgpus: %d,  mod: %d \n", numpart, numgpus, numpart%numgpus);
            if ((numpart % numgpus) == 0) {
               np.str("");
               np.clear();
               np << numpart;
               if (!socketinitiated) {
                  initSocketRead();
                  socketinitiated = 1;
               }
              
               if (numgpus != comma+1) {
                  if (DEBUG) cout << "\n\n GPUs: " << numgpus << "Comma: " << comma;
                  printf ("\n\n *** Balancer %d: waiting for READ control file... \n", gpu);
                  filename2.clear();
                  filename2 = WORKDIR + wk.str() + np.str() + "/dynread.txt";
                  //printf ("\n\n *** Agurdandno pelo arquivo %s \n", filename2.c_str());
                  while ((fpdyn  = fopen(filename2.c_str(),"rt")) == NULL)
                      usleep(100);
                  fclose(fpdyn);
                  printf ("\n\n *** Balancer: READ control file identified. \n");
               }
              strcpy(sendmessage, "READ");
              send(socketfdread, sendmessage, strlen(sendmessage), 0);
              printf (" *** Balancer: Sent message %s to controller \n", sendmessage);
              printf ("\n\n *** Balancer %d: waiting for END control file... \n", gpu);
              filename2.clear();

              filename2 = WORKDIR + wk.str() + np.str() + "/dynend.txt";
              //printf ("\n\n *** Agurdandno pelo arquivo %s \n", filename2.c_str());
              while ((fpdyn  = fopen(filename2.c_str(),"rt")) == NULL)
                usleep(100);
              fclose(fpdyn);

              printf ("\n\n *** Balancer: END control file identified. \n");
              strcpy(sendmessage, "END ");
              send(socketfdread, sendmessage, strlen(sendmessage), 0);
              printf (" *** Balancer: Sent message %s to controller \n", sendmessage);
              fflush(stdout);
              printf ("\n");
            }
    	    break;
    	case CONT:
    	    system(new_command);
    	    break;
    	case READ:
    		//printf ("\n\n *** Initiating writing thread... *** \n\n");
    		//if (!wr) {
    		//   pthread_create(&thrwrite, NULL, writecell, (void *) NULL);
    		//   wr = 1;
    		//}
            break;
    	case END:
    		close(new_socket);
    		close(socketfdread);
    		//close(socketfdwrite);
    		//if (rd)
    		//   pthread_cancel(thrread);
                //if (wr)
    		//   pthread_cancel(thrwrite);
    	    return (0);
    	    break;
    	}
    //return 0;
    //car = getchar();
    //strcpy(command,"");
    }
    return 0;
}
