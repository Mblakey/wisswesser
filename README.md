# Extraction and Conversion of Wiswesser Line Notation (WLN) 

Wiswesser Line Notation Parser, covert WLN to smiles, inchi, mol files and other chemical line notation (CLN) formats. This uses the OpenBabel chemical library, with the hope the code will be merged in. 

To extract WLN from documents, Finite State Machine (FSM) parser is built from the WLN rules, this machine uses greedy matching to return matched WLN sequences from documents. 

[READER RELEASE](./notes/release.md)

## Requirements

**git**, **cmake**, **make** and a **c++ compiler** are all essential. <br>
**graphviz** is an optional install to view wln graphs (not needed for build). 

**OpenBabel** [see repo](https://github.com/openbabel/openbabel), will be installed as an external dependency.  

## Build

Run `./build.sh` from the project directory, this will clone and build openbabel as well as linking
the library to the parser in cmake. Babel files will be installed to `external`. Building the projects places all executables into `bin/`. <br>


## Converting between WLN and CLN Formats

The following command line utilities are for converting between WLN and other chemical formats 

`readwln` - This takes a WLN sequence (single quote escaped) from the command line. e.g 'L6TJ', and returns the desired output format. 

```
readwln <options> -o<format> -s 'string'
```

#### Flags

`-c` - enable run-time string correction, this allows correction of branching symbols and spaces where a valid WLN string can be seen <br>
`-d` - enable all debugging logs to stderr<br>
`-h` - display the help menu <br>
`-o` - choose output format for string, options are `-osmi`, `-oinchi` and `-ocan` following OpenBabels format conventions <br>
`-w` - dump the wln graph to a dot file in the build directory, this can be seen using the following commands <br>

```
dot -Tsvg wln-graph.dot -o wln-graph.svg && open wln-graph.svg
```

<br>


`writewln` - This takes an input sequence (single quote escaped) from the command line. e.g 'c1ccccc1', and returns the corresponding WLN string. 

```
writewln <options> -i<format> -s 'string'
```

#### Flags 

`-d` - enable all debugging logs to stderr<br>
`-h` - display the help menu <br>
`-i` - choose input format for string, options are `-ismi`, `-iinchi` and `-ican` following OpenBabels format conventions <br>


## WLN Extraction 

Command line utility `wlngrep`. This either takes a filename, or an escaped single sequence if using the `-s` flag. With some exceptions, flags are kept inline with standard grep usage.

```
wlngrep <options> <filename>
```

#### Flags 

`-c` - return number of matches instead of string <br>
`-d` - dump resultant machine to dot file <br>
`-o` - print only the matched parts of line <br>
`-m` - do not minimise DFA (debugging only) <br>
`-s` - interpret `<filename>` as a string to match <br>
`-x` - return string if whole line matches <br>


## Unit Testing

All unit tests are contained in the `/test` directory.  

A text file for the WLN strings contained in Elbert G. Smiths rule book are contained in data. To run the unit test, run `./smith_test.sh`. This will give a score of successful conversions (100 is NOT expected), some incorrect strings are present. For Chembl, Chemspider and PubChem, run the equivilent shell files. 

Another text file containing all the english words is included, `./english_test.sh` will parse the reader over every capatilised english word and check for seg faults. 




