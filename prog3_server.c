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
#define PARTICIPANT 1
#define OBSERVER 0
#define TIMEOUT 10

int sdp[255]; /* socket descriptors for participants */
int sdo[255]; /* socket descriptors for observers */
time_t oStart[255]; //array of times when an observer joins
time_t oEnd[255]; //array of times when an observer connects after initial connection
int tempSD; //to send clients an 'N' if all slots are taken
int lsdp; //listening socket descriptor for participants
int lsdo; //listening socket descriptor for observers
fd_set set; //set of sockets to listen to

struct userPair{ //struct to hold an observer and a participant and relevant information
  int participantSD;
  int observerSD;
  char username[11];
  time_t startTime;
  time_t connectTime;
} userList[255];

struct node{ //node for queues to store a message's socket descriptor index in the global array and the next node in the queue
  int socketDes;
  int socketIndex;
  struct node *nextnode;
};

struct queue{ //queue for incoming messages
  struct node *first;
  struct node *last;
}requestqueue;

//prototype to use in disconnect
void broadcast(char* buf);

//properly disconnects a client, and its corresponding observer if an active participant
void disconnect(int index, int type) {
  //check if observer belongs to a participant
  int found = FALSE;

  //if participant, clear the struct username and participantSD
  if(type == PARTICIPANT) {
    //variables to broadcast disconnect message
    char* user = "User ";
    char* hasLeft = " has left\n";
    char messageBuf[26];
    memset(messageBuf,0,sizeof(messageBuf));

    //close the client sd, set to -1, and reset the userList struct
    close(sdp[index]);
    sdp[index] = -1;
    userList[index].participantSD = 0;

    //create disconnect message
    strncat(messageBuf, user, sizeof(messageBuf) - strlen(messageBuf) - 1);
    strncat(messageBuf, userList[index].username, sizeof(messageBuf) - strlen(messageBuf) - 1);
    strncat(messageBuf, hasLeft, sizeof(messageBuf) - strlen(messageBuf) - 1);

    //if participant is active, send disconnect message
    if(strcmp(userList[index].username,"") != 0) {
      broadcast(messageBuf);
    }

    //clear username
    memset(&userList[index].username, 0, sizeof(userList[index].username));
  }

  //if participant, or just observer
  //clear sdo and userList of observerSD
  for(int i = 0; i < NUMCLIENTS; i++) {
    //if observer is in a pair
    if(userList[index].observerSD == sdo[i]) {
      //close and remove from pair, reset struct
      close(sdo[i]);
      sdo[i] = -1;
      i = NUMCLIENTS;
      userList[index].observerSD = 0;
      found = TRUE;
    }
  }
  //if observer is not in a pair
  if(!found) {
    //close and reset socket descriptor
    close(sdo[index]);
    sdo[index] = -1;
  }

}

//sends len then buf data to sd and if theres a fixable error,
//try to send again, otherwise exit nicely
void betterSend(int sd, void* buf, uint8_t len, int index, int type) {

  ssize_t n = -1;
  //while errors occur
  while(n == -1) {
    //try to send length
    n = send(sd, &len, sizeof(uint8_t), 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect client
      if(errno != ENOBUFS && errno != ENOMEM) {
        disconnect(index, type);
      }
    }
  }
  n = -1;
  while(n == -1) {
    //try to send data
    n = send(sd, buf, len, 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect and exit
      if(errno != ENOBUFS && errno != ENOMEM) {
        disconnect(index, type);
      }
    }
  }
}

//betterSend but larger length for chat messages
void bigSend(int sd, void* buf, uint16_t len, int index, int type) {
  ssize_t n = -1;
  //while errors occur
  while(n == -1) {
    len = htons(len);
    //try to send length
    n = send(sd, &len, sizeof(uint16_t), 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect client
      if(errno != ENOBUFS && errno != ENOMEM) {
        disconnect(index, type);
      }
    }
  }
  n =-1;
  while(n == -1) {
    //reset length to be properly used
    len = ntohs(len);
    //try to send data
    n = send(sd, (char *)buf, len, 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect and exit
      if(errno != ENOBUFS && errno != ENOMEM) {
        disconnect(index, type);
      }
    }
  }
}

//recieves len then buf data from sd and if it breaks, print error
void recieve(int sd, void* buf, char* error, int index, int type) {
  ssize_t n;
  uint8_t length;
  memset(buf,0,sizeof(buf));
  //recieve length
  n = recv(sd, &length, sizeof(uint8_t), MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != sizeof(uint8_t)) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    disconnect(index, type);
  }
  //recieve data
  n = recv(sd, buf, length, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != length) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    disconnect(index, type);
  }
}

//recieve but larger length for chat messages
void bigRecieve(int sd, void* buf, char* error, int index, int type) {
  ssize_t n;
  uint16_t length;
  //recieve length
  memset(buf,0,sizeof(buf));
  n = recv(sd, &length, sizeof(uint16_t), MSG_WAITALL);
  length = ntohs(length);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != sizeof(uint16_t)) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    disconnect(index, type);
  }
  //recieve data
  n = recv(sd, buf, length, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != length) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    disconnect(index, type);
  }
}

//broadcast sends messages to all observers currently in the chat room.
void broadcast(char* buf) {
  for(int i = 0; i < NUMCLIENTS;i++) {
    if(userList[i].observerSD > 0) {
      bigSend(userList[i].observerSD, buf, (uint16_t) strlen(buf), i, OBSERVER);
    }
  }
}
//privateMsg is used to handle any private message sent in the chat room
//username is the destination user, buf stores their message and index is the index of the sender in global array
void privateMsg(char* username, char* buf, int index) {
  //flag to determine if user with given username exists
  int sent = FALSE;
  //buffer for user not found message
  char newBuf[41];
  memset(newBuf, 0, sizeof(newBuf));
  //search through the list of users for the given username
  for(int i = 0; i < NUMCLIENTS;i++) {
    //if we found the username send the private message to their observer.
    if(strcmp(userList[i].username, username) == 0) {
      if(userList[i].observerSD != 0) {
        bigSend(userList[i].observerSD, buf, strlen(buf), index, OBSERVER);
      }
      //end the loop early because we have found the user
      i = NUMCLIENTS;
      sent = TRUE;
    }
  }
  //if we didn't find the user then send the does not exist message to user
  if(!sent) {
    strncat(newBuf,"Warning: user ", sizeof(newBuf) - strlen(newBuf) - 1);
    strncat(newBuf, username, sizeof(newBuf) - strlen(newBuf) - 1);
    strncat(newBuf, "doesn't exist...\n", sizeof(newBuf) - strlen(newBuf) - 1);
    //check if the user has an observer to send message to
    if(userList[index].observerSD != 0) {
      bigSend(userList[index].observerSD, newBuf, strlen(newBuf), index, OBSERVER);
    }
  } else {//if we found the user then we also send the private message to the observer of the user that sent it
    if(userList[index].observerSD != 0) {
      bigSend(userList[index].observerSD, buf, strlen(buf), index, OBSERVER);
    }
  }
}

//handles incoming messages
void msgHandler(int index) {
  char username[11]; //username buffer
  char buf[1000]; //message buffer
  char newBuf[1014]; //message buffer with preppended message
  memset(buf,0,sizeof(buf));
  memset(newBuf,0,sizeof(newBuf));
  memset(username, 0 , sizeof(username));
  //set preppended message
  newBuf[0] = '>';
  for(int i = 0; i < 11-strlen(userList[index].username); i++) {
    newBuf[i+1] = ' ';
  }
  strncat(newBuf, userList[index].username, sizeof(newBuf) - strlen(newBuf) - 1);
  newBuf[12] = ':';
  newBuf[13] = ' ';

  //recieve message
  bigRecieve(sdp[index], buf, "Message", index, PARTICIPANT);

  //place message after preppended message
  strncat(newBuf, buf, sizeof(newBuf) - strlen(newBuf) - 1);

  //if private message, loop through the recipient and copy username
  if(buf[0] == '@') {
    for(int i = 1; i < 12; i++) {
      if(buf[i] != ' ') {
        username[i-1] = buf[i];
      } else {
        i = 12;
      }
    }
    //send private message
    privateMsg(username, newBuf, index);
  } else if(strcmp(buf, "/quit\n") && strcmp(buf,"")){ //if client typed something other than /quit
    //broadcast message
    broadcast(newBuf);
  } else {
    //else they typed quit and we disconnect
    disconnect(index, PARTICIPANT);
  }
}

//small function to validate incoming usernames from observers
int validObserverUsername(char* buf) {
  int taken = -1;
  int i = 0;
  //loop through all other usernames to check if taken
  for(i = 0; i < 256; i++) {
    if(!strcmp(buf,userList[i].username)) {
      taken = i;
      i = 256;
    }
  }
  //return the index we found the username
  return taken;
}

//validates incoming usernames of participants
int validUsername(char* buf) {
  int taken = FALSE;
  int done = FALSE;
  int i = 0;
  int valid = TRUE;
  //while reading the username
  while(!done){
    if(buf[i] > 64 && buf[i] < 91) { //if were uppercase A-Z
      i++; //increment
    } else if (buf[i] > 96 && buf[i] < 123) { //if were lowercase a-z
      i++;
    } else if(buf[i] > 47 && buf[i] < 58) { // if were numbers
      i++;
    } else if(buf[i] == 95) { //if were a space
      i++;
    } else if(buf[i] == 0) { //if null terminator
      done = TRUE; //no increment were done
    } else { //if invalid character
      valid = FALSE;
      done = TRUE;
    }
  }

  if(valid) {
    //loop through usernames to check its not taken
    for(i = 0; i < 256; i++) {
      if(!strcmp(buf,userList[i].username)) {
        taken = TRUE;
        i = 256;
      }
    }
    //if username is taken
    if(taken) {
      //return 0
      i = 0;
    } else {
      //if username is available return true
      i = 1;
    }
  } else {
    //if not valid return -1
    i = -1;
  }
  return i;
}

//starts negotiation of usernames between server and clients
int usernameLogic(int index, int type) {

  char buf[11];//username buffer
  memset(buf,0,sizeof(buf));
  char taken = 'T';//taken flag sent to client
  char valid = 'Y';//valid flag sent to client
  char invalid = 'I';//invalid flag sent to client
  char discon = 'N';//disconnect flag sent to client
  char* error = "Username";//error type possible
  int validUName;//flag for if the username is valid
  int i;//iterator
  char* user = "User ";//user has joined string
  char* hasJoined = " has joined\n";
  char messageBuf[28];//buffer for the has joined message
  memset(messageBuf,0,sizeof(messageBuf));

  //if PARTICIPANT
  if(type == PARTICIPANT) {
    //recieve username and validate it
    recieve(sdp[index], buf, error, index, type);
    validUName = validUsername(buf);
    //if username is valid
    if (validUName == TRUE) {
      //send valid flag and add username to active user list
      betterSend(sdp[index], &valid, sizeof(char), index, type);
      for (i = 0; i < strlen(buf); i++) {
        userList[index].username[i] = buf[i];
      }
      //set has joined message
      strncat(messageBuf, user, sizeof(messageBuf) - strlen(messageBuf) - 1);
      strncat(messageBuf, buf, sizeof(messageBuf) - strlen(messageBuf) - 1);
      strncat(messageBuf, hasJoined, sizeof(messageBuf) - strlen(messageBuf) - 1);
      //broadcast has joined message
      broadcast(messageBuf);
    } else if (validUName == FALSE) { //if username is taken
      //send taken flag and reset username timeout
      betterSend(sdp[index], &taken, sizeof(char), index, type);
      userList[index].startTime = time(&userList[index].startTime);
    } else { //if username is invalid
      //send invalid flag
      betterSend(sdp[index], &invalid, sizeof(char), index, type);
    }
  } else { //if we're an observer
    //recieve username check validity
    recieve(sdo[index], buf, error, index, type);
    i = validObserverUsername(buf);

    //if username is valid
    if (i >= 0) {
      if(userList[i].observerSD == 0) { //if username isnt taken
        //send valid flag
        betterSend(sdo[index], &valid, sizeof(char), index, type);
        //give observer its pair
        userList[i].observerSD = sdo[index];
        //broadcast an observer has joined
        broadcast("A new observer has joined\n");
      } else { //if username is taken
        //send taken flag and reset username timeout
        betterSend(sdo[index], &taken, sizeof(char), index, type);
        oStart[index] = time( &oStart[index] );
      }
    } else { //if invalid
      //send invalid flag and disconnect
      betterSend(sdo[index], &discon, sizeof(char), index, type);
      disconnect(index, type);
    }

  }
}

//handles incoming connections
void acceptHandler(struct sockaddr_in cad, int type) {
  char valid = 'Y'; //vacancy flag
  char invalid = 'N'; //no vacancy flag
  int alen = sizeof(cad);
  int index = -1;
  // loop to find empty index in participants array
  for(int i = 0; i <= NUMCLIENTS; i++) {
    //if we found an empty space
    if (type && sdp[i] < 0) {
      //set index and leave loop
      index = i;
      i = NUMCLIENTS;
    } else if(!type && sdo[i] < 0){
      //set index and leave loop
      index = i;
      i = NUMCLIENTS;
    }
  }
  //if we found an open index
  if(index >= 0) {
    //if participant
    if(type) {
      //try and accept new connection for participant
      if ((sdp[index] = accept(lsdp, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
        fprintf(stderr, "Error: Accept failed\n");
        sdp[index] =-1;
      }
      //if the accept succeeded send 'Y'
      else {
        //send char 'Y' place into userList
        betterSend(sdp[index], &valid, 1, index, type);
        //give pair
        userList[index].participantSD = sdp[index];
        memset(userList[index].username,0,sizeof(userList[index].username));
        //set username timeout start time
        userList[index].startTime = time( &userList[index].startTime );

      }
    }
    //if observer
    else if(!type) {
      //if connection fails
      if ((sdo[index] = accept(lsdo, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
        fprintf(stderr, "Error: Accept failed\n");
        sdo[index] =-1;
      } else {
        //send char 'Y' and start timer to wait for username
        betterSend(sdo[index], &valid, 1, index, type);
        oStart[index] = time ( &oStart[index] );
      }
    }
  }
  //else if no vacancy and participant
  else if(type) {
    if ((tempSD = accept(lsdp, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      tempSD =-1;
    } else {
      //send char 'N' and disconnect
      betterSend(tempSD, &invalid, 1, index, type);
      close(tempSD);
    }
  }
  //else if no vacancy and observer
  else if(!type) {
    if ((tempSD = accept(lsdo, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      tempSD =-1;
    } else {
      //send char 'N' and disconnect
      betterSend(tempSD, &invalid, 1, index, type);
      close(tempSD);
    }
  }
}

//place node into our select ready queue
void enqueue(struct node* newnode){
  //if newnode is the first node in the queue
  if(requestqueue.first == NULL && requestqueue.last ==NULL){
    //sets first and last pointer to be this node since queue size is 1
    requestqueue.first = newnode;
    requestqueue.last = newnode;
  }
  //else place after last item in queue
  else{
    requestqueue.last->nextnode = newnode;
    requestqueue.last = newnode;
  }
}

//Pop next select node from our select ready queue
struct node* dequeue(){
  //grab the front of the queue
  struct node * temp = requestqueue.first;
  //if the queue is empty return NULL
  if(requestqueue.first ==NULL) {
    return NULL;
  }
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

//search the fd set for any socket descriptors that are ready for handling
void findReadySockets(int n){
  int i;//iterator
  int numfound = 0;//count of ready socket descriptors

  //check if listen participant sd is ready
  if(FD_ISSET(lsdp, &set)){
    struct node *temp = (struct node *)malloc(sizeof(struct node));
    temp->socketDes = lsdp;
    temp->socketIndex = -1;
    enqueue(temp);
    numfound++;
  }

  //check if listen observer sd is ready
  if(FD_ISSET(lsdo,&set)){
    struct node *temp = (struct node *)malloc(sizeof(struct node));
    temp ->socketDes = lsdo;
    temp->socketIndex = -1;
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
      temp->socketIndex = i;
      //set connection time for participant
      userList[i].connectTime = time( &userList[i].connectTime );
      enqueue(temp);
    }
    //check each observer socket descriptor
    if(FD_ISSET(sdo[i],&set)){
      //create and add a new node to the request queue
      struct node *temp = (struct node *)malloc(sizeof(struct node));
      temp ->socketDes = sdo[i];
      temp->socketIndex = i;
      //set connection time for observers
      oEnd[i] = time( &oEnd[i] );
      enqueue(temp);
    }
  }
}

//function to remake fd set to listen to, returns max socket descriptor
int makeSet() {
  int i; //iterator variable
  int maxSD = 0; //variable to hold return value
  FD_ZERO(&set); //clear the set
  FD_SET(lsdp,&set); //add listening socket
  FD_SET(lsdo,&set); //add listening socket
  //for each client, place into the set
  for(i = 0; i < 256; i++) {
    if(sdp[i] > 0) {
      FD_SET(sdp[i],&set);
      //if new max socket descriptor
      if(sdp[i] > maxSD) {
        maxSD = sdp[i];
      }
    }
    if(sdo[i] > 0) {
      FD_SET(sdo[i],&set);
      //if new max socket descriptor
      if(sdo[i] > maxSD) {
        maxSD = sdo[i];
      }
    }
  }
  //if new max socket descriptor
  if(lsdp > maxSD) {
    maxSD = lsdp;
  }
  //if new max socket descriptor
  if(lsdo > maxSD) {
    maxSD = lsdo;
  }
  return maxSD;
}

//main function, mostly connection logic
int main(int argc, char **argv) {
  struct protoent *ptrp; /* pointer to a protocol table entry */
  struct sockaddr_in sad; /* structure to hold server's address */
  struct sockaddr_in cad; /* structure to hold client's address */

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



  int n; //return value, if we timed out or not
  int maxSD = 0; //max socket descriptor to be used in select
  /* Main server loop - accept and handle requests */
  while (1) {
    //make new set and set maxSD
    maxSD = makeSet();

    n =  select(maxSD+1,&set,NULL,NULL,NULL); //is there anything to read

    //iterate and find n sd's put in queue grab from queue
    findReadySockets(n);

    //loop until queue is empty
    while((temp = dequeue()) != NULL) {

      int activeSd = temp->socketDes;
      int activeIndex = temp->socketIndex;
      //if the current sd is the participants listening one, negotiate a new connection
      if (activeSd == lsdp) {
        acceptHandler(cad, PARTICIPANT);
      }
      //if the current sd is the observers listening one, negotiate a new connection
      else if (activeSd == lsdo) {
        acceptHandler(cad, OBSERVER);
      }
      // if participant
      else if(sdp[activeIndex] == activeSd){
        //if not active participant (no username)
        if(strcmp(userList[activeIndex].username,"") ==0){
          //check timestamps, if timed out disconnect
          if(difftime(userList[activeIndex].connectTime,userList[activeIndex].startTime) >= TIMEOUT) {
            disconnect(activeIndex, PARTICIPANT);
          } else {
            //negotiate user name
            usernameLogic(activeIndex, PARTICIPANT);
          }
        } else { //else the participant has a username already and it must be some message
          //handle message
          msgHandler(activeIndex);
        }
      } else if (sdo[activeIndex] == activeSd){ //if observer is ready for something
        //check timestamps, if timed out disconnect
        if(difftime(oEnd[activeIndex],oStart[activeIndex]) > TIMEOUT) {
          disconnect(activeIndex, OBSERVER);
        } else {
          //else we are clear to negotiate a username
          usernameLogic(activeIndex, OBSERVER);
        }
      }
    }
  }
  //somehow exited infinite loop, crash server
  exit(EXIT_FAILURE);
}
