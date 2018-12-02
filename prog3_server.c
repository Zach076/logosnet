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
int pvisits = 0; /* counts participant's connections */
int ovisits = 0; /* counts observers's connections */
int sdp[255]; /* socket descriptors for participants */
int sdo[255]; /* socket descriptors for observers */
time_t oStart[255];
time_t oEnd[255];
int tempSD; //to send clients an 'N' if all slots are taken
int lsdp; //listening socket descriptor for participants
int lsdo; //listening socket descriptor for observers
fd_set set;

struct userPair{
  int participantSD;
  int observerSD;
  char username[11];
  time_t startTime;
  time_t connectTime;
} userList[255];

//TODO: add a flag to determine which type of client this sd is
struct node{
  int socketDes;
  int socketIndex;
  struct node *nextnode;
};

struct queue{
  struct node *first;
  struct node *last;
}requestqueue;

void disconnect(int index, int type) {

  //if participant, clear the struct username and participantSD
  if(type == PARTICIPANT) {
    close(sdp[index]);
    sdp[index] = -1;
    userPair[index].participantSD = 0;
    memset(&userPair[index].username, 0, sizeof(userPair[index].username));
  }

  //if participant, or just observer
  //clear sdo and userList of observerSD
  for(int i = 0; i < NUMCLIENTS; i++) {
    if(userPair[index].observerSD == sdo[i]) {
      close(sdo[i]);
      sdo[i] = -1;
      i = NUMCLIENTS;
    }
  }
  userPair[index].observerSD = 0;
}

//sends data from buf of size len to sd and if theres a fixable error,
//try to send again, otherwise exit nicely
void betterSend(int sd, void* buf, uint8_t len) {

  //TODO:send length then message
  ssize_t n = -1;
  //while errors occur
  while(n == -1) {
    //try to send data
    n = send(sd, &len, sizeof(uint8_t), 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect client
      if(errno != ENOBUFS && errno != ENOMEM) {
        close(sd);//TODO: disconnect logic
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
        close(sd);
      }
    }
  }
}

void bigSend(int sd, void* buf, uint16_t len) {

  //TODO:send length then message
  ssize_t n = -1;
  //while errors occur
  while(n == -1) {

    len = htons(len);
    //try to send data
    n = send(sd, &len, sizeof(uint16_t), 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect client
      if(errno != ENOBUFS && errno != ENOMEM) {
        close(sd);
      }
    }
  }
  n =-1;
  while(n == -1) {
    buf = htons(buf);
    //try to send data
    n = send(sd, buf, len, 0);
    //if error occured
    if(n == -1) {
      //if error is not fixable, disconnect and exit
      if(errno != ENOBUFS && errno != ENOMEM) {
        close(sd);
      }
    }
  }
}

void recieve(int sd, void* buf, char* error) {
  //TODO: recieve length then message
  ssize_t n;
  uint8_t length;
  //recieve length
  n = recv(sd, &length, sizeof(uint8_t), MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != sizeof(uint8_t)) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    close(sd);//TODO: disconnect logic
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

void bigRecieve(int sd, void* buf, char* error) {
  //TODO: recieve length then message
  ssize_t n;
  uint16_t length;
  //recieve length
  n = recv(sd, &length, sizeof(uint16_t), MSG_WAITALL);
  length = ntohs(length);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != sizeof(uint16_t)) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    close(sd);//TODO: disconnect logic
    sd = -1;
  }

  n = recv(sd, buf, length, MSG_WAITALL);
  buf = ntohs(buf);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != length) {
    fprintf(stderr,"Read Error: %s not read properly from sd: %d\n", error, sd);
    close(sd);
    sd = -1;
  }
}

void broadcast(char* buf) {
  for(int i = 0; i < NUMCLIENTS;i++) {
    if(userPair[i].observerSD > 0) {
      bigSend(userPair[i].observerSD, buf, sizeof(buf));
    }
  }
}

void privateMsg(char* username, char* buf, int index) {
  int sent = FALSE;

  for(int i = 0; i < NUMCLIENTS;i++) {
    if(strcmp(userPair[i].username, username) == 0) {
      bigSend(userPair[i].observerSD, buf, sizeof(buf));
      i = NUMCLIENTS;
    }
  }
  if(!sent) {
    buf = "Warning: user " + username + "doesn't exist...";
    bigSend(userPair[index].observerSD, buf, sizeof(buf));
  } else {
    bigSend(userPair[index].observerSD, buf, sizeof(buf));
  }
}

void msgHandler(int index) {
  char username[11];

  char newBuf[1014];
  newBuf[0] = '>';
  for(int i = 0; i < 11-strlen(userList[index].username); i++) {
    newBuf[i] = ' ';
  }
  strcat(newBuf, userList[index].username);
  newBuf[12] = ':';
  newBuf[13] = ' ';

  memset(username, 0 , sizeof(username));
  memset(newBuf, 0 , sizeof(newBuf));

  bigRecieve(sdp[index], buf, "Message");

  strcat(newBuf, buf);

  if(buf[0] == '@') {
    for(int i = 1; i < 12; i++) {
      if(buf[i] != ' ') {
        username[i-1] = buf[i];
      } else {
        i = 12;
      }
    }
    privateMsg(username, newBuf, index);
  } else {
    broadcast(newBuf);
  }

}

int validObserverUsername(char* buf) {
    int taken = -1;
    int i = 0;

    for(i = 0; i < 256; i++) {
        if(!strcmp(buf,userList[i].username)) {
            taken = i;
            i = 256;
        }
    }

    return taken;
}

int validUsername(char* buf) {
  int taken = FALSE;
  int done = FALSE;
  int i = 0;
  int valid = TRUE;
  while(!done){
    //TODO: better comments
    if(buf[i] > 64 && buf[i] < 91) {
      //nothing
      i++;
    } else if (buf[i] > 96 && buf[i] < 123) {
      //nothing
      i++;
    } else if(buf[i] > 47 && buf[i] < 58) {
      //nothing
      i++;
    } else if(buf[i] == 95) {
      //nothing
      i++;
    } else if(buf[i] == 0) {
      done = TRUE;
    } else {
      valid = FALSE;
      done = TRUE;
    }
  }

  if(valid) {

    for(i = 0; i < 256; i++) {
      if(!strcmp(buf,userList[i].username)) {
        taken = TRUE;
        i = 256;
      }
    }

    if(taken) {
      return 0;
    } else {
      return 1;
    }

  } else {
    return -1;
  }
}
//TODO: can't use select anywhere else
int usernameLogic(int index, int type) {
  //TODO: check username length client side
  char buf[11];
  memset(buf,0,sizeof(buf));
  char taken = 'T';
  char valid = 'Y';
  char invalid = 'I';
  char discon = 'N';
  char* error = "Username";
  int validUName;
  int i;
  char user[28];
  user = "User ";
  char hasJoined = " has joined";

  if(type == PARTICIPANT) {
      recieve(sdp[index], buf, error);
      validUName = validUsername(buf);

      if (validUName == TRUE) {
          betterSend(sdp[index], &valid, sizeof(char));
          for (i = 0; i < strlen(buf); i++) {
              userList[index].username[i] = buf[i];
          }
          //userList[index].username[i] = 0;//TODO: don't need this?
          strcat(user, buf);
          strcat(user, hasJoined);
          broadcast(buf)
      } else if (validUName == FALSE) {
          betterSend(sdp[index], &taken, sizeof(char));
          userList[index].startTime = time(&userList[index].startTime);
      } else {
          betterSend(sdp[index], &invalid, sizeof(char));
      }
  } else {
      recieve(sdo[index], buf, error);
      i = validObserverUsername(buf);

      if (i >= 0) {
          if(userList[i].observerSD == 0) {
              betterSend(sdo[index], &valid, sizeof(char));
              userList[i].observerSD = sdo[index];
          } else {
              betterSend(sdo[index], &taken, sizeof(char));
              oStart[index] = time( &oStart[index] );
          }
      } else {
          betterSend(sdo[index], &discon, sizeof(char));
          //TODO: disconnect
      }

  }
}

void acceptHandler(struct sockaddr_in cad, int type) {
  char valid = 'Y';
  char invalid = 'N';
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
        //TODO debug
        fprintf(stderr, "This should print in acceptHandler: part 1\n");
        //send char 'Y' place into userList
        betterSend(sdp[index], &valid, 1);
        //TODO: move this to select loop
        //if(usernameLogic(60,sdp,index)) {
        //TODO remove
        //fprintf(stderr, "This should print in acceptHandler: part 2 electric boogaloo\n");

        //give pair
        userList[index].participantSD = sdp[index];
        memset(userList[index].username,0,sizeof(userList[index].username));
        userList[index].startTime = time( &userList[index].startTime );
        //}

      }
    }
    //if observer
    else if(!type) {
      //if connection fails
      if ((sdo[index] = accept(lsdo, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
        fprintf(stderr, "Error: Accept failed\n");
        sdo[index] =-1;
      } else {
        //send char 'Y' and ask for username
        //TODO: ask for username and match it.
        //TODO:what if participant already has an observer?
        betterSend(sdo[index], &valid, 1);
        oStart[index] = time ( &oStart[index] );
        //observer username logic.
        //TODO: move into select loop
        //observerUsernameLogic(60, sdo, index);
      }
    }
  }
  //else if no vacancy and participant
  else if(type) {
    if ((tempSD = accept(lsdp, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      tempSD =-1;
    } else {
      //send char 'N'
      betterSend(tempSD, &invalid, 1);
      close(tempSD);
    }
  }
  //else if no vacancy and observer
  else if(!type) {
    if ((tempSD = accept(lsdo, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      tempSD =-1;
    } else {
      //send char 'N'
      betterSend(tempSD, &invalid, 1);
      close(tempSD);
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

void findReadySockets(int n){

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
      userList[i].connectTime = time( &userList[i].connectTime );
      enqueue(temp);
    }
    //check each observer socket descriptor
    if(FD_ISSET(sdo[i],&set)){
      //create and add a new node to the request queue
      struct node *temp = (struct node *)malloc(sizeof(struct node));
      temp ->socketDes = sdo[i];
      temp->socketIndex = i;
      oEnd[i] = time( &oEnd[i] );
      enqueue(temp);
    }
  }
}

int makeSet() {
  int i;
  int maxSD = 0;
  FD_ZERO(&set);
  FD_SET(lsdp,&set);
  FD_SET(lsdo,&set);
  for(i = 0; i < 256; i++) {
    if(sdp[i] > 0) {

      FD_SET(sdp[i],&set);
      if(sdp[i] > maxSD) {
        maxSD = sdp[i];
      }
    }
    if(sdo[i] > 0) {
      FD_SET(sdo[i],&set);
      if(sdo[i] > maxSD) {
        maxSD = sdo[i];
      }
    }
  }
  if(lsdp > maxSD) {
    maxSD = lsdp;
  }
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
  //fd_set set;

  //struct timeval timeout = {sec,0};
  int n; //return value, if we timed out or not
  int maxSD = 0;
  int visits = 0;
  /* Main server loop - accept and handle requests */
  while (1) {

    maxSD = makeSet();

    fprintf(stderr,"SELECT, LSDP:%d LSDO:%d maxSD:%d\n", lsdp, lsdo, maxSD);
    n =  select(maxSD+1,&set,NULL,NULL,NULL); //is there anything to read in time

    //iterate and find n sd's put in queue grab from queue

    findReadySockets(n);

    //loop until queue is empty
    while((temp = dequeue()) != NULL) {

      visits++;
      fprintf(stderr,"VISIT number:%d\n", visits );

      int activeSd = temp->socketDes;
      int activeIndex = temp->socketIndex;
      fprintf(stderr,"Active SD:%d\n", activeSd );
      //if the current sd is the participants listening one, negotiate a new connection
      if (activeSd == lsdp) {
        fprintf(stderr,"lsdp\n");
        acceptHandler(cad, PARTICIPANT);
      }
      //if the current sd is the observers listening one, negotiate a new connection
      else if (activeSd == lsdo) {
        fprintf(stderr,"lsdo\n");
        acceptHandler(cad, OBSERVER);
      }
      // if participant
      else if(sdp[activeIndex] == activeSd){
        //if not active participant
        if(strcmp(userList[activeIndex].username,"") ==0){
          //check timestamps
          if(difftime(userList[activeIndex].connectTime,userList[activeIndex].startTime) > 600) {
            //disconnect it
          } else {
            //negotiate user name
            usernameLogic(activeIndex, PARTICIPANT);
            //TODO: broadcast username message to all observers
          }
        }
        //else the participant has a username already and it must be some message
        else{
          //check if disconnected in recieve
          //recieve message from active participant
          //broadcast to all observers
          msgHandler(activeIndex);
        }
      }

      //if observer is ready for something
      else if (sdo[activeIndex] == activeSd){

        //observer username logic
        //check if observer took to long
        if(difftime(oEnd[activeIndex],oStart[activeIndex]) > 600) {
          //disconnect it
        } else {
          //else we are negotiating a username
          usernameLogic(activeIndex, OBSERVER);
        }
      }
    }
  }
  //shouldn't get here but ok
  exit(EXIT_SUCCESS);
}
