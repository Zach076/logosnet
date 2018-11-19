/* CSCI 367 Lexithesaurus: prog2_server.c
*
* 31 OCT 2018, Zach Richardson and Mitch Kimball
*/

#include "trie.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define MAXWORDSIZE 255
#define QLEN 6 /* size of request queue */
#define TRUE 1
#define FALSE 0
int visits = 0; /* counts client connections */
struct TrieNode *dictionary;

/*------------------------------------------------------------------------
*  Program: demo_server
*
*  Purpose: allocate a socket and then repeatedly execute the following:
*  (1) wait for the next connection from a client
*  (2) send a short message to the client
*  (3) close the connection
*  (4) go back to step (1)
*
*  Syntax: ./demo_server port
*
*  port - protocol port number to use
*
*------------------------------------------------------------------------
*/

//sends data from buf of size len to sd and if theres a fixable error,
//try to send again, otherwise exit nicely
void betterSend(int sd, void* buf, size_t len, int sd2) {
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
        close(sd2);
        exit(EXIT_FAILURE);
      }
    }
  }
}

//recieves data drom a send, storing the data of length len to buf
//prints error if recieve fails
void recieve(int sd, void* buf, size_t len, char* error, int sd2) {
  ssize_t n;
  //recieve data
  n = recv(sd, buf, len, MSG_WAITALL);
  //if recieved incorrectly print error, disconnect both clients, and exit
  if (n != len) {
    fprintf(stderr,"Read Error: %s Score not read properly\n", error);
    close(sd);
    close(sd2);
    exit(EXIT_FAILURE);
  }
}

//random board generator
void makeBoard(char* board, uint8_t boardSize) {
  char vowels[5] = {'a','e','i','o','u'};
  char randChar; //char to be generated
  int vowel = 0; //vowel flag

  srand(time(NULL)); //set seed for randomization
  //for each space in the board
  for (int i = 0; i < boardSize; i++) {
    //if were not at the end or we already have a vowel
    if(i != boardSize-1 || vowel) {
      //generate a randChar
      randChar = (rand() %(122-97+1))+97;
    } else {
      //generate a vowel
      randChar = vowels[rand()%(4-0+1)];
    }

    //check if we just added a vowel and update the vowel flag
    if(randChar == 'a' ||
       randChar == 'e' ||
       randChar == 'i' ||
       randChar == 'o' ||
       randChar == 'u') {
      vowel = 1;
    }

    //put the char in the board
    board[i] = randChar;
  }
}

//a quick check to see if the guess is in the board
int checkGuess(char* guess,char* board){
  int letterCount[26]; //make an array for the size of the alphabet
  memset(letterCount,0,sizeof(letterCount)); //fill alphabet with zeroes
  int i; //used in for loops
  int valid = TRUE; //valid guess flag, to be returned

  //for each char in the board, add 1 to the letter position in the array
  for(i=0;i <strlen(board);i++){
    letterCount[board[i] - 97]++;
  }

  //for each char in the guess, decrement from the letterCount
  for(i=0;i <strlen(guess);i++){
    letterCount[guess[i]-97]--;
    if(letterCount[guess[i]-97] ==-1){ //if char isnt in board
      valid = FALSE; //return false
    }
  }
  return valid;
}

//plays turns in a round according to what turn flag the server sends us
void turnHandler(int p1,int p2,char* board,uint8_t *p1Score,uint8_t* p2Score){
  char yourTurn = 'Y';
  char notYourTurn = 'N';
  int isRoundOver = FALSE;
  ssize_t n;
  uint8_t wordlength;
  char guessbuffer[MAXWORDSIZE];
  struct TrieNode *guessedWords = getNode();
  uint8_t validguess = TRUE;
  uint8_t timeoutFlag = FALSE;

  memset(guessbuffer,0,sizeof(guessbuffer));
  int ap = p1;
  int iap = p2;

  while(!isRoundOver){
    //T.1
    betterSend(ap,&yourTurn,sizeof(yourTurn),iap);
    betterSend(iap,&notYourTurn,sizeof(notYourTurn), ap);

    //recieve timeout
    recieve(ap,&timeoutFlag,sizeof(timeoutFlag),"timeoutFlag", iap);

    // recieve players guess
    recieve(ap,&wordlength,sizeof(wordlength),"wordlength", iap);
    recieve(ap,guessbuffer,wordlength,"Word", iap);

    // check if the guess is in the dictionary and
    // if the guess has already been made and
    // if the guess was made from the board
    if(search(dictionary,guessbuffer) && !search(guessedWords, guessbuffer) && checkGuess(guessbuffer,board)){
      //put word into the guessed words trie
      insert(guessedWords,guessbuffer);

      //guess is valid, send to players
      betterSend(ap,&validguess,sizeof(validguess),iap);
      betterSend(iap,&validguess,sizeof(validguess),ap);
      //send length to inactive player
      betterSend(iap,&wordlength,sizeof(wordlength),ap);
      //send the guessed word
      betterSend(iap,guessbuffer,sizeof(guessbuffer),ap);

    } else{
      //not valid, send to players
      validguess = FALSE;
      betterSend(ap,&validguess,sizeof(validguess),iap);
      betterSend(iap,&validguess,sizeof(validguess),ap);

      //increment iap score
      isRoundOver = TRUE;
      if(ap == p1) {
        (*p2Score)++;
      } else {
        (*p1Score)++;
      }
    }

    //changing active player
    if(ap == p1) {
      ap = p2;
      iap = p1;
    } else {
      ap = p1;
      iap = p2;
    }

  }
}

//main game function
//plays game until win or loss calling turnHandler for each round
void playGame(uint8_t boardSize, uint8_t turnTime, int sd2, int sd3) {

  char board[boardSize+1];
  char guess; //guess recieved from server
  int isCorrect = 0; //flag for if the guess is correct
  int i; //used in for loop
  uint8_t roundNum = 1;
  uint8_t player1Score = 0;
  uint8_t player2Score = 0;
  int done = FALSE; //flag to keep game in while loop

  //null terminate the board
  memset(board,0,sizeof(board));

  //while the game is playing
  while(!done) {
    //if the game is now over
    if(player1Score == 3 || player2Score == 3) {
      done = TRUE;
    }
    //R.1 (send scores to both players)
    betterSend(sd2,&player1Score,sizeof(player1Score),sd3);
    betterSend(sd3,&player1Score,sizeof(player1Score),sd2);
    betterSend(sd2,&player2Score,sizeof(player2Score),sd3);
    betterSend(sd3,&player2Score,sizeof(player2Score),sd2);
    //R.2 (send round number to both players)
    betterSend(sd2,&roundNum,sizeof(roundNum),sd3);
    betterSend(sd3,&roundNum,sizeof(roundNum),sd2);
    //R.3 (make the board for the current round)
    makeBoard(board, boardSize);
    //R.4 (send the board to both players)
    betterSend(sd2,&board,(size_t)boardSize,sd3);
    betterSend(sd3,&board,(size_t)boardSize,sd2);

    //if we're still playing
    if(!done) {
      //R.5+R.6 (decide who starts the round, call turnHandler)
      if(roundNum%2 == 0){
        turnHandler(sd3,sd2, board, &player2Score, &player1Score);
      } else{
        turnHandler(sd2,sd3, board, &player1Score, &player2Score);
      }
      //increment round number
      roundNum++;
    }
  }

}

//fills a trie from a dictionary file
void populateTrie(char* dictionaryPath) {
  FILE *fp;
  char fileBuffer[1000];
  //fill buffer with null terminators
  memset(fileBuffer,0,sizeof(fileBuffer));

  //open file and read line by line, insert into trie
  fp = fopen(dictionaryPath, "r");
  while(fgets(fileBuffer,1000,fp)) {
    fileBuffer[strlen(fileBuffer)-1] = '\0';
    insert(dictionary, fileBuffer);
  }
}

//main function, mostly connection logic
int main(int argc, char **argv) {
  struct protoent *ptrp; /* pointer to a protocol table entry */
  struct sockaddr_in sad; /* structure to hold server's address */
  struct sockaddr_in cad; /* structure to hold client's address */

  int sd, sd2, sd3; /* socket descriptors */
  int port; /* protocol port number */
  int alen; /* length of address */
  int optval = 1; /* boolean value when we set socket option */
  char buf[1000]; /* buffer for string the server sends */
  char player1 = '1';
  char player2 = '2';
  uint8_t boardSize;
  uint8_t turnTime;
  char* dictionaryPath;
  port = atoi(argv[1]); /* convert argument to binary */

  dictionary = getNode();


  if( argc != 5 ) {
    fprintf(stderr,"Error: Wrong number of arguments\n");
    fprintf(stderr,"usage:\n");
    fprintf(stderr,"./prog2_server server_port board_size seconds_per_round dictionary_path\n");
    exit(EXIT_FAILURE);
  }

  boardSize = atoi(argv[2]);
  turnTime = atoi(argv[3]);
  dictionaryPath = argv[4];

  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */

  //: Set socket family to AF_INET
  sad.sin_family = AF_INET;

  // Set local IP address to listen to all IP addresses this server can assume. You can do it by using INADDR_ANY
  sad.sin_addr.s_addr = INADDR_ANY;


  if (port > 0) { /* test for illegal value */
    // set port number. The data type is u_short
    sad.sin_port = htons(port);
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
  sd = socket(AF_INET, SOCK_STREAM, ptrp->p_proto);
  if (sd < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  /* Allow reuse of port - avoid "Bind failed" issues */
  if( setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
    fprintf(stderr, "Error Setting socket option failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Bind a local address to the socket. For this, you need to
  pass correct parameters to the bind function. */
  if (bind(sd, (struct sockaddr*) &sad, sizeof(sad)) < 0) {
    fprintf(stderr,"Error: Bind failed\n");
    exit(EXIT_FAILURE);
  }

  /*  Specify size of request queue. Listen take 2 parameters --
  socket descriptor and QLEN, which has been set at the top of this code. */
  if (listen(sd, QLEN) < 0) {
    fprintf(stderr,"Error: Listen failed\n");
    exit(EXIT_FAILURE);
  }
  pid_t pid; //process id of the child processes.

  //fill dictionary
  populateTrie(dictionaryPath);

  /* Main server loop - accept and handle requests */
  while (1) {
    alen = sizeof(cad);
    if ((sd2 = accept(sd, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      exit(EXIT_FAILURE);
    }

    //send starting game info to player 1
    send(sd2,&player1,sizeof(player1),0);
    send(sd2,&boardSize,sizeof(boardSize),0);
    send(sd2,&turnTime,sizeof(turnTime),0);

    if ((sd3 = accept(sd, (struct sockaddr *)&cad, (socklen_t*)&alen)) < 0) {
      fprintf(stderr, "Error: Accept failed\n");
      exit(EXIT_FAILURE);
    }

    //send starting game info to player 2
    betterSend(sd3,&player2,sizeof(player2),sd2);
    betterSend(sd3,&boardSize,sizeof(boardSize),sd2);
    betterSend(sd3,&turnTime,sizeof(turnTime),sd2);

    // fork here and implement logic
    pid = fork();
    if (pid < 0) {
      perror("Error Fork() failure");
    }
    //we are in the child process.
    else if (pid == 0) {

      //play Lexithesaurus
      playGame(boardSize, turnTime,sd2,sd3);

      //at end of game close the socket and exit
      close(sd2);
      close(sd3);
      exit(EXIT_SUCCESS);
    }
  }
}
