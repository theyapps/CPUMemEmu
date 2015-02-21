#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

// Pipe channels
#define READ 0
#define WRITE 1

// Mem modes
#define MEM_READ 1
#define MEM_WRITE 2
#define MEM_EXIT 5

// Execution modes
#define SYS 0
#define USR 1
#define SU 2

// Sizes
#define BUF_S 256 // Buffer Size
#define MEM_S 2000 // Memory Size
#define USR_SYS_S 1000 // User System Mem Division

using namespace std;

enum Registers{PC, IR, SP, AC, X, Y, NUM_REG};
enum MemCh{FLAG, ADDR, VAL, NUM_CH};

int CPU2Mem[2];
int Mem2CPU[2];
int execMode;

/**
*   Read value from memory
*/
int read(int address){
  char buf[BUF_S];

  if(execMode != SU && (execMode == SYS && address < 1000) || (execMode == USR && address >= 1000)){
    cout << "Memory read access violation at address: " << address << endl;
    sprintf(buf,"%02d%04d%04d", MEM_EXIT, 0, 0);
    exit(1);
  }

  // Send message to memory
  memset(buf, 0, BUF_S);
  sprintf(buf,"%02d%04d%04d", MEM_READ, address, 0);
  write(CPU2Mem[WRITE], buf, BUF_S);

  // Read value from memory
  read(Mem2CPU[READ], buf, BUF_S);

  return atoi(buf);
}

/**
*   Write value to memory
*/
void write(int address, int value){
  char buf[BUF_S];

  if(execMode != SU && (execMode == SYS && address < 1000) || (execMode == USR && address >= 1000)){
    cout << "Memory write access violation at address: " << address << endl;
    sprintf(buf,"%02d%04d%04d", MEM_EXIT, 0, 0);
    exit(1);
  }

  // Send message to memory
  memset(buf, 0, BUF_S);
  sprintf(buf,"%02d%04d%04d", MEM_WRITE, address, value); // 0110000999
  write(CPU2Mem[WRITE], buf, BUF_S);

}

/**
* Dump mem from min to max
*/
void memdump(int min, int max){

  cout << "MemDump:" << endl;
  for(int i = min; i < max; i++){
    cout << "[" << setw(4) << i << " : " << setw(4) <<  read(i) << "]";
    if(!((i + 1) % 5)){
      cout << endl;
    }
  }
}

/**
*
*/
bool init_mem(string filename){
  char buf[BUF_S];
  ifstream ifs;
  int address = 0;

  ifs.open(filename.c_str(), ifstream::in);
  // Set up memory
  if(!ifs.is_open()){
    cerr << "Failed to open file" << endl;
    memset(buf, 0, BUF_S);
    sprintf(buf,"%02d%04d%04d", MEM_EXIT, 0, 0);
    write(CPU2Mem[WRITE], buf, BUF_S);
    return false;
  }
  else{
    while (ifs.getline(buf,BUF_S)){
      for(int i = 0; i < BUF_S; i++){
        if(buf[i] == ' ' || buf[i] == '\n'){
          buf[i] = '\0';
          break;
        }
      }

      if(buf[0] == '.'){
        memmove(buf, buf+1, strlen(buf));
        address = atoi(buf);
      }
      else if(buf[0] == ' ' || buf[0] == '\0');
      else{
        write(address++, atoi(buf));
      }
    }
    ifs.close();
    return true;
  }
}

int main(int argc, char* argv[]){
  pid_t pid;
  string filename;
  int timerInterval;
  int f, address, value, insCnt;
  int reg[NUM_REG]; // Registers
  int memory[MEM_S]; // USR: 0 - 999 | SYS: 1000 - 1999

  char buf[BUF_S];

  srand (time(NULL));

  if(argc != 3){
    cerr << "Impoper arguments proper format is: \"" << argv[0]
    << " [programfile] [timerinterval]\"" << endl;
    return 1;
  }
  else{
    filename = argv[1];
    timerInterval = atoi(argv[2]);
  }

  pipe(CPU2Mem);
  pipe(Mem2CPU);

  switch(pid = fork()){
    case -1: // ERR
    cerr << "Failed to fork" << endl;
    return 1;

    case 0: // Child (Memory)

    memset(memory, 0, sizeof(memory));

    // Close streams that belong to parent
    close(CPU2Mem[WRITE]);
    close(Mem2CPU[READ]);

    // Act like memory...
    do{
      memset(buf, 0, BUF_S);
      read(CPU2Mem[READ], buf, 2);
      f = atoi(buf);

      switch(f){
        case MEM_READ:

        memset(buf, 0, BUF_S);
        read(CPU2Mem[READ], buf, 4);
        address = atoi(buf);

        if(address >= 0 && address < MEM_S){
          memset(buf, 0, BUF_S);
          sprintf(buf, "%d", memory[address]);
          write(Mem2CPU[WRITE], buf, BUF_S);
        }

        // << "MemRead: " << address << endl;

        break;

        case MEM_WRITE:

        memset(buf, 0, BUF_S);
        read(CPU2Mem[READ], buf, 4);
        address = atoi(buf);

        memset(buf, 0, BUF_S);
        read(CPU2Mem[READ], buf, 4);
        value = atoi(buf);

        if(address >= 0 && address < MEM_S){
          memory[address] = value;
        }

        //cout << "MemWrite: " << address << ", " << value << endl;


        break;

        case MEM_EXIT:
        //cout << "Mem exit" << endl;
        break;
      }

    }
    while(f != MEM_EXIT);

    _exit(0);
    break;

    default: // Parent (CPU)

    // Close streams that belong to child
    close(CPU2Mem[READ]);
    close(Mem2CPU[WRITE]);

    execMode = SU;
    if(!init_mem(filename)) return 1;

    // Initialize registers
    reg[SP] = USR_SYS_S;
    reg[PC] = reg[IR] = reg[AC] = reg[X] = reg[Y]  = 0;
    execMode = USR;

    // Run process
    insCnt = 1;
    while(reg[IR] != 50){

      //cout << " [" << insCnt << "] ";

      if(insCnt++ % timerInterval == 0 && reg[PC] < 1000){

        execMode = SYS;
        write(MEM_S - 1, reg[SP]);
        reg[SP] = MEM_S - 1;
        write(--reg[SP],reg[PC]);
        reg[PC] = 1000;

      }


      reg[IR] = read(reg[PC]++); // Fetch

      switch(reg[IR]){ // Execute

        case 1: // Load the value into the AC
        reg[AC] = read(reg[PC]++);
        break;

        case 2: // Load the value at the address into the AC
        reg[AC] = read(read(reg[PC]++));
        break;

        case 3: // Load the value from the address found in the address into the AC
        reg[AC] = read(read(read(reg[PC]++)));
        break;

        case 4: // Load the value at (address+X) into the AC
        reg[AC] = read(read(reg[PC]++) + reg[X]);
        break;

        case 5: // Load the value at (address+Y) into the AC
        reg[AC] = read(read(reg[PC]++) + reg[Y]);
        break;

        case 6: // Load from (Sp+X) into the AC
        reg[AC] = read(reg[SP] + reg[X]);
        break;

        case 7: // Store the value in the AC into the address
        write(read(reg[PC]++), reg[AC]);
        break;

        case 8: // Gets a random int from 1 to 100 into the AC
        reg[AC] = rand() % 100 + 1;
        break;

        case 9: // Put port 1 -> int 2->char
        switch(read(reg[PC]++)){

          case 1: // int
          cout << reg[AC];
          break; // char

          case 2:
          cout << (char)reg[AC];
          break;

          default: // Invalid Port
          break;

        }
        break;

        case 10: // Add the value in X to the AC
        reg[AC] += reg[X];
        break;

        case 11: // Add the value in Y to the AC
        reg[AC] += reg[Y];
        break;

        case 12: // Subtract the value in X from the AC
        reg[AC] -= reg[X];
        break;

        case 13: // Subtract the value in Y from the AC
        reg[AC] -= reg[Y];
        break;

        case 14: // Copy the value in the AC to X
        reg[X] = reg[AC];
        break;

        case 15: // Copy the value in X to the AC
        reg[AC] = reg[X];
        break;

        case 16: // Copy the value in the AC to Y
        reg[Y] = reg[AC];
        break;

        case 17: // Copy the value in Y to the AC
        reg[AC] = reg[Y];
        break;

        case 18: // Copy the value in AC to the SP
        write(reg[SP]--, reg[AC]);
        break;

        case 19: // Copy the value in SP to the AC
        reg[AC] = read(reg[SP]);
        break;

        case 20: // Jump to the address
        reg[PC] = read(reg[PC]++);
        break;

        case 21: // Jump to the address only if the value in the AC is zero
        if(reg[AC] == 0){
          reg[PC] = read(reg[PC]++);
        }
        else{
          reg[PC]++;
        }
        break;

        case 22: // Jump to the address only if the value in the AC is not zero
        if(reg[AC] != 0){
          reg[PC] = read(reg[PC]++);
        }
        else{
          reg[PC]++;
        }
        break;

        case 23: // Push return address onto stack, jump to the address
        write(--reg[SP], reg[PC] + 1);
        reg[PC] = read(reg[PC]);
        break;

        case 24: // Pop return address from the stack, jump to the address
        reg[PC] = read(reg[SP]++);
        break;

        case 25: // Increment the value in X
        reg[X]++;
        break;

        case 26: // Decrement the value in X
        reg[X]--;
        break;

        case 27: // Push AC onto stack
        write(--reg[SP], reg[AC]);
        break;

        case 28: // Pop from stack into AC
        reg[AC] = read(reg[SP]++);
        break;

        case 29: // Set system mode, switch stack, push SP and PC, set new SP and PC
        execMode = SYS;
        write(MEM_S - 1, reg[SP]);
        reg[SP] = MEM_S - 1;
        write(--reg[SP],reg[PC]);
        reg[PC] = 1500;
        break;

        case 30: // Restore registers, set user mode
        reg[PC] = read(reg[SP]++);
        reg[SP] = read(reg[SP]++);
        execMode = USR;
        break;

        case 50: // End execution
        break;

        default: // Invalid Inst
        break;
      }
    }

    memset(buf, 0, BUF_S);
    sprintf(buf,"%02d%04d%04d", MEM_EXIT, 0, 0);
    write(CPU2Mem[WRITE], buf, BUF_S);
    return 0;
    break;
  }
}
