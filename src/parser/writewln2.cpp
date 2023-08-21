/**********************************************************************
 
Author : Michael Blakey

This file is part of the Open Babel project.
For more information, see <http://openbabel.org/>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
***********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <set>
#include <deque>
#include <vector>
#include <stack>
#include <map>
#include <string>

#include <utility> // std::pair
#include <iterator>
#include <sstream>

#include <openbabel/mol.h>
#include <openbabel/plugin.h>
#include <openbabel/atom.h>
#include <openbabel/bond.h>
#include <openbabel/obconversion.h>
#include <openbabel/obiter.h>
#include <openbabel/kekulize.h>
#include <openbabel/ring.h>
#include <openbabel/babelconfig.h>
#include <openbabel/obmolecformat.h>

using namespace OpenBabel; 

#define REASONABLE 1024

const char *cli_inp;
const char *format; 

// --- options ---
static bool opt_wln2dot = false;
static bool opt_debug = false;


struct WLNSymbol;
struct WLNEdge; 
struct WLNGraph;
struct ObjectStack;


enum WLNTYPE
{
  STANDARD = 0,
  RING = 1,     
  SPECIAL = 2  // for now this is only going to be the pi bond
};

unsigned char static int_to_locant(unsigned int i){
  return i + 64;
}

unsigned int static locant_to_int(unsigned char loc){
  return loc - 64;
}

unsigned char static create_relative_position(unsigned char parent){
  // A = 129
  unsigned int relative = 128 + locant_to_int(parent);
  if(relative > 252){
    fprintf(stderr,"Error: relative position is exceeding 252 allowed space - is this is suitable molecule for WLN notation?\n");
    return '\0';
  }
  else
    return relative;
}



static void print_locant_array(OBAtom **locant_path, unsigned int size){
  fprintf(stderr,"[ ");
  for(unsigned int i=0; i<size;i++){
    if(!locant_path[i])
      fprintf(stderr,"0 ");
    else
      fprintf(stderr,"%d ",locant_path[i]->GetIdx());
  }
    
  fprintf(stderr,"]\n");
}



/**********************************************************************
                          STRUCT DEFINTIONS
**********************************************************************/
 

struct WLNEdge{
  WLNSymbol *parent;
  WLNSymbol *child;
  WLNEdge *nxt;

  bool aromatic;
  unsigned int order;

  WLNEdge(){
    parent = 0;
    child = 0;
    aromatic = 0;
    order = 0;
    nxt = 0;
  }
  ~WLNEdge(){};
};


struct WLNSymbol
{
  unsigned char ch;
  std::string special; // string for element, or ring, if value = '*'
  
  unsigned int type;
  unsigned int allowed_edges;
  unsigned int num_edges;

  unsigned int num_children;  // specifically for forward edges
  unsigned int on_child;      // which branch are we on for stack tracking

  WLNSymbol *previous;
  WLNEdge   *bonds; // array of bonds

  // if default needed
  WLNSymbol()
  {
    ch = '\0';
    allowed_edges = 0;
    num_edges = 0;
    type = 0;
    previous = 0;
    bonds = 0;


    num_children = 0; 
    on_child = 0; 
  }
  ~WLNSymbol(){};

  void set_edge_and_type(unsigned int e, unsigned int t=STANDARD){
    allowed_edges = e;
    type = t;
  }

};


// handles all memory and 'global' vars
struct WLNGraph
{
  
  WLNSymbol *root;

  unsigned int edge_count;
  unsigned int symbol_count;
  unsigned int ring_count;

  WLNSymbol *SYMBOLS[REASONABLE];
  WLNEdge   *EDGES  [REASONABLE];

  std::map<WLNSymbol *, unsigned int> index_lookup;
  std::map<unsigned int, WLNSymbol *> symbol_lookup;

  unsigned int glob_index; // babel starts from 1, keep consistent  

    // ionic parsing
  std::map<unsigned int,WLNSymbol*> string_positions; 
  std::map<WLNSymbol*,int> charge_additions;

  WLNGraph(){
    edge_count   = 0;
    symbol_count = 0;
    ring_count   = 0;
    glob_index   = 1; // babel atoms are +1 indexed

    // pointer safety
    root = 0;
    for (unsigned int i = 0; i < REASONABLE;i++){
      SYMBOLS[i] = 0;
      EDGES[i] = 0;
    }
  };

  ~WLNGraph(){
    for (unsigned int i = 0; i < REASONABLE;i++){
      delete SYMBOLS[i];
      delete EDGES[i];
    }
  }
};

/**********************************************************************
                         WLNSYMBOL Functions
**********************************************************************/


WLNSymbol *AllocateWLNSymbol(unsigned char ch, WLNGraph &graph)
{

  graph.symbol_count++;
  if(graph.symbol_count > REASONABLE){
    fprintf(stderr,"Error: creating more than 1024 wln symbols - is this reasonable?\n");
    return 0;
  }

  if(!ch){
    fprintf(stderr,"Error: null char used to symbol creation\n");
    return 0;
  }

  WLNSymbol *wln = new WLNSymbol;
  graph.SYMBOLS[graph.symbol_count] = wln;
  
  wln->ch = ch;
  graph.index_lookup[wln] = graph.glob_index;
  graph.symbol_lookup[graph.glob_index] = wln;
  graph.glob_index++;
  return wln;
}

WLNSymbol* CreateWLNNode(OBAtom* atom, WLNGraph &graph){

  if(!atom){
    fprintf(stderr,"Error: nullptr OpenBabel Atom*\n");
    return 0; 
  }

  unsigned int neighbours = 0; 
  unsigned int orders = 0; 
  OBAtom *neighbour = 0; 
  OBBond *bond = 0; 

  WLNSymbol *node = 0;
  switch(atom->GetAtomicNum()){
    case 1:
      node = AllocateWLNSymbol('H',graph);
      node->set_edge_and_type(1);
      break; 

    case 5:
      node = AllocateWLNSymbol('B',graph);
      node->set_edge_and_type(3);
      break;

    case 6:
      FOR_NBORS_OF_ATOM(iterator, atom){
        neighbour = &(*iterator);
        bond = atom->GetBond(neighbour);
        orders += bond->GetBondOrder(); 
        neighbours++;
      }
      if(neighbours <= 2){
        node = AllocateWLNSymbol('1',graph);
        node->set_edge_and_type(4);
      }
      else if(neighbours > 2){
        if(orders == 3){
          node = AllocateWLNSymbol('Y',graph);
          node->set_edge_and_type(3);
        }
        else{
          node = AllocateWLNSymbol('X',graph);
          node->set_edge_and_type(4);
        }
      }
      else{
        node = AllocateWLNSymbol('C',graph);
        node->set_edge_and_type(4);
      }
      break;
    
    case 7:
      node = AllocateWLNSymbol('N',graph);
      node->set_edge_and_type(atom->GetExplicitValence());
      break;
    
    case 8:
      if(atom->GetExplicitValence() < 2 && atom->GetFormalCharge() != -1){
        node = AllocateWLNSymbol('Q',graph);
        node->set_edge_and_type(1);
      }
      else{
        node = AllocateWLNSymbol('O',graph);
        node->set_edge_and_type(2);
      }
      break;
    
    case 9:
      node = AllocateWLNSymbol('F',graph);
      node->set_edge_and_type(atom->GetExplicitValence());
      break;

    case 15:
      node = AllocateWLNSymbol('P',graph);
      node->set_edge_and_type(6);
      break;

    case 16:
      node = AllocateWLNSymbol('S',graph);
      node->set_edge_and_type(6);
      break;

    case 17:
      node = AllocateWLNSymbol('G',graph);
      node->set_edge_and_type(atom->GetExplicitValence());
      break;

    case 35:
      node = AllocateWLNSymbol('E',graph);
      node->set_edge_and_type(atom->GetExplicitValence());
      break;

    case 53:
      node = AllocateWLNSymbol('I',graph);
      node->set_edge_and_type(atom->GetExplicitValence());
      break;



// all special elemental cases

    case 89:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AC";
      break;

    case 47:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AG";
      break;
  
    case 13:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AL";
      break;

    case 95:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AM";
      break;

    case 18:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AR";
      break;

    case 33:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AS";
      break;

    case 85:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AT";
      break;

    case 79:
      node = AllocateWLNSymbol('*',graph);
      node->special += "AU";
      break;


    case 56:
      node = AllocateWLNSymbol('*',graph);
      node->special += "BA";
      break;

    case 4:
      node = AllocateWLNSymbol('*',graph);
      node->special += "BE";
      break;

    case 107:
      node = AllocateWLNSymbol('*',graph);
      node->special += "BH";
      break;

    case 83:
      node = AllocateWLNSymbol('*',graph);
      node->special += "BI";
      break;

    case 97:
      node = AllocateWLNSymbol('*',graph);
      node->special += "BK";
      break;

    case 20:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CA";
      break;
    
    case 48:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CD";
      break;

    case 58:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CE";
      break;

    case 98:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CF";
      break;

    case 96:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CN";
      break;

    case 112:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CN";
      break;

    case 27:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CO";
      break;

    case 24:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CR";
      break;

    case 55:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CS";
      break;

    case 29:
      node = AllocateWLNSymbol('*',graph);
      node->special += "CU";
      break;

    case 105:
      node = AllocateWLNSymbol('*',graph);
      node->special += "DB";
      break;

    case 110:
      node = AllocateWLNSymbol('*',graph);
      node->special += "DS";
      break;

    case 66:
      node = AllocateWLNSymbol('*',graph);
      node->special += "DY";
      break;

    case 68:
      node = AllocateWLNSymbol('*',graph);
      node->special += "ER";
      break;

    case 99:
      node = AllocateWLNSymbol('*',graph);
      node->special += "ES";
      break;

    case 63:
      node = AllocateWLNSymbol('*',graph);
      node->special += "EU";
      break;

    case 26:
      node = AllocateWLNSymbol('*',graph);
      node->special += "FE";
      break;

    case 114:
      node = AllocateWLNSymbol('*',graph);
      node->special += "FL";
      break;

    case 100:
      node = AllocateWLNSymbol('*',graph);
      node->special += "FM";
      break;

    case 87:
      node = AllocateWLNSymbol('*',graph);
      node->special += "FR";
      break;

    case 31:
      node = AllocateWLNSymbol('*',graph);
      node->special += "GA";
      break;

    case 64:
      node = AllocateWLNSymbol('*',graph);
      node->special += "GD";
      break;

    case 32:
      node = AllocateWLNSymbol('*',graph);
      node->special += "GE";
      break;

    case 2:
      node = AllocateWLNSymbol('*',graph);
      node->special += "HE";
      break;

    case 72:
      node = AllocateWLNSymbol('*',graph);
      node->special += "HF";
      break;

    case 80:
      node = AllocateWLNSymbol('*',graph);
      node->special += "HG";
      break;

    case 67:
      node = AllocateWLNSymbol('*',graph);
      node->special += "HO";
      break;

    case 108:
      node = AllocateWLNSymbol('*',graph);
      node->special += "HS";
      break;

    case 49:
      node = AllocateWLNSymbol('*',graph);
      node->special += "IN";
      break;

    case 77:
      node = AllocateWLNSymbol('*',graph);
      node->special += "IR";
      break;

    case 36:
      node = AllocateWLNSymbol('*',graph);
      node->special += "KR";
      break;

    case 19:
      node = AllocateWLNSymbol('*',graph);
      node->special += "KA";
      break;

    case 57:
      node = AllocateWLNSymbol('*',graph);
      node->special += "LA";
      break;

    case 3:
      node = AllocateWLNSymbol('*',graph);
      node->special += "LI";
      break;

    case 103:
      node = AllocateWLNSymbol('*',graph);
      node->special += "LR";
      break;

    case 71:
      node = AllocateWLNSymbol('*',graph);
      node->special += "LU";
      break;

    case 116:
      node = AllocateWLNSymbol('*',graph);
      node->special += "LV";
      break;

    case 115:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MC";
      break;

    case 101:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MD";
      break;

    case 12:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MG";
      break;

    case 25:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MN";
      break;

    case 42:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MO";
      break;

    case 109:
      node = AllocateWLNSymbol('*',graph);
      node->special += "MT";
      break;

    case 11:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NA";
      break;

    case 41:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NB";
      break;

    case 60:
      node = AllocateWLNSymbol('*',graph);
      node->special += "ND";
      break;

    case 10:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NE";
      break;

    case 113:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NH";
      break;

    case 28:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NI";
      break;

    case 102:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NO";
      break;

    case 93:
      node = AllocateWLNSymbol('*',graph);
      node->special += "NP";
      break;


    case 118:
      node = AllocateWLNSymbol('*',graph);
      node->special += "OG";
      break;

    case 76:
      node = AllocateWLNSymbol('*',graph);
      node->special += "OS";
      break;


    case 91:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PA";
      break;

    case 82:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PB";
      break;

    case 46:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PD";
      break;

    case 61:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PM";
      break;

    case 84:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PO";
      break;

    case 59:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PR";
      break;

    case 78:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PT";
      break;

    case 94:
      node = AllocateWLNSymbol('*',graph);
      node->special += "PU";
      break;

    case 88:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RA";
      break;

    case 37:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RB";
      break;

    case 75:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RE";
      break;

    case 104:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RF";
      break;

    case 111:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RG";
      break;

    case 45:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RH";
      break;

    case 86:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RN";
      break;

    case 44:
      node = AllocateWLNSymbol('*',graph);
      node->special += "RU";
      break;

    case 51:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SB";
      break;

    case 21:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SC";
      break;

    case 34:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SE";
      break;

    case 106:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SG";
      break;

    case 14:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SI";
      break;

    case 62:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SM";
      break;

    case 50:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SN";
      break;

    case 38:
      node = AllocateWLNSymbol('*',graph);
      node->special += "SR";
      break;


    case 73:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TA";
      break;

    case 65:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TB";
      break;

    case 43:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TC";
      break;

    case 52:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TE";
      break;

    case 90:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TH";
      break;

    case 22:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TI";
      break;

    case 81:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TL";
      break;

    case 69:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TM";
      break;

    case 117:
      node = AllocateWLNSymbol('*',graph);
      node->special += "TS";
      break;

    case 92:
      node = AllocateWLNSymbol('*',graph);
      node->special += "UR";
      break;

    case 23:
      node = AllocateWLNSymbol('*',graph);
      node->special += "VA";
      break;

    case 54:
      node = AllocateWLNSymbol('*',graph);
      node->special += "XE";
      break;

    case 39:
      node = AllocateWLNSymbol('*',graph);
      node->special += "YT";
      break;

    case 70:
      node = AllocateWLNSymbol('*',graph);
      node->special += "YB";
      break;

    case 30:
      node = AllocateWLNSymbol('*',graph);
      node->special += "ZN";
      break;

    case 40:
      node = AllocateWLNSymbol('*',graph);
      node->special += "ZR";
      break;
    

    default:
      fprintf(stderr,"Error: unhandled element for WLNSymbol formation\n");
      return 0;
  }
  
  if(!graph.root)
    graph.root = node; 

  if(!node->allowed_edges)
    node->set_edge_and_type(8);

  return node; 
}



/**********************************************************************
                          WLNEdge Functions
**********************************************************************/


WLNEdge *AllocateWLNEdge(WLNSymbol *child, WLNSymbol *parent,WLNGraph &graph){

  if(!child || !parent){
    fprintf(stderr,"Error: attempting bond of non-existent symbols - %s|%s is dead\n",child ? "":"child",parent ? "":"parent");
    return 0;
  }

  graph.edge_count++;
  if(graph.edge_count > REASONABLE){
    fprintf(stderr,"Error: creating more than 1024 wln symbols - is this reasonable?\n");
    return 0;
  }
  
  if ((child->num_edges + 1) > child->allowed_edges){
    fprintf(stderr, "Error: wln character[%c] is exceeding allowed connections %d/%d\n", child->ch,child->num_edges+1, child->allowed_edges);
    return 0;
  }
  
  if ((parent->num_edges + 1) > parent->allowed_edges){
    fprintf(stderr, "Error: wln character[%c] is exceeding allowed connections %d/%d\n", parent->ch,parent->num_edges+1, parent->allowed_edges);
    return 0;
  }

  WLNEdge *edge = new WLNEdge;
  graph.EDGES[graph.edge_count] = edge;

  // use a linked list to store the bond, can also check if it already exists

  WLNEdge *curr = parent->bonds;
  if(curr){
    
    while(curr->nxt){
      if(curr->child == child){
        fprintf(stderr,"Error: trying to bond already bonded symbols\n");
        return 0;
      }
      curr = curr->nxt;
    }
      
    curr->nxt = edge;
  }
  else
    parent->bonds = edge; 

  // set the previous for look back
  child->previous = parent; 

  child->num_edges++;
  parent->num_edges++;

  edge->parent = parent; 
  edge->child = child;
  edge->order = 1;

  parent->num_children++;
  return edge;
}


void debug_edge(WLNEdge *edge){
  if(!edge)
    fprintf(stderr,"Error: debugging nullptr edge\n");  
  else
    fprintf(stderr,"%c -- %d --> %c\n",edge->parent->ch, edge->order ,edge->child->ch);
}


WLNEdge *search_edge(WLNSymbol *child, WLNSymbol*parent, bool verbose=true){
  if(!child || !parent){
    fprintf(stderr,"Error: searching edge on nullptrs\n");
    return 0;
  }
  
  WLNEdge *edge = 0;
  for (edge=parent->bonds;edge;edge = edge->nxt){
    if(edge->child == child)
      return edge;
  }
  if(verbose)
    fprintf(stderr,"Error: could not find edge in search\n");
  return 0;
}

WLNEdge *unsaturate_edge(WLNEdge *edge,unsigned int n){
  if(!edge){
    fprintf(stderr,"Error: unsaturating non-existent edge\n");
    return 0;
  }

  edge->order += n; 
  edge->parent->num_edges += n;
  edge->child->num_edges+= n;

  if(edge->parent->num_edges > edge->parent->allowed_edges){
    fprintf(stderr, "Error: wln character[%c] is exceeding allowed connections %d/%d\n", edge->parent->ch,edge->parent->num_edges, edge->parent->allowed_edges);
    return 0;
  }

  if(edge->child->num_edges > edge->child->allowed_edges){
    fprintf(stderr, "Error: wln character[%c] is exceeding allowed connections %d/%d\n", edge->child->ch,edge->child->num_edges, edge->child->allowed_edges);
    return 0;
  }

  return edge;
}



bool remove_edge(WLNSymbol *head,WLNEdge *edge){
  if(!head || !edge){
    fprintf(stderr,"Error: removing bond of non-existent symbols\n");
    return false;
  }
  
  head->num_edges--;
  edge->child->num_edges--;

  if(head->bonds == edge){
    head->bonds = 0;
    return true;
  }

  bool found = false;
  WLNEdge *search = head->bonds;

  WLNEdge *prev = 0;
  while(search){
    if(search == edge){ 
      found = true;
      break;
    }
    prev = search; 
    search = search->nxt;
  }

  if(!found){
    fprintf(stderr,"Error: trying to remove bond from wln character[%c] - bond not found\n",head->ch);
    return false;
  }
  else{
    WLNEdge *tmp = edge->nxt;
    prev->nxt = tmp;
    // dont null the edges as we use the mempool to release them
  }

  return true;
}


/**********************************************************************
                          Ring Construction Functions
**********************************************************************/

/* constructs the local rings system, return ring type code */
unsigned int ConstructLocalSSSR(  OBAtom *ring_root, OBMol *mol, 
                                  std::set<OBAtom*> &ring_atoms,
                                  std::map<OBAtom*,unsigned int> &ring_shares,
                                  std::set<OBRing*> &local_SSSR){

  if(!ring_root){
    fprintf(stderr,"Error: ring root is nullptr\n");
    return 0; 
  }

  std::map<OBAtom*,bool> visited; 
  std::stack<OBAtom*> atom_stack; 
  
  unsigned int in_rings = 0; 
  OBAtom *atom = 0; 
  OBAtom *neighbour = 0; 

  unsigned int fuses = 0;
  unsigned int multicyclic = 0;
  unsigned int branching = 0;   

  atom_stack.push(ring_root); 
  while(!atom_stack.empty()){
    in_rings = 0;
    atom = atom_stack.top();
    atom_stack.pop();
    visited[atom] = true; 
    ring_atoms.insert(atom);

    FOR_RINGS_OF_MOL(r,mol){
      OBRing *obring = &(*r);
      if(obring->IsMember(atom)){
        in_rings++;
        local_SSSR.insert(obring); // use to get the SSSR size into WLNRing 
      }
    }
    ring_shares[atom] = in_rings; 

    FOR_NBORS_OF_ATOM(aiter,atom){
      neighbour = &(*aiter); 
      if(neighbour->IsInRing() && !visited[neighbour]){
        atom_stack.push(neighbour); 
        visited[neighbour] = true;
      }
    }

    if(in_rings > 3)
      branching++;
    else if(in_rings == 3)
      multicyclic++;
    
    else if (in_rings == 2)
      fuses++;
  }

  if(opt_debug){
    fprintf(stderr,"  SSSR for system:    ");
    for(std::set<OBRing*>::iterator set_iter = local_SSSR.begin();set_iter != local_SSSR.end();set_iter++)
      fprintf(stderr,"%ld(%c) ",(*set_iter)->Size(), (*set_iter)->IsAromatic()?'a':'s');
    fprintf(stderr,"\n");

    fprintf(stderr,"  ring size:          %d\n",(unsigned int)ring_atoms.size());
    fprintf(stderr,"  fuse points:        %d\n",fuses);
    fprintf(stderr,"  multicyclic points: %d\n",multicyclic);
    fprintf(stderr,"  branching points:   %d\n",branching);
  }

  if(branching){
    fprintf(stderr,"NON-SUPPORTED: branching cyclics\n");
    return 0;
  }
  else if(multicyclic)
    return 3; 
  else 
    return 2; 
}

// get all potential seeds for locant path start
void GetSeedAtoms(  std::set<OBAtom*> &ring_atoms,
                    std::map<OBAtom*,unsigned int> &ring_shares,
                    std::vector<OBAtom*> &seed_atoms,
                    unsigned int target_shares)
{ 
  for(std::set<OBAtom*>::iterator iter = ring_atoms.begin(); iter != ring_atoms.end();iter++){
    if(ring_shares[(*iter)] == target_shares)
      seed_atoms.push_back((*iter));
  }
}


/* construct locant paths without hamiltonians - return the new locant pos */
unsigned int ShiftandAddLocantPath( OBMol *mol, OBAtom **locant_path,
                            unsigned int locant_pos,unsigned int path_size,
                            unsigned int hp_pos, OBRing *obring,
                            std::map<OBAtom*,bool> &atoms_seen,
                            std::vector<std::pair<OBAtom*,OBAtom*>> &nt_pairs,
                            std::vector<unsigned int> &nt_sizes)
                            
{
  
  bool seen = false;
  OBAtom *ratom = 0; 
  OBAtom *insert_start  =  locant_path[hp_pos];
  OBAtom *insert_end    =  locant_path[hp_pos+1]; 
  std::deque<int> path;

  for(unsigned int i=0;i<obring->Size();i++){
    path.push_back(obring->_path[i]);
    if(insert_end->GetIdx() == obring->_path[i])
      seen = true;
  }

  if(!seen){
    insert_start = locant_path[locant_pos-1];
    insert_end = locant_path[0];
  }
    

  while(path[0] != insert_start->GetIdx()){
    unsigned int tmp = path[0];
    path.pop_front();
    path.push_back(tmp);
  }

    
  // standard clockwise and anti clockwise additions to the path
  if(seen){
  
    // must be anti-clockwise - shift and reverse
    if(insert_start && path[1] == insert_start->GetIdx()){
      unsigned int tmp = path[0];
      path.pop_front();
      path.push_back(tmp);
      std::reverse(path.begin(),path.end());
    }

    if(opt_debug)
      fprintf(stderr,"  non-trivial bonds:  %-2d <--> %-2d from size: %ld\n",locant_path[hp_pos]->GetIdx(),locant_path[hp_pos+1]->GetIdx(),obring->Size());

    // add nt pair + size
    nt_pairs.push_back({locant_path[hp_pos],locant_path[hp_pos+1]});
    nt_sizes.push_back(obring->Size()); 

    // spit the locant path between hp_pos and hp_pos + 1, add elements
    unsigned int j=0;
    for(unsigned int i=0;i<path.size();i++){
      ratom = mol->GetAtom(path[i]);
      if(!atoms_seen[ratom]){
        // shift
        for(int k=path_size-1;k>hp_pos+j;k--) // potential off by 1 here. 
          locant_path[k]= locant_path[k-1];
          
        locant_path[hp_pos+1+j] = ratom;
        atoms_seen[ratom] = true;
        j++;
        locant_pos++;
      }
    }
  }

  // must be a ring wrap on the locant path, can come in clockwise or anticlockwise
  else{

    // this is the reverse for the ring wrap
    if(path[1] == insert_end->GetIdx()){ // end due to shift swap
      unsigned int tmp = path[0];
      path.pop_front();
      path.push_back(tmp);
      std::reverse(path.begin(),path.end());
    }

    // just add to the back, no shift required
    for(unsigned int i=0;i<path.size();i++){
      ratom = mol->GetAtom(path[i]);
      if(!atoms_seen[ratom]){
        locant_path[locant_pos++] = ratom;
        atoms_seen[ratom] = true;
      }
    }

    if(opt_debug)
      fprintf(stderr,"  non-trivial ring wrap:  %-2d <--> %-2d from size: %ld\n",locant_path[0]->GetIdx(),locant_path[locant_pos-1]->GetIdx(),obring->Size());


    // ending wrap condition 
    nt_pairs.push_back({locant_path[0],locant_path[locant_pos-1]});
    nt_sizes.push_back(obring->Size()); 

  } 

  return locant_pos;
}

/* works on priority, and creates locant path via array shifting, returns the spawn size */
OBAtom ** CreateLocantPath(   OBMol *mol, std::set<OBRing*> &local_SSSR, 
                              std::map<OBAtom*,unsigned int> &ring_shares,
                              std::vector<std::pair<OBAtom*,OBAtom*>> &nt_pairs,
                              std::vector<unsigned int> &nt_sizes,
                              unsigned int path_size,
                              OBAtom *seed_atom)
{

  OBAtom **locant_path = (OBAtom**)malloc(sizeof(OBAtom*) * path_size); 
  for(unsigned int i=0;i<path_size;i++)
    locant_path[i] = 0;


  OBAtom *ratom = 0;
  OBRing *obring = 0;

  // get the ring with the seed atom present
  for(std::set<OBRing*>::iterator iter = local_SSSR.begin(); iter != local_SSSR.end(); iter++){
    for(unsigned int i=0;i<(*iter)->_path.size();i++){
      if(mol->GetAtom((*iter)->_path[i]) == seed_atom){
        obring = (*iter);
        break;
      }
    }
  }

  if(!obring){
    fprintf(stderr,"Error: seed atom could not be found in local SSSR\n");
    return 0; 
  } 

  
  unsigned int locant_pos = 0;
  std::map<OBRing*,bool> rings_seen; 
  std::map<OBAtom*,bool> atoms_seen; 

  // add into the array directly and shift so seed is guareented in position 0
  for(unsigned int i=0;i<obring->_path.size();i++){
    ratom = mol->GetAtom(obring->_path[i]);
    locant_path[locant_pos++] = ratom;
    atoms_seen[ratom] = true;
  }

  while(locant_path[0] != seed_atom){
    locant_path[locant_pos] = locant_path[0];
    for(unsigned int i=0;i<path_size-1;i++)
      locant_path[i] = locant_path[i+1];
  }

  if(opt_debug)
    fprintf(stderr,"  non-trivial bonds:  %-2d <--> %-2d from size: %ld\n",locant_path[0]->GetIdx(),locant_path[locant_pos-1]->GetIdx(),obring->Size());
  

  nt_pairs.push_back({locant_path[0],locant_path[locant_pos-1]});
  nt_sizes.push_back(obring->Size());


  // get next ring in locant order
  for(unsigned int rings_handled = 0; rings_handled < local_SSSR.size()-1;rings_handled++){
    rings_seen[obring] = true;
    unsigned int hp_pos = 0; 
    for(unsigned int i=0;i<locant_pos;i++){
      bool found = false;
      ratom = locant_path[i];
      if(ring_shares[ratom] > 1){
        for(std::set<OBRing*>::iterator iter = local_SSSR.begin(); iter != local_SSSR.end(); iter++){
          if(!rings_seen[(*iter)] && (*iter)->IsInRing(ratom->GetIdx())){
            hp_pos = i;
            obring = (*iter);
            found = true;
            break;
          }
        }
        if(found)
          break;
      }
    }

    locant_pos =  ShiftandAddLocantPath(  mol,locant_path,
                                          locant_pos,path_size,hp_pos,obring,
                                          atoms_seen,nt_pairs,nt_sizes);
    if(!locant_pos)
      return 0;
  }
  

  
  return locant_path;
}




bool IsHeteroRing(OBAtom **locant_array,unsigned int size){
  for(unsigned int i=0;i<size;i++){
    if(locant_array[i]->GetAtomicNum() != 6)
      return true;
  }
  return false; 
}


void UpdateReducedPath( OBAtom **reduced_path, OBAtom** locant_path, unsigned int size,
                        std::map<OBAtom*,unsigned int> &ring_shares){
  for(unsigned int i=0;i<size;i++){
    if(ring_shares[locant_path[i]] > 1)
      reduced_path[i] = locant_path[i];
    else
      reduced_path[i] = 0; 
  }
}


std::string ReadLocantPath(OBAtom **locant_path,unsigned int path_size,
                            std::map<OBAtom*,unsigned int> ring_shares, // copy unavoidable 
                            std::vector<std::pair<OBAtom*,OBAtom*>> &nt_pairs,
                            std::vector<unsigned int> &nt_sizes,
                            unsigned int expected_rings)
{
  
  std::string ring_str; 
  if(IsHeteroRing(locant_path,path_size))
    ring_str += 'T';
  else
    ring_str += 'L';


  // can we take an interrupted walk between the points, if so, write ring size 
  // and remove 

  // create a reduced array 
  OBAtom **reduced_path = (OBAtom**)malloc(sizeof(OBAtom*) * path_size);
  UpdateReducedPath(reduced_path,locant_path,path_size,ring_shares);

  if(opt_debug){
    fprintf(stderr,"  locant path:  ");
    print_locant_array(locant_path,path_size); 
    fprintf(stderr,"  reduced path: ");
    print_locant_array(reduced_path,path_size);
  }
  
  
  unsigned int safety = 0;
  while(!nt_pairs.empty() && safety < expected_rings){
  
    for(unsigned int i=0;i<nt_pairs.size();i++){
      OBAtom *first =  nt_pairs[i].first; 
      OBAtom *second = nt_pairs[i].second;

      // find the position of first in the array
      unsigned int pos = 0; 
      for(pos;pos < path_size;pos++){
        if(locant_path[pos] == first)
          break;
      }

      // can we go to the second without interuption
      bool popped = false;
      for(unsigned int j=pos+1;j < path_size;j++){
        if(reduced_path[j] && reduced_path[j] != second){
          // interuption - pair cannot be handled in this iteration
          // break out of search, search next pair
          break;
        }
        else if(reduced_path[j] && reduced_path[j] == second){
          // write the ring, and pop nt_pair and nt_ring at position 
          if(pos){
            ring_str+= ' ';
            ring_str+= int_to_locant(pos+1);
          }
          ring_str += std::to_string(nt_sizes[i]);

          nt_pairs.erase(nt_pairs.begin() + i);
          nt_sizes.erase(nt_sizes.begin() + i);

          // update the reduced locant path based on ring_shares
          ring_shares[first]--;
          ring_shares[second]--;
          UpdateReducedPath(reduced_path,locant_path,path_size,ring_shares);

          // requires reset to while loop 
          popped = true; 
          break;
        }
      }

      if(popped) // resets to while
        break;
    }
    
    safety++;
  }

  if( nt_pairs[0].first == locant_path[0] 
      && nt_pairs[0].second == locant_path[path_size-1])
  {
    // last implied ring wrap
    ring_str += std::to_string(nt_sizes[0]);
    nt_pairs.clear();
    nt_sizes.clear();
  }
  else{
    fprintf(stderr,"Error: safety caught on reduced locant loop\n");
    return {};
  }

  free(reduced_path);
  reduced_path=0;
  return ring_str;
}


/* create the hetero atoms where neccesary */
void ReadHeteroAtoms(OBAtom** locant_path,unsigned int path_size, std::string &buffer,WLNGraph &graph){

  unsigned int last_hetero_index = 0; 
  for(unsigned int i=0;i<path_size;i++){

    if(locant_path[i]->GetAtomicNum() != 6){

      // handles 'A' starting and consecutive locants
      if(i > 0 && last_hetero_index != i-1){
        buffer += ' ';
        buffer += int_to_locant(i+1);
      }

      WLNSymbol *sym = CreateWLNNode(locant_path[i],graph); // graph can handle memory
      if(sym->ch == '*'){
        buffer += '-';
        buffer += sym->special; 
        buffer += '-';
      }
      else
        buffer += sym->ch; 
      
      last_hetero_index = i; 
    }


  }
 
}


/**********************************************************************
                          Canonicalisation Function
**********************************************************************/

unsigned int highest_unbroken_numerical_chain(std::string &str){
  unsigned int highest_chain = 0; 
  unsigned int current_chain = 0; 
  for(unsigned int i=0;i<str.size();i++){
    if(str[i] <= '9' && str[i] >= '0')
      current_chain++;
    else{
      if(current_chain > highest_chain)
        highest_chain = current_chain;
      
      current_chain = 0; 
    }
  }

  if(current_chain > highest_chain)
    highest_chain = current_chain;

  return highest_chain; 
}

unsigned char first_locant_seen(std::string &str){
  // ignore the L|T
  for(unsigned int i=1;i<str.size();i++){
    if(str[i] != ' ' && !(str[i] <= '9' && str[i] >= '0'))
      return str[i];
  }
  return 0;
}


/* returns the index of the minimal ring ring
- unbroken numerical chain count and lowest locant sum? */
unsigned int MinimalWLNRingNotation(std::vector<std::string> &ring_strings){

  unsigned int highest_chain = 0; 
  unsigned char lowest_loc  =  0;
  unsigned int return_index = 0;  
  for(unsigned int i=0;i<ring_strings.size();i++){
    unsigned int chain =  highest_unbroken_numerical_chain(ring_strings[i]); 
    unsigned char loc =  first_locant_seen(ring_strings[i]); 

    if(chain > highest_chain){
      highest_chain = chain;
      lowest_loc = loc; 
      return_index = i; 
    }
    else if (chain == highest_chain && lowest_loc > loc){
      lowest_loc = loc; 
      return_index = i;
    }
  }

  return return_index;
}



/**********************************************************************
                         Debugging Functions
**********************************************************************/


/* dump wln tree to a dotvis file */
void WLNDumpToDot(FILE *fp, WLNGraph &graph)
{  
  fprintf(fp, "digraph WLNdigraph {\n");
  fprintf(fp, "  rankdir = LR;\n");
  for (unsigned int i=0; i<=graph.symbol_count;i++)
  {
    WLNSymbol *node = graph.SYMBOLS[i];
    if(!node)
      continue;

    fprintf(fp, "  %d", graph.index_lookup[node]);
    if (node->ch == '*')
      fprintf(fp, "[shape=circle,label=\"%s\"];\n", node->special.c_str());
    else if (node->type == RING)
      fprintf(fp, "[shape=circle,label=\"%c\",color=green];\n", node->ch);
    else{
      if(std::isdigit(node->ch)){
        if (!node->special.empty())
          fprintf(fp, "[shape=circle,label=\"%s\"];\n", node->special.c_str());
        else
          fprintf(fp, "[shape=circle,label=\"%c\"];\n", node->ch);
      } 
      else
        fprintf(fp, "[shape=circle,label=\"%c\"];\n", node->ch);
    }
  
      
    WLNEdge *edge = 0;
    for (edge = node->bonds;edge;edge = edge->nxt){

      WLNSymbol *child = edge->child;
      unsigned int bond_order = edge->order;

      // aromatic
      if (bond_order > 1){
        for (unsigned int k=0;k<bond_order;k++){
          fprintf(fp, "  %d", graph.index_lookup[node]);
          fprintf(fp, " -> ");
          fprintf(fp, "%d\n", graph.index_lookup[child]);
        }
      }
      else{
        fprintf(fp, "  %d", graph.index_lookup[node]);
        fprintf(fp, " -> ");
        fprintf(fp, "%d\n", graph.index_lookup[child]);
      }
    }
  }

  fprintf(fp, "}\n");
}

bool WriteWLNDotGraph(WLNGraph &graph){
  fprintf(stderr,"Dumping wln graph to wln-graph.dot:\n");
  FILE *fp = 0;
  fp = fopen("wln-graph.dot", "w");
  if (!fp)
  {
    fprintf(stderr, "Error: could not create dump .dot file\n");
    fclose(fp);
    return false;
  }
  else
    WLNDumpToDot(fp,graph);
  
  fclose(fp);
  fp = 0;
  fprintf(stderr,"  dumped\n");
  return true;
}


void BabelDumptoDot(FILE *fp, OBMol *mol){

  fprintf(fp, "digraph BABELdigraph {\n");
  fprintf(fp, "  rankdir = LR;\n");
  FOR_ATOMS_OF_MOL(aiter,mol){
    OBAtom* atom = &(*aiter); 
    fprintf(fp, "  %d", atom->GetIdx());
    fprintf(fp, "[shape=circle,label=\"%s\"];\n", std::to_string(atom->GetIdx()).c_str());
  }

  FOR_BONDS_OF_MOL(biter,mol){
    OBBond* bond = &(*biter); 
    fprintf(fp, "  %d", bond->GetBeginAtom()->GetIdx() );
    fprintf(fp, " -> ");
    fprintf(fp, "%d\n", bond->GetEndAtom()->GetIdx() );

  }

  fprintf(fp, "}\n");
}


bool WriteBabelDotGraph(OBMol *mol){
  fprintf(stderr,"Dumping babel graph to babel-graph.dot:\n");
  FILE *fp = 0;
  fp = fopen("babel-graph.dot", "w");
  if (!fp)
  {
    fprintf(stderr, "Error: could not create dump .dot file\n");
    fclose(fp);
    return false;
  }
  else
    BabelDumptoDot(fp,mol);
  
  fclose(fp);
  fp = 0;
  fprintf(stderr,"  dumped\n");
  return true;
}


/**********************************************************************
                         BABEL Mol Functions
**********************************************************************/


// holds all the functions for WLN graph conversion, mol object is assumed ALIVE AT ALL TIMES
// uses old NM functions from previous methods: Copyright (C) NextMove Software 2019-present
struct BabelGraph{

  std::map<OBAtom*, WLNSymbol*>  atom_symbol_map; 
  std::map<WLNSymbol*,OBAtom*>   symbol_atom_map; 
  std::map<OBAtom*,bool> ring_handled; 
  
  BabelGraph(){};
  ~BabelGraph(){};


  /* add the starting atom to build a tree for a locant position */
  WLNSymbol* BuildWLNTree(OBAtom* start_atom, OBMol *mol,WLNGraph &graph){

    // has to be done as DFS in order to keep bond direction on the tree
    WLNSymbol *root = 0; 
    WLNSymbol *node   = 0; 
    WLNSymbol *child  = 0; 
    WLNEdge   *edge   = 0; 

    OBAtom* atom = start_atom;
    std::map<OBAtom*,bool> visited; 
    std::stack<OBAtom*> atom_stack; 
    atom_stack.push(atom); 

    while(!atom_stack.empty()){
      atom = atom_stack.top(); 
      atom_stack.pop();
      visited[atom] = true;

      // negative oxygen should be given a W character, therefore pointing in
      if(atom->GetFormalCharge() == -1 && atom->GetAtomicNum() == 8){
        FOR_NBORS_OF_ATOM(iterator, atom){
          OBAtom *neighbour = &(*iterator);
          if(!visited[neighbour] && !neighbour->IsInRing())
            atom_stack.push(neighbour); 
        }
        continue;
      }

      // create the first atom if needed
      if(!atom_symbol_map[atom]){
        node = CreateWLNNode(atom,graph); 

        if(!root)
          root = node;

        if(!node){
          fprintf(stderr,"Error: could not create node in BuildWLNTree\n");
          return 0;
        }
        
        atom_symbol_map[atom] = node; 
        symbol_atom_map[node] = atom;
      }
      else
        node = atom_symbol_map[atom]; 

      // this will look back, so order is important to maintain
      FOR_NBORS_OF_ATOM(iterator, atom){
        OBAtom *neighbour = &(*iterator);
        if(!atom_symbol_map[neighbour] && !neighbour->IsInRing()){
          child = CreateWLNNode(neighbour,graph); 
          if(!child){
            fprintf(stderr,"Error: could not create node in BuildWLNTree\n");
            return 0;
          }

          atom_symbol_map[neighbour] = child; 
          symbol_atom_map[child] = neighbour; 

          // bond here, and don't consider the symbol if the atom is already made 
          OBBond *bond = atom->GetBond(neighbour); 
          if(!bond){
            fprintf(stderr,"Error: accessing non-existent bond in BuildWLNTree\n");
            return 0;
          }
          unsigned int order = bond->GetBondOrder(); 
          edge = AllocateWLNEdge(child,node,graph); 
          if(order > 1)
            edge = unsaturate_edge(edge,order-1);
        }
          
        if(!visited[neighbour] && !neighbour->IsInRing())
          atom_stack.push(neighbour); 
      } 

    }
    
    return root;
  }


  /* reads the babel graph into the appropriate wln graph
      either return roots for ionic species to build tree,
      or if cyclic, return ring objects to parse  */

  bool ParseNonCyclic(OBAtom *start_atom,OBMol *mol,WLNGraph &graph,std::string &buffer){
    WLNSymbol *root_node = BuildWLNTree (start_atom,mol,graph); 
    if(root_node && WriteWLNFromNode(root_node,graph,buffer))
      return true;
    else{
      fprintf(stderr,"Error: failure in parsing non-cyclic WLN graph\n");
      return 0;
    } 
  }

  // will also add to handled
  bool CheckCarbonyl(WLNSymbol *sym, std::map<WLNSymbol*,bool> &visited){
    WLNEdge *edge = 0; 
    WLNEdge *oxygen = 0; 
  
    for(edge=sym->bonds;edge;edge=edge->nxt){
      if((edge->child->ch == 'O') && (edge->order == 2 || symbol_atom_map[edge->child]->GetFormalCharge() == -1)){
        oxygen = edge; 
        break;
      }
    }
    if(!oxygen)
      return false;
    else{
      visited[oxygen->child] = true;
      return true;
    } 
  }


  // will also add to handled
  bool CheckDIOXO(WLNSymbol *sym, std::map<WLNSymbol*,bool> &visited){

    WLNEdge *edge = 0; 
    // needs to a be a priority, so taking a double bond =O over a =O + -O-
    std::deque<WLNSymbol*> oxygens;
    for(edge=sym->bonds;edge;edge=edge->nxt){
      // highest priority double bond =O
      if(edge->child->ch == 'O' && edge->order == 2)
        oxygens.push_front(edge->child); 
      // lower priority  =O + -O-
      else if(edge->child->ch == 'O' && symbol_atom_map[edge->child]->GetFormalCharge() == -1)
        oxygens.push_back(edge->child); 
    }

    if(oxygens.size() < 2)
      return false;
    else{
      visited[oxygens[0]] = true;
      visited[oxygens[1]] = true;
      return true;
    } 
  }

  // writes to the buffer
  WLNSymbol* WriteCarbonChain(WLNSymbol *sym, std::string &buffer){
    
    unsigned int carbons = 1; 
    WLNSymbol *carbon_sym = sym;

    while(carbon_sym->bonds && carbon_sym->bonds->child->ch == '1' && carbon_sym->bonds->order == 1){
      carbons++;
      carbon_sym = carbon_sym->bonds->child;
    }

    buffer += std::to_string(carbons);
    return carbon_sym;
  }


  /* parse the created WLN graph */
  bool WriteWLNFromNode(WLNSymbol *root,WLNGraph &graph,std::string &buffer){

    // dfs style notational build from a given root, use to build the notation 
    WLNSymbol *top = 0; 
    WLNSymbol *prev = 0; 
    WLNEdge   *edge = 0; // for iterating
    
    std::stack<std::pair<WLNSymbol*,unsigned int>> stack; 
    std::stack <WLNSymbol*> branch_stack; 
    std::map<WLNSymbol*,bool> visited;
    
    bool following_terminator = false;
    unsigned int order = 0;  

    stack.push({root,0});

    while(!stack.empty()){
      top = stack.top().first;
      order = stack.top().second; 
      
// branching returns
      if( (top->previous && prev) && top->previous != prev && 
          !branch_stack.empty()){

        prev = top->previous;
        
        if(opt_debug)
          fprintf(stderr,"%c is on branch: %d\n",prev->ch,prev->on_child);

        // distinction between a closure and a pop
        if(!following_terminator)
          buffer += '&';

        WLNSymbol *branch_top = 0; 
        while(!branch_stack.empty() && prev != branch_stack.top()){
          branch_top = branch_stack.top();
          if(opt_debug)
            fprintf(stderr,"stack_top: %c - %d\n",branch_top->ch,branch_top->on_child);
          
          if( (branch_top->num_children != branch_top->on_child) 
              || branch_top->num_edges < branch_top->allowed_edges)
            buffer += '&';

          branch_stack.pop();
        }

        prev->on_child++;
      }
      else if(prev)
        prev->on_child++;

      following_terminator = false;
      
      stack.pop();
      visited[top] = true;
      prev = top;      

// bond unsaturations
      if(order == 2)
        buffer+='U';
      if(order == 3)
        buffer+="UU";        

      switch (top->ch){

// oxygens
        case 'O':
          buffer += 'O';
          break;

        case 'Q':
          buffer += 'Q';
          if(!top->num_edges)
            buffer += 'H';

          if(!branch_stack.empty()){
            prev = branch_stack.top();
            following_terminator = true;
          }
          break;

// carbons 
        // alkyl chain 
        case '1':
          top = WriteCarbonChain(top,buffer);
          prev = top;
          break;

        case 'Y':
        case 'X':
          if(CheckDIOXO(top, visited)){
            buffer += top->ch;
            buffer += 'W'; 
          }
          else if(CheckCarbonyl(top,visited))
            buffer += 'V'; 
          else{
            buffer += top->ch;
            branch_stack.push(top);
          }
          break;


// nitrogen
        case 'N':
          if(top->num_edges < 2){
            buffer += 'Z';
            if(!top->num_edges)
              buffer += 'H';

            if(!branch_stack.empty()){
              prev = branch_stack.top();
              following_terminator = true;
            }
          }
          else if(top->num_children < 2 && top->num_edges < 3)
            buffer += 'M';
          else if (top->num_children < 3 && top->num_edges < 4){
            buffer += 'N';
            if(CheckDIOXO(top, visited))
              buffer += 'W'; 

            branch_stack.push(top);
          }
          else{
            if(CheckDIOXO(top, visited)){
              buffer += 'N';
              buffer += 'W';
            }
            else{
              buffer += 'K';
              branch_stack.push(top); // implied methyl, must add to branch
            }
          }
          break;


// halogens
        case 'E':
        case 'F':
        case 'G':
        case 'I':
          if(top->num_edges > 1){
            buffer += '-';
            buffer += top->ch;
            buffer += '-';
            if(CheckDIOXO(top, visited))
              buffer += 'W'; 

            branch_stack.push(top);
          }
          else{
            buffer += top->ch;
            if(!top->num_edges && symbol_atom_map[top]->GetFormalCharge() == 0)
              buffer += 'H';

            if(!branch_stack.empty()){
              prev = branch_stack.top();
              following_terminator = true;
            }
          }
          break;

// branching heteroatoms 
        case 'B':
        case 'S':
        case 'P':
          buffer += top->ch;
          if(CheckDIOXO(top, visited))
            buffer += 'W'; 

          if(top->num_children > 0)
            branch_stack.push(top);
          
          break;


// specials 
        case '*':
          buffer += '-';
          buffer += top->special;
          buffer += '-';
          if(!top->num_edges && symbol_atom_map[top]->GetFormalCharge() == 0)
            buffer += 'H';
          else if(top->num_children > 0)
            branch_stack.push(top);
          
          break;

        default:
          fprintf(stderr,"Error: unhandled WLN char %c\n",top->ch); 
          return false; 
      }
    

      for(edge=top->bonds;edge;edge=edge->nxt){
        if(!visited[edge->child])
          stack.push({edge->child,edge->order}); 
      }

    }
    return true;
  }

  /* constructs and parses a cyclic structre, locant path is returned with its path_size */
  std::pair<OBAtom **,unsigned int> ParseCyclic(OBAtom *ring_root,OBMol *mol, WLNGraph &graph, std::string &buffer){
    if(opt_debug)
      fprintf(stderr,"Reading Cyclic\n");
   
    OBAtom **locant_path = 0;
    std::set<OBRing*> local_SSSR; 
    std::set<OBAtom*> ring_atoms; 
    std::map<OBAtom*,unsigned int> ring_shares; 
    std::vector<std::pair<OBAtom*,OBAtom*>> nt_pairs;
    std::vector<unsigned int>               nt_sizes;  
    
    std::vector<std::string> cyclic_strings;
    std::vector<OBAtom**> locant_paths;  
    unsigned int minimal_index = 0; 

    unsigned int expected_rings = 0;
    unsigned int ring_type = ConstructLocalSSSR(ring_root,mol,ring_atoms,ring_shares,local_SSSR); 
    unsigned int path_size = ring_atoms.size(); 
    
    if(path_size && ring_type){
      // generate seeds
      std::vector<OBAtom*> seed_atoms; 
      expected_rings = local_SSSR.size(); 
      GetSeedAtoms(ring_atoms,ring_shares,seed_atoms,ring_type);
      if(seed_atoms.empty()){
        fprintf(stderr,"Error: no seeds found to build locant path\n");
        return {0,0};
      }

      for(unsigned int i=0;i<seed_atoms.size();i++){
        
        // create a new path per string
        locant_path = CreateLocantPath(   mol,local_SSSR,ring_shares,
                                          nt_pairs,nt_sizes,path_size,
                                          seed_atoms[i]);
        if(!locant_path)
          return {0,0}; 
  

        std::string cyclic_str = ReadLocantPath(  locant_path,path_size,ring_shares,
                                                  nt_pairs,nt_sizes,
                                                  expected_rings);
        if(cyclic_str.empty())
          return {0,0};

        cyclic_strings.push_back(cyclic_str);
        locant_paths.push_back(locant_path);  

        if(opt_debug)
          std::cout << "  produced: " << cyclic_str << "\n\n";
      }


      // get the minal WLN cyclic system from the notations generated
      minimal_index = MinimalWLNRingNotation(cyclic_strings); 
      buffer+= cyclic_strings[minimal_index]; // add to the buffer

      // free all other locant paths 
      for(unsigned int i=0;i<locant_paths.size();i++){
        if(i != minimal_index){
          free(locant_paths[i]);
          locant_paths[i] = 0; 
        }
      }

      // add any hetero atoms at locant positions
      ReadHeteroAtoms(locant_paths[minimal_index],path_size,buffer,graph);

      // close ring
      buffer += 'J';
    }
    else
      return {0,0}; 


    return {locant_paths[minimal_index],path_size};
  }


  bool ParseAllCyclic(OBMol *mol, WLNGraph &graph,std::string &buffer){
    // get the start ring, and then use that as the jump point
    std::pair<OBAtom**,unsigned int> path_pair;  
    std::stack <std::pair<OBAtom**,unsigned int>> locant_stack; 
    path_pair = ParseCyclic(mol->GetAtom(mol->GetSSSR()[0]->_path[0]),mol,graph,buffer);
    locant_stack.push(path_pair); 
    while(!locant_stack.empty()){

      path_pair = locant_stack.top(); 
      OBAtom** loc_path = path_pair.first;
      unsigned int path_size = path_pair.second;

      if(!loc_path){
        fprintf(stderr,"Error: could not create locant path for local SSSR\n");
        return false;
      } 

      for(unsigned int i=0;i<path_size;i++){
        if(!ring_handled[loc_path[i]]){
          ring_handled[loc_path[i]] = true; // stops duplicates when returned from stack 

          // check its neighbours
          FOR_BONDS_OF_ATOM(b,loc_path[i]){
            OBAtom *ext_atom = b->GetEndAtom(); 
            if(!ext_atom->IsInRing()){
              buffer += ' ';
              buffer += int_to_locant(i+1); 

              if(b->GetBondOrder() > 1)
                buffer += 'U';
              if(b->GetBondOrder() > 2)
                buffer += 'U';
            
              if(!ParseNonCyclic(ext_atom,mol,graph,buffer))
                return false;
            }
          }
        }
      }

      // if you get to the end, pop and return down 
      

      free(loc_path); // make this a stack release when handling locants
    }

    return true; 
  }

};



/**********************************************************************
                         API FUNCTION
**********************************************************************/


bool WriteWLN(std::string &buffer, OBMol* mol)
{   
 
  WLNGraph wln_graph;
  BabelGraph obabel; 

  unsigned int cyclic = 0;
  FOR_RINGS_OF_MOL(r,mol)
    cyclic++;

  if(opt_debug)
    WriteBabelDotGraph(mol);

  if(!cyclic){
    bool started = false; 
    FOR_ATOMS_OF_MOL(a,mol){
      if(!obabel.atom_symbol_map[&(*a)]){
        if(started)
          buffer += " &"; // ionic species
        
        if(!obabel.ParseNonCyclic(&(*a),mol,wln_graph,buffer))
          return false;

        started = true; 
      }
    }
    
    // create an optional dotfiles
    if (opt_wln2dot)
      WriteWLNDotGraph(wln_graph);
    
    return true; 
  }
  else{

    if(!obabel.ParseAllCyclic(mol,wln_graph,buffer))
      return false; 
    
    // handles ionics here
    
    return true; 
  }

}


static void DisplayUsage()
{
  fprintf(stderr, "writewln <options> -i<format> -s <input (escaped)>\n");
  fprintf(stderr, "<options>\n");
  fprintf(stderr, "  -d                    print debug messages to stderr\n");
  fprintf(stderr, "  -h                    show the help for executable usage\n");
  fprintf(stderr, "  -i                    choose input format (-ismi, -iinchi, -ican)\n");
  fprintf(stderr, "  -w                    dump wln trees & babel graphs to dot files in [build]\n");
  exit(1);
}

static void DisplayHelp()
{
  fprintf(stderr, "\n--- wisswesser notation parser ---\n\n");
  fprintf(stderr, " This parser writes to wiswesser\n"
                  " line notation (wln) from smiles/inchi, the parser is built on OpenBabels\n"
                  " toolkit and will return the minimal WLN string\n");
  DisplayUsage();
}

static void ProcessCommandLine(int argc, char *argv[])
{
  const char *ptr = 0;
  int i;

  cli_inp = (const char *)0;
  format = (const char *)0;

  if (argc < 2)
    DisplayUsage();

  for (i = 1; i < argc; i++)
  {

    ptr = argv[i];
    if (ptr[0] == '-' && ptr[1]){
      switch (ptr[1]){

        case 'd':
          opt_debug = true;
          break;

        case 'h':
          DisplayHelp();

        case 'w':
          opt_wln2dot = true;
          break;

        case 'i':
          if (!strcmp(ptr, "-ismi"))
          {
            format = "smi";
            break;
          }
          else if (!strcmp(ptr, "-iinchi"))
          {
            format = "inchi";
            break;
          }
          else if (!strcmp(ptr, "-ican"))
          {
            format = "can";
            break;
          }
          else{
            fprintf(stderr,"Error: unrecognised format, choose between ['smi','inchi','can']\n");
            DisplayUsage();
          }

        case 's':
          if(i+1 >= argc){
            fprintf(stderr,"Error: must add string after -s\n");
            DisplayUsage();
          }
          else{
            cli_inp = argv[i+1];
            i++;
          }
          break;

        default:
          fprintf(stderr, "Error: unrecognised input %s\n", ptr);
          DisplayUsage();
      }
    }
  }

  if(!format){
    fprintf(stderr,"Error: no input format selected\n");
    DisplayUsage();
  }

  if(!cli_inp){
    fprintf(stderr,"Error: no input string entered\n");
    DisplayUsage();
  }

  return;
}

int main(int argc, char *argv[])
{
  ProcessCommandLine(argc, argv);
  
  std::string res;
  OBMol mol;
  OBConversion conv;

  conv.SetInFormat(format);
  res = conv.ReadString(&mol,cli_inp);

  std::string buffer;
  buffer.reserve(1000);
  if(!WriteWLN(buffer,&mol))
    return 1;
  
  std::cout << buffer << std::endl;

  return 0;
}


