#!/usr/bin/python3
# -*- coding: utf-8 -*-
#better run in an empty folder

import sys, os, subprocess, shutil, distutils, platform
from megacmd_tests_common import *

GET="mega-get"
PUT="mega-put"
RM="mega-rm"
CD="mega-cd"
LCD="mega-lcd"
MKDIR="mega-mkdir"
EXPORT="mega-export -f"
SHARE="mega-share"
FIND="mega-find"
WHOAMI="mega-whoami"
LOGOUT="mega-logout"
LOGIN="mega-login"
IPC="mega-ipc"
IMPORT="mega-import"

ABSPWD=os.getcwd()
currentTest=1

try:
    MEGA_EMAIL=os.environ["MEGA_EMAIL"]
    MEGA_PWD=os.environ["MEGA_PWD"]
except:
    logging.error("You must define variables MEGA_EMAIL MEGA_PWD. WARNING: Use an empty account for $MEGA_EMAIL")
    exit(1)

try:
    os.environ['VERBOSE']
    VERBOSE=True
except:
    VERBOSE=False

#VERBOSE=True

try:
    MEGACMDSHELL=os.environ['MEGACMDSHELL']
    CMDSHELL=True
    #~ FIND="executeinMEGASHELL find" #TODO
except:
    CMDSHELL=False

def clean_all():

    if cmd_es(WHOAMI) != osvar("MEGA_EMAIL"):
        cmd_ef(LOGOUT)
        cmd_ef(LOGIN+" " +osvar("MEGA_EMAIL")+" "+osvar("MEGA_PWD"))
    
    cmd_ec(RM+' -rf "*"')
    cmd_ec(RM+' -rf "//bin/*"')
    
    rmfolderifexisting("localUPs")
    rmfolderifexisting("localtmp")
    
    rmfileifexisting("megafind.txt")
    rmfileifexisting("localfind.txt")


def clear_local_and_remote():
    rmfolderifexisting("localUPs")
    cmd_ec(RM+' -rf "/*"')
    initialize_contents()

currentTest=1

def compare_and_clear() :
    global currentTest
    if VERBOSE:
        print("test $currentTest")
    
    megafind=sort(cmd_ef(FIND))
    localfind=sort(find('localUPs','.'))
    
    #~ if diff --side-by-side megafind.txt localfind.txt 2>/dev/null >/dev/null; then
    if (megafind == localfind):
        if VERBOSE:
            print("diff megafind vs localfind:")
            #diff --side-by-side megafind.txt localfind.txt#TODO: do this
            print("MEGAFIND:")
            print(megafind)
            print("LOCALFIND")
            print(localfind)
        print("test "+str(currentTest)+" succesful!")     
    else:
        print("test "+str(currentTest)+" failed!")
        print("diff megafind vs localfind:")
        #~ diff --side-by-side megafind.txt localfind.txt #TODO: do this
        print("MEGAFIND:")
        print(megafind)
        print("LOCALFIND")
        print(localfind)
        
        #cd $ABSPWD #TODO: consider this
        exit(1)

    clear_local_and_remote()
    currentTest+=1
    cmd_ef(CD+" /")


def check_failed_and_clear(o,status):
    global currentTest

    if status == 0: 
        print("test "+str(currentTest)+" failed!")
        print(o)
        exit(1)
    else:
        print("test "+str(currentTest)+" succesful!")

    clear_local_and_remote()
    currentTest+=1
    cmd_ef(CD+" /")

def initialize():
    if cmd_es(WHOAMI) != osvar("MEGA_EMAIL"):
        cmd_ef(LOGOUT)
        cmd_ef(LOGIN+" " +osvar("MEGA_EMAIL")+" "+osvar("MEGA_PWD"))
        

    if len(os.listdir(".")):
        logging.error("initialization folder not empty!")
        #~ cd $ABSPWD
        exit(1)

    if cmd_es(FIND+" /") != "/":
        logging.error("REMOTE Not empty, please clear it before starting!")
        #~ cd $ABSPWD
        exit(1)

    #initialize localtmp estructure:
    makedir("localtmp")
    touch("localtmp/file01.txt")
    out('file01contents', 'localtmp/file01nonempty.txt')
    #local empty folders structure
    for f in ['localtmp/le01/'+a for a in ['les01/less01']+ ['les02/less0'+z for z in ['1','2']] ]: makedir(f)
    #local filled folders structure
    for f in ['localtmp/lf01/'+a for a in ['lfs01/lfss01']+ ['lfs02/lfss0'+z for z in ['1','2']] ]: makedir(f)
    for f in ['localtmp/lf01/'+a for a in ['lfs01/lfss01']+ ['lfs02/lfss0'+z for z in ['1','2']] ]: touch(f+"/commonfile.txt")
    #spaced structure
    for f in ['localtmp/ls 01/'+a for a in ['ls s01/ls ss01']+ ['ls s02/ls ss0'+z for z in ['1','2']] ]: makedir(f)
    for f in ['localtmp/ls 01/'+a for a in ['ls s01/ls ss01']+ ['ls s02/ls ss0'+z for z in ['1','2']] ]: touch(f+"/common file.txt")

    # localtmp/
    # ├── file01nonempty.txt
    # ├── file01.txt
    # ├── le01
    # │   ├── les01
    # │   │   └── less01
    # │   └── les02
    # │       ├── less01
    # │       └── less02
    # ├── lf01
    # │   ├── lfs01
    # │   │   └── lfss01
    # │   │       └── commonfile.txt
    # │   └── lfs02
    # │       ├── lfss01
    # │       │   └── commonfile.txt
    # │       └── lfss02
    # │           └── commonfile.txt
    # └── ls 01
        # ├── ls s01
        # │   └── ls ss01
        # │       └── common file.txt
        # └── ls s02
            # ├── ls ss01
            # │   └── common file.txt
            # └── ls ss02
                # └── common file.txt


    #initialize dynamic contents:
    clear_local_and_remote()


def initialize_contents():
    contents=" ".join(['"localtmp/'+x+'"' for x in os.listdir('localtmp/')])
    cmd_ef(PUT+" "+contents+" /")
    makedir('localUPs')
    copybypattern('localtmp/','*','localUPs')

#INITIALIZATION
clean_all()
initialize()

ABSMEGADLFOLDER=ABSPWD+'/megaDls'

clear_local_and_remote()

#Test 01 #clean comparison
compare_and_clear()

#Test 02 #destiny empty file
cmd_ef(RM+' '+'file01.txt')
rmfileifexisting('localUPs/file01.txt')
compare_and_clear()

#Test 03 #/ destiny empty file upload
cmd_ef(PUT+' '+'localtmp/file01.txt /')
shutil.copy2('localtmp/file01.txt','localUPs')
compare_and_clear()

#Test 04 #no destiny nont empty file upload
cmd_ef(RM+' '+'file01nonempty.txt')
rmfileifexisting('localUPs/file01nonempty.txt')
compare_and_clear()

#Test 05 #empty folder
cmd_ef(RM+' '+'-rf le01/les01/less01')
rmfolderifexisting('localUPs/le01/les01/less01')
compare_and_clear()

#Test 06 #1 file folder
cmd_ef(RM+' '+'-rf lf01/lfs01/lfss01')
rmfolderifexisting('localUPs/lf01/lfs01/lfss01')
compare_and_clear()

#Test 07 #entire empty folders structure
cmd_ef(RM+' '+'-rf le01')
rmfolderifexisting('localUPs/le01')
compare_and_clear()

#Test 08 #entire non empty folders structure
cmd_ef(RM+' '+'-rf lf01')
rmfolderifexisting('localUPs/lf01')
compare_and_clear()

#Test 09 #multiple
cmd_ef(RM+' '+'-rf lf01 le01/les01')
rmfolderifexisting('localUPs/lf01')
rmfolderifexisting('localUPs/le01/les01')
compare_and_clear()

#Test 10 #.
cmd_ef(CD+' '+'le01')
cmd_ef(RM+' '+'-rf .')
cmd_ef(CD+' '+'/')
rmfolderifexisting('localUPs/le01')
compare_and_clear()

#Test 11 #..
cmd_ef(CD+' '+'le01/les01')
cmd_ef(RM+' '+'-rf ..')
cmd_ef(CD+' '+'/')
rmfolderifexisting('localUPs/le01')
compare_and_clear()

#Test 12 #../XX
cmd_ef(CD+' '+'le01/les01')
cmd_ef(RM+' '+'-rf ../les01')
cmd_ef(CD+' '+'/')
rmfolderifexisting('localUPs/le01/les01')
compare_and_clear()

currentTest=13

#Test 13 #spaced stuff
cmd_ef(RM+' '+'-rf "ls 01"')
rmfolderifexisting('localUPs/ls 01')
compare_and_clear()

#Test 14 #complex stuff
cmd_ef(RM+' '+'-rf "ls 01/../le01/les01" "lf01/../ls*/ls s02"')
rmfolderifexisting('localUPs/ls 01/../le01/les01')
[rmfolderifexisting('localUPs/lf01/../'+f+'/ls s02') for f in os.listdir('localUPs/lf01/..') if f.startswith('ls')] 
compare_and_clear()

#Test 15 #complex stuff with PCRE exp
if (platform.system() != "Windows"):
    cmd_ef(RM+' '+'-rf --use-pcre "ls 01/../le01/les0[12]" "lf01/../ls.*/ls s0[12]"')
    rmfolderifexisting('localUPs/ls 01/../le01/les01')
    rmfolderifexisting('localUPs/ls 01/../le01/les02')
    [rmfolderifexisting('localUPs/lf01/../'+f+'/ls s01') for f in os.listdir('localUPs/lf01/..') if f.startswith('ls')] 
    [rmfolderifexisting('localUPs/lf01/../'+f+'/ls s02') for f in os.listdir('localUPs/lf01/..') if f.startswith('ls')] 
    compare_and_clear()

currentTest=16

#Test 16 #spaced stuff2
cmd_ef(RM+' '+'-rf ls\ 01')
rmfolderifexisting('localUPs/ls 01')
compare_and_clear()

###TODO: do stuff in shared folders...

########

#~ #Test XX #regexp #yet unsupported
#~ cmd_ef(RM+' '+'-rf "le01/les*"')
#~ rmfolderifexisting('localUPs/le01/les*')
#~ compare_and_clear()


# Clean all
if not VERBOSE:
    clean_all()
