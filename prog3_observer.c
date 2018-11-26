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
int reader(char* guess, uint8_t sec) {
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
    fgets(guess,MAXWORDSIZE,stdin);
    n = 1;
  }
  return n;
}

//recieves data drom a send, storing the data of length len to buf
//prints error if recieve fails
void recieve(int sd, void* buf, size_t len, char* error) {
  ssize_t n;
  //recieve data
  n = recv(sd, buf, len, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect from server, and exit
  if (n != len) {
    fprintf(stderr,"Read Error: %s Score not read properly\n", error);
    close(sd);
    exit(EXIT_FAILURE);
  }
}

//keeps 2 players in a round of the game until one loses the round
void turnHandler(int sd, uint8_t turnTime) {
  char turnFlag; //flag fow whose turn it is
  uint8_t guessSize;
  char guess[MAXWORDSIZE];
  uint8_t isCorrect; //flag if we guessed correctly
  uint8_t timeoutFlag = FALSE; //flag for if weve timed out
  int done = FALSE; //flag for round running

  //while the round hasnt been won
  while(!done) {

    //null terminate the guess
    memset(guess,0,sizeof(guess));

    //recieve whose turn it is
    recieve(sd, &turnFlag, sizeof(turnFlag), "Turn flag");

    //if its our turn
    if(turnFlag == 'Y') {
      fprintf(stderr,"Your turn, enter a word: ");
      //if we dont guess in time
      if (!reader(guess, turnTime)) {
        //set flag
        timeoutFlag = TRUE;
        betterSend(sd,&timeoutFlag,sizeof(timeoutFlag));
      }
      //null terminate the guess
      guess[strlen(guess)-1]=0;

      guessSize = (uint8_t)strlen(guess);
      //send the timeoutFlag, the guessSize, and the guess
      betterSend(sd,&timeoutFlag,sizeof(timeoutFlag));
      betterSend(sd,&guessSize,sizeof(guessSize));
      betterSend(sd,guess,guessSize);

      //recieve if we guessed correctly
      recieve(sd,&isCorrect,sizeof(isCorrect), "isCorrect");

      //if the guess is correct
      if(isCorrect == 1){
        printf("Valid word\n");
      } else if(timeoutFlag){ //if we timed out
        //print accordingly and set done true
        printf("\nTimed out\n");
        done = TRUE;
      } else {
        //print accordingly and set done true
        printf("Invalid word\n");
        done = TRUE;
      }

    } else if(turnFlag == 'N') { //if not our turn
      // print to wait for opponents guess
      printf("Please wait for opponent to enter word... \n");

      //recieve if opponent guessed correctly
      recieve(sd,&isCorrect,sizeof(isCorrect),"isCorrect");

      //if opponent guessed correctly
      if(isCorrect == 1){
        //recieve the guess size and the guess
        recieve(sd, &guessSize, sizeof(guessSize), "Guess size");
        recieve(sd, guess, guessSize, "Guess");

        //print what they guessed
        printf("Opponent entered \"%s\" \n",guess);
      } else {
        //print accordingly and set done true
        printf("Opponent lost the round!\n");
        done = TRUE;
      }
    }
  }
}

//main game function
//keeps 2 players in the game for up to 5 rounds, calling turnHandler for each
//once a player gets 3 points, the game ends
void playGame(int sd, char playerNum, uint8_t boardSize, uint8_t turnTime) {
  char guessBuffer[MAXWORDSIZE+1];
  char board[boardSize+1];
  uint8_t player1Score = 0;
  uint8_t player2Score = 0;
  uint8_t roundNum = 0;
  uint8_t lastRound = 0;
  int i; //for loop
  int done = FALSE; //flag for if game is being played

  //null terminate the board and the guess
  memset(guessBuffer,0,sizeof(guessBuffer));
  memset(board,0,sizeof(board));

  //while were in the game
  while(!done) {
    //R.1 (recieve player scores)
    recieve(sd, &player1Score, sizeof(player1Score), "Player1 Score");
    recieve(sd, &player2Score, sizeof(player2Score), "Player2 Score");
    //R.2 (recieve round number)
    recieve(sd, &roundNum, sizeof(roundNum), "Round number");
    //R.4 (recieve the board)
    recieve(sd, board, boardSize, "Board");

    //if someone hasnt won
    if(player1Score < 3 && player2Score < 3) {
      //print round number
      printf("\nRound %d... \n", roundNum);

      //print score accordingly
      if (playerNum == '1') {
        printf("Score is %d-%d\n", player1Score, player2Score);
      } else {
        printf("Score is %d-%d\n", player2Score, player1Score);
      }
      //R.4 (print board)
      printf("Board:");
      for (i = 0; i < boardSize; i++) {
        printf(" %c", board[i]);
      }
      printf("\n");

      //play a round
      turnHandler(sd, turnTime);
    } else {
      //round over, someone won
      done = TRUE;
    }
  }

  //print win/loss accordingly
  if (player1Score == 3) {
    if (playerNum == '1') {
      printf("You won\n");
    } else {
      printf("You lost\n");
    }
  } else {
    if (playerNum == '2') {
      printf("You won\n");
    } else {
      printf("You lost\n");
    }
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

  //read the player number, print error if one occurs
  n = read(sd,&playerNum,sizeof(playerNum));
  if(n != sizeof(playerNum)){
    fprintf(stderr,"Read Error: playerNum not read properly");
    exit(EXIT_FAILURE);
  }
  //print which player we are
  if(playerNum == '1') {
    printf("You are Player 1... the game will begin when Player 2 joins...\n");
  } else {
    printf("You are Player 2... \n");
  }

  //read the board size, print error if one occurs
  n = read(sd,&boardSize,sizeof(boardSize));
  if(n != sizeof(boardSize)){
    fprintf(stderr,"Read Error: boardSize not read properly");
    exit(EXIT_FAILURE);
  }
  //print board size
  printf("Board size : %d \n",boardSize);

  //read turn time, print error if one occurs
  n = read(sd,&turnTime,sizeof(turnTime));
  if(n != sizeof(turnTime)){
    fprintf(stderr,"Read Error: turnTime not read properly");
    exit(EXIT_FAILURE);
  }
  //print seconds per turn
  printf("Seconds per turn : %d \n",turnTime);

  /* Game Logic function */
  playGame(sd, playerNum, boardSize, turnTime);

  //in case real bad things happen still close the socket and exit nicely
  close(sd);

  exit(EXIT_SUCCESS);
}
