#!/usr/bin/python3
# -*- coding: utf-8 -*-
#better run in an empty folder

import os, subprocess, shutil, platform
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
    # MEGA_EMAIL_AUX=os.environ["MEGA_EMAIL_AUX"]
    # MEGA_PWD_AUX=os.environ["MEGA_PWD_AUX"]
except:
    logging.fatal("You must define variables MEGA_EMAIL MEGA_PWD. WARNING: Use an empty account for $MEGA_EMAIL")
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

    if cmd_es(FIND+" /") != b"/":
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
    remotefolders=['01/'+a for a in ['s01/ss01']+ ['s02/ss0'+z for z in ['1','2']] ]
    cmd_ef(MKDIR+" -p "+" ".join(remotefolders))
    for f in ['localUPs/01/'+a for a in ['s01/ss01']+ ['s02/ss0'+z for z in ['1','2']] ]: makedir(f)

#INITIALIZATION
clean_all()
initialize()

ABSMEGADLFOLDER=ABSPWD+'/megaDls'

clear_local_and_remote()

#Test 01 #clean comparison
compare_and_clear()

#Test 02 #no destiny empty file upload
cmd_ef(PUT+' '+'localtmp/file01.txt')
shutil.copy2('localtmp/file01.txt','localUPs/')
compare_and_clear()

#Test 03 #/ destiny empty file upload
cmd_ef(PUT+' '+'localtmp/file01.txt /')
shutil.copy2('localtmp/file01.txt','localUPs/')
compare_and_clear()

#Test 04 #no destiny non empty file upload
cmd_ef(PUT+' '+'localtmp/file01nonempty.txt')
shutil.copy2('localtmp/file01nonempty.txt','localUPs/')
compare_and_clear()

#Test 05 #update non empty file upload
out('newfile01contents', 'localtmp/file01nonempty.txt')
cmd_ef(PUT+' '+'localtmp/file01nonempty.txt')
shutil.copy2('localtmp/file01nonempty.txt','localUPs/file01nonempty.txt')
compare_and_clear()

#Test 06 #empty folder
cmd_ef(PUT+' '+'localtmp/le01/les01/less01')
copyfolder('localtmp/le01/les01/less01','localUPs/')
compare_and_clear()

#Test 07 #1 file folder
cmd_ef(PUT+' '+'localtmp/lf01/lfs01/lfss01')
copyfolder('localtmp/lf01/lfs01/lfss01','localUPs/')
compare_and_clear()

#Test 08 #entire empty folders structure
cmd_ef(PUT+' '+'localtmp/le01')
copyfolder('localtmp/le01','localUPs/')
compare_and_clear()

#Test 09 #entire non empty folders structure
cmd_ef(PUT+' '+'localtmp/lf01')
copyfolder('localtmp/lf01','localUPs/')
compare_and_clear()

#Test 10 #copy structure into subfolder
cmd_ef(PUT+' '+'localtmp/le01 /01/s01')
copyfolder('localtmp/le01','localUPs/01/s01')
compare_and_clear()

#~ #Test 11 #copy exact structure
makedir('auxx')
copyfolder('localUPs/01','auxx')
cmd_ef(PUT+' '+'auxx/01/s01 /01/s01')
copyfolder('auxx/01/s01','localUPs/01/s01')
rmfolderifexisting("auxx")
compare_and_clear()

#~ #Test 12 #merge increased structure
makedir('auxx')
copyfolder('localUPs/01','auxx')
touch('auxx/01/s01/another.txt')
cmd_ef(PUT+' '+'auxx/01/s01 /01/')
shutil.copy2('auxx/01/s01/another.txt','localUPs/01/s01')
compare_and_clear()
rmfolderifexisting("auxx")

#Test 13 #multiple upload
cmd_ef(PUT+' '+'localtmp/le01 localtmp/lf01 /01/s01')
copyfolder('localtmp/le01','localUPs/01/s01')
copyfolder('localtmp/lf01','localUPs/01/s01')
compare_and_clear()

currentTest=14
#Test 14 #local regexp
if (platform.system() != "Windows" and not CMDSHELL):
    cmd_ef(PUT+' '+'localtmp/*txt /01/s01')
    copybyfilepattern('localtmp/','*.txt','localUPs/01/s01')
    compare_and_clear()

currentTest=15
#Test 15 #../
cmd_ef(CD+' 01')
cmd_ef(PUT+' '+'localtmp/le01 ../01/s01')
cmd_ef(CD+' /')
copyfolder('localtmp/le01','localUPs/01/s01')
compare_and_clear()

currentTest=16

#Test 16 #spaced stuff
if CMDSHELL: #TODO: think about this again
    cmd_ef(PUT+' '+'localtmp/ls\ 01')
else:
    cmd_ef(PUT+' '+'"localtmp/ls 01"')

copyfolder('localtmp/ls 01','localUPs')
compare_and_clear()


###TODO: do stuff in shared folders...

########

##Test XX #merge structure with file updated
##This test fails, because it creates a remote copy of the updated file.
##That's expected. If ever provided a way to create a real merge (e.g.: put -m ...)  to do BACKUPs, reuse this test.
#mkdir aux
#shutil.copy2('-pr localUPs/01','aux')
#touch aux/01/s01/another.txt
#cmd_ef(PUT+' '+'aux/01/s01 /01/')
#rsync -aLp aux/01/s01/ localUPs/01/s01/
#echo "newcontents" > aux/01/s01/another.txt 
#cmd_ef(PUT+' '+'aux/01/s01 /01/')
#rsync -aLp aux/01/s01/ localUPs/01/s01/
#rm -r aux
#compare_and_clear()


# Clean all
if not VERBOSE:
    clean_all()
