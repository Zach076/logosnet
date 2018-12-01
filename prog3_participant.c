/* CSCI 367 LogosNet: prog3_participant.c
*
* 26 NOV 2018, Zach Richardson and Mitch Kimball
*/

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXMSGSIZE 1000
#define TRUE 1
#define FALSE 0

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
      //if error is not fixable, disconnect from server, and exit
      if(errno != ENOBUFS && errno != ENOMEM) {
        close(sd);
        exit(EXIT_FAILURE);
      }
    }
  }
}

//reads from stdin within a certain time frame,
//returning if the timeout was reached or not
int reader(char* buf, uint8_t sec) {
  fd_set set;
  struct timeval timeout = {sec,0}; //set turn timer
  int n; //return value, if we timed out or not
  FD_ZERO(&set);
  FD_SET(0,&set);
  n =  select(1,&set,NULL,NULL,&timeout); //is there anything to read in time
  if(n == 0){
    //timeout
    n = 0;
  } else if(n == -1) {
    //error
    n = 0;
  } else {
    //get word
    fgets(buf,MAXMSGSIZE,stdin);
    n = 1;
  }
  return n;
}

//recieves data drom a send, storing the data of length len to buf
//prints error if recieve fails
void recieve(int sd, void* buf, char* error) {
  //TODO: recieve length then message
  ssize_t n;
  uint8_t length;
  //recieve length
  n = recv(sd, &length, sizeof(uint8_t), MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != sizeof(uint8_t)) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    close(sd);
    sd = -1;
  }

  n = recv(sd, buf, length, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != length) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    close(sd);
    sd = -1;
  }
}

//main function, mostly connection logic
int main( int argc, char **argv) {
  struct hostent *ptrh; /* pointer to a host table entry */
  struct protoent *ptrp; /* pointer to a protocol table entry */
  struct sockaddr_in sad; /* structure to hold an IP address */
  int sd; /* socket descriptor */
  int port; /* protocol port number */
  char *host; /* pointer to host name */
  int n; /* number of characters read */
  char buf[1000]; /* buffer for data from the server */
  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
  sad.sin_family = AF_INET; /* set family to Internet */

  if( argc != 3 ) {
    fprintf(stderr,"Error: Wrong number of arguments\n");
    fprintf(stderr,"usage:\n");
    fprintf(stderr,"./prog1_client server_address server_port\n");
    exit(EXIT_FAILURE);
  }

  port = atoi(argv[2]); /* convert to binary */
  if (port > 0) { /* test for legal value */
    sad.sin_port = htons((u_short)port);
  } else {
    fprintf(stderr,"Error: bad port number %s\n",argv[2]);
    exit(EXIT_FAILURE);
  }

  host = argv[1]; /* if host argument specified */

  /* Convert host name to equivalent IP address and copy to sad. */
  ptrh = gethostbyname(host);
  if ( ptrh == NULL ) {
    fprintf(stderr,"Error: Invalid host: %s\n", host);
    exit(EXIT_FAILURE);
  }

  memcpy(&sad.sin_addr, ptrh->h_addr, ptrh->h_length);

  /* Map TCP transport protocol name to protocol number. */
  if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
    fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
    exit(EXIT_FAILURE);
  }

  /* Create a socket. */
  sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
  if (sd < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  /* Connect the socket to the specified server. You have to pass correct parameters to the connect function.*/
  if (connect(sd, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
    fprintf(stderr,"connect failed\n");
    exit(EXIT_FAILURE);
  }

  //TODO: fix from here forward
  char* quit = "/quit";
  int done = FALSE;
  //buffer for username and sending messages
  char buf[1000];
  memset(&buf, 0, sizeof(buf));

  while(!done) {
    reader(buf, 60);
    if(strlen(buf) < 10) {
      betterSend(sd, buf, sizeof(buf));

      recieve(sd, buf, "username verification");
      if(buf == 'Y') {
        done = TRUE;
      }
    }
  }

  done = FALSE;
  while(!done) {
    reader(buf, NULL);

    betterSend(sd, buf, sizeof(buf));
    if(strcmp(buf, quit) == 0) {
      done = TRUE;
    }
  }

  //in case real bad things happen still close the socket and exit nicely
  close(sd);

  exit(EXIT_SUCCESS);
}
