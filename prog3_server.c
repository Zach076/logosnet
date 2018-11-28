/* CSCI 367 LogosNet: prog3_server.c
*
* 26 NOV 2018, Zach Richardson and Mitch Kimball
*/

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define MAXMSGSIZE 1000
#define QLEN 255 /* size of request queue */
#define NUMCLIENTS 255
#define TRUE 1
#define FALSE 0
int pvisits = 0; /* counts participant's connections */
int ovisits = 0; /* counts observers's connections */
int sdp[255]; /* socket descriptors for participants */
int sdo[255]; /* socket descriptors for observers */
int lsdp; //listening socket descriptor for participants
int lsdo; //listening socket descriptor for observers

//TODO: add a flag to determine which type of client this sd is
struct node{
    int socketDes;
    struct node *nextnode;
};

struct queue{
    struct node *first;
    struct node *last;
}requestqueue;

//sends data from buf of size len to sd and if theres a fixable error,
//try to send again, otherwise exit nicely
void betterSend(int sd, void* buf, size_t len) {
  ssize_t n = -1;
  //while errors occur
  while(n == -1) {
    //try to send data
    n = send(sd, buf, len, 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect both clents, and exit
      if(errno != ENOBUFS && errno != ENOMEM) {
        close(sd);
        exit(EXIT_FAILURE);
      }
    }
  }
}

//recieves data drom a send, storing the data of length len to buf
//prints error if recieve fails
void recieve(int sd, void* buf, size_t len, char* error) {
  ssize_t n;
  //recieve data
  n = recv(sd, buf, len, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != len) {
    fprintf(stderr,"Read Error: %s Score not read properly from sd:%d\n", error, sd);
    close(sd);
    exit(EXIT_FAILURE);
  }
}


void acceptHandler(int lsd, struct sockaddr_in cad) {
  //TODO: fix alen error
  int alen = sizeof(cad);
  int index = -1;
  // loop to find empty index in participants array
  for(int i = 0; i <= NUMCLIENTS; i++) {
    //if we found an empty space
    if (sdp[i] < 0) {
      //set index and leave loop
      index = i;
      i = NUMCLIENTS;
    }
  }

  //TODO: can't just exit if fails
  if(index >= 0) {
    if ((sdp[index] = accept(lsd, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      sdp[index] =-1;
    }
  }
}

void enqueue(struct node* newnode){
  if(requestqueue.first == NULL && requestqueue.last ==NULL){
    requestqueue.first = newnode;
    requestqueue.last = newnode;
  }
    //else place after last item in queue
  else{
    requestqueue.last->nextnode = newnode;
    requestqueue.last = newnode;
  }
}

struct node* dequeue(){
  //grab the front of the queue
  struct node * temp = requestqueue.first;
  //if the queue is empty return NULL
  if(requestqueue.first ==NULL) return NULL;
  //if there is only one node in queue
  if(requestqueue.first ==requestqueue.last){
    requestqueue.first = NULL;
    requestqueue.last = NULL;
  }
    //else just move the front pointer to the next node
  else{
    requestqueue.first = requestqueue.first->nextnode;
  }

  return temp;

}

void findReadySockets(fd_set set, int n){

  /*go through set
   * for each item in set
   * check if is_set
   *  add to queue
   * else
   *  move to next item
   *
   * */
  int i;
  int numfound = 0;

  //check if listen participant sd is ready
  if(FD_ISSET(lsdp, &set)){
    struct node *temp = (struct node *)malloc(sizeof(struct node));
    temp->socketDes = lsdp;
    enqueue(temp);
    numfound++;
  }

  //check if listen observer sd is ready
  if(FD_ISSET(lsdo,&set)){
    struct node *temp = (struct node *)malloc(sizeof(struct node));
    temp ->socketDes = lsdo;
    enqueue(temp);
    numfound++;
  }

  //iterate over the number of total possible clients until we find n ready socket descriptors
  for(i = 0; numfound < n && i < NUMCLIENTS; i++ ){
    //check each participant socket descriptor
    if(FD_ISSET(sdp[i],&set)){
      //create and add a new node to the request queue
      struct node *temp = (struct node *)malloc(sizeof(struct node));
      temp ->socketDes = sdp[i];
      enqueue(temp);
    }
    //check each observer socket descriptor
    if(FD_ISSET(sdo[i],&set)){
      //create and add a new node to the request queue
      struct node *temp = (struct node *)malloc(sizeof(struct node));
      temp ->socketDes = sdo[i];
      enqueue(temp);
    }
  }
}

//main function, mostly connection logic
int main(int argc, char **argv) {
  struct protoent *ptrp; /* pointer to a protocol table entry */
  struct sockaddr_in sad; /* structure to hold server's address */
  struct sockaddr_in cad; /* structure to hold client's address */


  //TODO: note that we can't keep an index of empty spots because one could have disconnected.
  //int sdp[255]; /* socket descriptors for participants */
  //int sdo[255]; /* socket descriptors for observers */

  int pPort; /* participant port number */
  int oPort; /* observer port number */
  int alen; /* length of address */
  int optval = 1; /* boolean value when we set socket option */
  char buf[1000]; /* buffer for string the server sends */
  pPort = atoi(argv[1]); /* convert argument to binary */
  oPort = atoi(argv[2]); /* convert argument to binary */
  struct node *temp;

  if( argc != 3 ) {
    fprintf(stderr,"Error: Wrong number of arguments\n");
    fprintf(stderr,"usage:\n");
    fprintf(stderr,"./prog2_server participants_port observers_port\n");
    exit(EXIT_FAILURE);
  }

  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */

  //initalize all of the socket descriptors for participants and clients
  memset(sdp,-1,sizeof(sdp));
  memset(sdo,-1,sizeof(sdo));

  //: Set socket family to AF_INET
  sad.sin_family = AF_INET;

  // Set local IP address to listen to all IP addresses this server can assume. You can do it by using INADDR_ANY
  sad.sin_addr.s_addr = INADDR_ANY;


  if (pPort > 0) { /* test for illegal value */
    // set port number. The data type is u_short
    sad.sin_port = htons(pPort);
  } else { /* print error message and exit */
    fprintf(stderr,"Error: Bad port number %s\n",argv[1]);
    exit(EXIT_FAILURE);
  }

  /* Map TCP transport protocol name to protocol number */
  if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
    fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
    exit(EXIT_FAILURE);
  }

  /*  Create a socket with AF_INET as domain, protocol type as
  SOCK_STREAM, and protocol as ptrp->p_proto. This call returns a socket
  descriptor named sd. */
  lsdp = socket(AF_INET, SOCK_STREAM, ptrp->p_proto);
  if (lsdp < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  /* Allow reuse of port - avoid "Bind failed" issues */
  if( setsockopt(lsdp, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
    fprintf(stderr, "Error Setting socket option failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Bind a local address to the socket. For this, you need to
  pass correct parameters to the bind function. */
  if (bind(lsdp, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
    fprintf(stderr,"Error: Bind failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Specify size of request queue. Listen take 2 parameters --
  socket descriptor and QLEN, which has been set at the top of this code. */
  if (listen(lsdp, QLEN) < 0) {
    fprintf(stderr,"Error: Listen failed\n");
    exit(EXIT_FAILURE);
  }

  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */

  //: Set socket family to AF_INET
  sad.sin_family = AF_INET;

  // Set local IP address to listen to all IP addresses this server can assume. You can do it by using INADDR_ANY
  sad.sin_addr.s_addr = INADDR_ANY;


  if (oPort > 0) { /* test for illegal value */
    // set port number. The data type is u_short
    sad.sin_port = htons(oPort);
  } else { /* print error message and exit */
    fprintf(stderr,"Error: Bad port number %s\n",argv[1]);
    exit(EXIT_FAILURE);
  }

  /* Map TCP transport protocol name to protocol number */
  if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
    fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
    exit(EXIT_FAILURE);
  }

  /*  Create a socket with AF_INET as domain, protocol type as
  SOCK_STREAM, and protocol as ptrp->p_proto. This call returns a socket
  descriptor named sd. */
  lsdo = socket(AF_INET, SOCK_STREAM, ptrp->p_proto);
  if (lsdo < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  /* Allow reuse of port - avoid "Bind failed" issues */
  if( setsockopt(lsdo, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
    fprintf(stderr, "Error Setting socket option failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Bind a local address to the socket. For this, you need to
  pass correct parameters to the bind function. */
  if (bind(lsdo, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
    fprintf(stderr,"Error: Bind failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Specify size of request queue. Listen take 2 parameters --
  socket descriptor and QLEN, which has been set at the top of this code. */
  if (listen(lsdo, QLEN) < 0) {
    fprintf(stderr,"Error: Listen failed\n");
    exit(EXIT_FAILURE);
  }


  //TODO: fix from here forward
  fd_set set;

  //struct timeval timeout = {sec,0};
  int n; //return value, if we timed out or not
  FD_ZERO(&set);
  FD_SET(lsdp,&set);
  FD_SET(lsdo,&set);
  int maxSD = 0;
  if(lsdp > lsdo) {
    maxSD = lsdp;
  } else {
    maxSD = lsdo;
  }

  int visits = 0;
  /* Main server loop - accept and handle requests */
  while (1) {

    fprintf(stderr,"SELECT\n");
    n =  select(maxSD+1,&set,NULL,NULL,NULL); //is there anything to read in time

    //iterate and find n sd's put in queue grab from queue

    findReadySockets(set,n);

    //loop until queue is empty
    while((temp = dequeue()) != NULL) {

        visits++;
        fprintf(stderr,"VISIT number:%d\n", visits );

        int activeSd = temp->socketDes;
        fprintf(stderr,"Active SD:%d\n", activeSd );
        fprintf(stderr,"LSDO:%d\n", lsdo);
        //if the current sd is the participants listening one, negotiate a new connection
        if (activeSd == lsdp) {
            acceptHandler(lsdp,cad);
        }
            //if the current sd is the observers listening one, negotiate a new connection
        else if (activeSd == lsdo) {
          fprintf(stderr,"I'm in.\n");
          acceptHandler(lsdo, cad);
        }

        else{
            //message logic
            //handle disconnects
        }
    }
/* cases must be constants
    switch (activeSd) {
      case lsdp:

        alen = sizeof(cad);
        if ((sd2 = accept(sd, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
          fprintf(stderr, "Error: Accept failed\n");
          exit(EXIT_FAILURE);
        }

        break;
      case lsdo:

        break;
      default:
          //handle chat messages and disconnects
    }*/

  }
  //at end of game close the socket and exit
  exit(EXIT_SUCCESS);
}
