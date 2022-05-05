# About h2incc

This tool's purpose is to convert C header files to MASM include files.
Once it is mature it should be able to convert the Win32 header files
supplied by Microsoft (in the Platform SDK or in Visual C). This will
never work 100% perfectly, though, some of the created includes will
always require at least minor manual adjustments.


# Installation/Deinstallation

h2incc can be built/installed using CMake.

# Usage

h2incc is a console application and requires command line parameters:
 
     h2incc <options> filespec
   
filespec specifies the files to process, usually C header files. Wildcards
are allowed. 
 
Case-sensitive options accepted by h2incc are:
 
 -a: this will add @align as alignment parameter for STRUCT/UNION 
     declarations. Initially equate @align is defined as empty string,
     but include files pshpackX.inc will change it. Thus this issue is
     handled roughly similiar as with Microsoft VC. Set this option if
     you want to ensure VC compatibility.
     
 -b: batch mode without user interaction. 
 
 -c: copy comments found in source files to the created .INC files
 
 -dn: define handling of __declspec(dllimport).
   n=0: this is the default behaviour. Depending on values found in
        h2incc.ini, section [Prototype Qualifiers], h2incc will create
        either true MASM prototypes or externdefs referencing IAT entries,
        which is slightly faster.
   n=1: always assume __declspec(dllimport) for prototypes. This will force
        h2incc to always create externdefs referencing IAT entries.
   n=2: always ignore __declspec(dllimport) for prototypes. This will force
        h2incc to always create true MASM prototypes.
   n=3: use @DefProto macro to define prototypes. This may reduce file size
        compared to option -d0 or -d1 and makes the generated includes more
        readable, but more importantly it will allow to link statically or
        dynamically with one include file version and still use the fastest
        calling mechanism for both methods. Example:
     
          _CRTIMP char * __cdecl _strupr( char *);
     
        With option -d0 this would be converted to either:
     
          proto__strupr typedef proto c  :ptr sbyte
          externdef c _imp___strupr: ptr proto__strupr
          _strupr equ <_imp___strupr>

        or, if entry _CRTIMP=8 is *not* included in h2incc.ini:
     
          _strupr proto c :ptr sbyte

        With option -d3 h2incc will instead generate:

          @DefProto _CRTIMP, _strupr, c, <:ptr sbyte>, 4

        and @DefProto macro will then create either a IAT based externdef
        or a true prototype depending on the current value of _CRTIMP.
         
 -e: write full decorated names of function prototypes to a .DEF file,
     which may then be used as input for an external tool to create import
     libraries (POLIB for example).
     
 -i: process includes. This option will cause h2incc to process all
     #include preprocessor lines in the source file. So if you enter
     "h2incc -i windows.h" windows.h and all headers referenced inside
     windows.h will be converted to include files! Furthermore, h2incc
     will store all structure and macro names found in any source file
     in one symbol table.
 
 -I directory: specify an additional directory to search for header files.
     May be useful in conjunction with -i switch.
     
 -p: add prototypes to summary (-S).
     
 -q: avoid RECORD definitions. Since names in records must be unique in MASM
     it may be conveniant to avoid records at all. Instead equates will be
     defined.
 
 -r: size-safe RECORD definitions. May be required if a C bitfield isn't
     fully defined, that is, only some bits are declared. With this option
     set the record is enclosed in a union together with its type.
     example (excerpt from structure MENUBARINFO in winuser.h):
     
         BOOL  fBarFocused:1;
         BOOL  fFocused:1;
         
     is now translated to:    
     
         MENUBARINFO_R0	RECORD fBarFocused:1,fFocused:1
         union                 ;added by -r switch	
             BOOL ?            ;added by -r switch
             MENUBARINFO_R0 <>
         ends                  ;added by -r switch
         
     So MASM will reserve space for a BOOL (which is 4 bytes). Without
     the -r option MASM would pack the bits in 1 byte only.
     
 -s c|p|t|e: selective output. Without this option everything is generated.
     Else select c/onstants or p/rototypes or t/ypedefs or e/xternals
     or any combination of these.
     
 -S: display summary of structures, macros, prototypes (optionally, -p) and
     typedefs (optionally, -t) found in source.
 
 -t: add typedefs to summary (-S).
 
 -u: generate untyped parameters in prototypes. Without this option the
     types are copied from the source file.
     
 -v: verbose mode. h2incc will display the files it is currently processing.

 -Wn: set warning level:
     n=0: display no warnings
     n=1: display warnings concerning usage of reserved words as names
          of structures, prototypes, typedefs or equates/macros.
     n=2: display warnings concerning usage of reserved words as names
          of structure members or macro parameters.
     n=3: display all warnings.
     
 -y: overwrites existing .INC files without confirmation. Without this
     option h2incc will not process input files if the resulting output
     file already exists. Shouldn't be used in conjunction with -i option
     to avoid multiple processing of the same header file.
     
 h2incc expects a private profile file with name h2incc.ini in the directory
 where the binary is located. This file contains some parameters for fine
 tuning. For more details view this file.


Some examples for how to use h2incc:
 
- h2incc c:\c\include\windows.h
  
  will process c:\c\include\windows.h and generate a file windows.inc
  in the current directory.
    
- h2incc -i c:\c\include\windows.h

  will process c:\vc\include\windows.h and all include files referenced
  by it. Include files will be stored in current directory.

- h2incc c:\c\include    or    h2incc c:\c\include\*.*

  will process all files in c:\c\include. Include files will be stored
  in current directory.

- h2incc -o c:\temp *.h

  will process all files with extension .h in current directory and store
  the include files in c:\temp.
 

 
# Known Bugs and Restrictions

- one should be aware that some C header file declarations simply cannot
  be translated to ASM. There are no things like inline functions in ASM,
  for example.
- on some situations h2incc has to "count" braces. This can interfere
  with #if preprocessor commands, because h2incc cannot evaluate expressions
  in these commands. As a result h2incc may get confused and produce garbage.
- h2incc knows little about C++ classes. Source files which have class 
  definitions included may confuse h2incc.
- "far" and "near" qualifiers are skipped, so this tool will not work
  for 16bit includes.
- macros in C header files will most likely not be converted reliably
  and therefore may require manual adjustments.


# License

h2incc is freeware. Please read file license.txt for more details.


Based on h2incx by Japheth (https://www.japheth.de/h2incX.html)
