#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rfsm.h"
#include "wlndfa.h"
#include "wlnzip.h"

#define CSIZE 64

unsigned int opt_mode = 0;
unsigned int opt_verbose = false;

const char *input;

void print_bits(unsigned char val) {
  for (int i = 7; i >= 0; i--)
    fprintf(stderr,"%d", (val & (1 << i)) ? 1:0);
  fprintf(stderr,"\n");
}


bool ReadLineFromFile(FILE *fp, char *buffer, unsigned int n, bool add_nl=true){
  char *end = buffer+n;
  char *ptr;
  int ch;

  ptr = buffer;
  do {
    ch = getc_unlocked(fp); // this increments fp
    if (ch == '\n') {
      if (add_nl)
        *ptr++ = '\n'; // if i want the newline or not
      *ptr = '\0';
      return true;
    }
    if (ch == '\f') {
      *ptr++ = '\n';
      *ptr = '\0';
      return true;
    }
    if (ch == '\r') {
      *ptr++ = '\n';
      *ptr = '\0';
      ch = getc_unlocked(fp);
      if (ch != '\n') {
        if (ch == -1)
          return false;
        ungetc(ch,fp);
      }
      return true;
    }
    if (ch == -1) {
      *ptr++ = '\n';
      *ptr = '\0';
      return ptr-buffer > 1;
    }
    *ptr++ = ch;
  } while (ptr < end);
  *ptr = 0;
  
  fprintf(stderr, "Warning: line too long!\n");
  return false;
}


double WLNNormalisedCompressionDistance(const char *s1, const char *s2, FSMAutomata *wlnmodel)
{
  double ncd = 0.0;   
  unsigned int A = EncodedBits(s1, wlnmodel);
  wlnmodel->AssignEqualProbs();

  unsigned int B = EncodedBits(s2, wlnmodel);
  wlnmodel->AssignEqualProbs();


  char *store = (char*)malloc(sizeof(char)*(strlen(s1)+strlen(s2)) +1); 
  memset(store,0,(strlen(s1)+strlen(s2)) +1);
  
  strcpy(store,s1);
  strcat(store,s2);
  unsigned int AB = EncodedBits(store, wlnmodel); 
  
  fprintf(stderr,"A: %d, B: %d, AB: %d\n",A,B,AB);

  if ( A > B )
    ncd = (double)(AB - B)/(double)A;
  else
    ncd = (double)(AB - A)/(double)B;
  

  free(store); 
  return ncd; 
}


static void DisplayUsage()
{  
  fprintf(stderr, "wlncluster <options> <input> > <out>\n");
  fprintf(stderr, "<options>\n");
  fprintf(stderr, "  -v          verbose debugging statements on\n");
  fprintf(stderr, "  -h          display this help menu\n");
  exit(1);  
}

static void DisplayHelp()
{
  fprintf(stderr,"wlncluster, uses NCD and FSM based similarity measures to cluster\n"
                  "chemicals in a file, chemical machine will provide text based similarity\n"
                  "measures in order to improve seperation. This will output a NCD matrix,\n"
                  "where seperate functions are used to plot based on a given hierarchical method.\n\n");

  DisplayUsage();
}

static void ProcessCommandLine(int argc, char *argv[])
{
  const char *ptr = 0;
  int i,j;

  input = (const char *)0;

  j = 0;
  for (i = 1; i < argc; i++)
  {

    ptr = argv[i];
    if (ptr[0] == '-' && ptr[1]){
      switch (ptr[1]){
        
        case 'h':
          DisplayHelp();
          break;

        case 'v':
          opt_verbose = true;
          break;

        default:
          fprintf(stderr, "Error: unrecognised input %s\n", ptr);
          DisplayUsage();
      }
    }
    else{
      switch(j++){
        case 0:
          input = ptr; 
          break;
        default:
          fprintf(stderr,"Error: multiple files not currently supported\n");
          exit(1);
      }
    }
  }

  if(!input){
    fprintf(stderr,"Error: no input file given\n");
    DisplayUsage();
  }
  
  return;
}


int main(int argc, char *argv[])
{
  ProcessCommandLine(argc, argv);

  FSMAutomata *wlnmodel = CreateWLNDFA(REASONABLE*2,REASONABLE*4); // build the model 

  // mimic arithmetic 
  for(unsigned int i=0;i<wlnmodel->num_states;i++){
    if(wlnmodel->states[i]->accept)
      wlnmodel->AddTransition(wlnmodel->states[i],wlnmodel->root,'\n');
  }

  wlnmodel->AssignEqualProbs();
  
  fprintf(stderr,"%f\n",WLNNormalisedCompressionDistance("L67TJ\n", "L6TJ\n", wlnmodel));
  fprintf(stderr,"%f\n",WLNNormalisedCompressionDistance("L6TJ\n", "L6TJ\n", wlnmodel));
  fprintf(stderr,"%f\n",WLNNormalisedCompressionDistance("L B666TJ\n", "L6TJ\n", wlnmodel));
  fprintf(stderr,"%f\n",WLNNormalisedCompressionDistance("1X28P2X1\n", "L6TJ\n", wlnmodel));

  delete wlnmodel;
  return 0;
}
