#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

WRITER="${SCRIPT_DIR}/../bin/writewln"
READER="${SCRIPT_DIR}/../bin/readwln"
COMP="${SCRIPT_DIR}/../bin/obcomp"
MODE=""
FILE=""


ARG_COUNT=0
process_arguments() {
  # Loop through all the arguments
  for arg in "$@"; do
    case "$arg" in
      -h|--help)
        # Display a help message
        echo "Usage: pubchem_score_test.sh <MODE>"
        echo "Modes:"
        echo "  chemspider"
        echo "  pubchem"
        echo "  smith"
        echo "  chembl"
        echo "  external"
        echo "Options:"
        echo "  -h, --help          Show this help message"
        exit 0;
        ;;
      *)
        if [ $ARG_COUNT -eq 0 ]; then
          MODE=$arg;
        elif [ $ARG_COUNT -eq 1 ]; then
          FILE=$arg
        fi
        ((ARG_COUNT++))
        ;;
    esac
    shift # Shift to the next argument
  done
}

main(){

  if [ -z $MODE ]; then 
    echo "No mode selected!, for help use -h flag";
    exit 1;
  elif [ "$MODE" == "chemspider" ]; then
    FILE="${SCRIPT_DIR}/../data/chemspider.tsv"
  elif [ "$MODE" == "pubchem" ]; then
    FILE="${SCRIPT_DIR}/../data/pubchem.tsv"
  elif [ "$MODE" == "smith" ]; then
    FILE="${SCRIPT_DIR}/../data/smith.tsv"
  elif [ "$MODE" == "chembl" ]; then
    FILE="${SCRIPT_DIR}/../data/chembl24.tsv"
  elif [ "$MODE" == "external" ]; then
    echo "performing unit test on $FILE"
  else
    echo "mode choice invalid!, for help use -h flag";
    exit 1;
  fi;

  COUNT=0
  TOTAL=$(wc -l < $FILE)

  LINE=0
  while read p; do
    ((LINE++));
    echo -ne "$LINE: "
    WLN=$(echo -n "$p" | cut -d $'\t' -f1)
    SMILES=$(echo -n "$p" | cut -d $'\t' -f2)
    SMILES="$(sed -e 's/[[:space:]]*$//' <<<${SMILES})"

    NEW_WLN=$($WRITER -ismi -s "${SMILES}" 2> /dev/null) # chembl is canonical smiles

    if [ -z "$NEW_WLN" ]; then
      echo -ne "$WLN != any new WLN string\t${SMILES}\n$"
      continue
    fi;

    NEW_WLN="$(sed -e 's/[[:space:]]*$//' <<<${NEW_WLN})"
    NEW_SMILES=$($READER -osmi -s "${NEW_WLN}" 2> /dev/null) 
    
    if [ -z "$NEW_SMILES" ]; then
      echo "$NEW_WLN != anything $SMILES"
      continue
    fi;

    SAME=$($COMP "$SMILES" "$NEW_SMILES")
    if [[ "$SAME" == "1" ]]; then
      ((COUNT++));
      echo -ne "\r"
    else
      echo -ne  "$WLN == $SMILES\t$NEW_WLN\t$NEW_SMILES\n"
    fi;

  done <$FILE

  echo -ne "\r$COUNT/$TOTAL correct\n"
}

process_arguments "$@"
main
exit 0
