#!/usr/bin/python3
# -*- coding: utf-8 -*-
#better run in an empty folder

import os, shutil, logging
import unittest
import xmlrunner
from megacmd_tests_common import *

GET="mega-get"
PUT="mega-put"
RM="mega-rm"
CD="mega-cd"
LCD="mega-lcd"
MKDIR="mega-mkdir"
EXPORT="mega-export"
SHARE="mega-share"
FIND="mega-find"
WHOAMI="mega-whoami"
LOGOUT="mega-logout"
LOGIN="mega-login"
IPC="mega-ipc"
IMPORT="mega-import"

def setUpModule():
    global VERBOSE
    global MEGA_PWD
    global MEGA_EMAIL
    global MEGA_EMAIL_AUX
    global MEGA_PWD_AUX
    global MEGACMDSHELL
    global CMDSHELL
    global ABSPWD
    global ABSMEGADLFOLDER

    ABSPWD = os.getcwd()
    ABSMEGADLFOLDER=ABSPWD+'/megaDls'
    VERBOSE = 'VERBOSE' in os.environ

    if "MEGA_EMAIL" in os.environ and "MEGA_PWD" in os.environ and "MEGA_EMAIL_AUX" in os.environ and "MEGA_PWD_AUX" in os.environ:
        MEGA_EMAIL=os.environ["MEGA_EMAIL"]
        MEGA_PWD=os.environ["MEGA_PWD"]
        MEGA_EMAIL_AUX=os.environ["MEGA_EMAIL_AUX"]
        MEGA_PWD_AUX=os.environ["MEGA_PWD_AUX"]
    else:
        raise Exception("Environment variables MEGA_EMAIL, MEGA_PWD, MEGA_EMAIL_AUX, MEGA_PWD_AUX are not defined. WARNING: Use an empty account for $MEGA_EMAIL")

    CMDSHELL= "MEGACMDSHELL" in os.environ
    if CMDSHELL:
        MEGACMDSHELL=os.environ["MEGACMDSHELL"]

def clean_all():
    if cmd_es(WHOAMI) != osvar("MEGA_EMAIL"):
        cmd_ef(LOGOUT)
        cmd_ef(LOGIN+" " +osvar("MEGA_EMAIL")+" "+osvar("MEGA_PWD"))

    cmd_ec(RM+' -rf "*"')
    cmd_ec(RM+' -rf "//bin/*"')

    rmfolderifexisting("localUPs")
    rmfolderifexisting("localtmp")
    rmfolderifexisting("origin")
    rmfolderifexisting("megaDls")
    rmfolderifexisting("localDls")

    rmfileifexisting("megafind.txt")
    rmfileifexisting("localfind.txt")

def clear_dls():
    rmcontentsifexisting("megaDls")
    rmcontentsifexisting("localDls")


def safe_export(path):
    command = EXPORT + ' ' + path
    stdout, code = cmd_esc(command + ' -f -a')

    if code == 0:
        return stdout.split(b' ')[-1]

    # Run export without the `-a` option (also need to remove `-f` or it'll fail)
    # Output is slightly different in this case:
    #   * Link is surrounded by parenthesis, so we need to remove the trailing ')'
    #   * If path is a folder it'll also return the list of nodes inside, so we
    #   need to keep only the first line
    if b'already exported' in stdout:
        stdout = cmd_ef(command).split(b'\n')[0].strip(b')')
        if b'AuthKey=' in stdout:
            # In this case the authkey is the last word,
            # so the link is second from the right
            return stdout.split(b' ')[-2]
        else:
            return stdout.split(b' ')[-1]
    else:
        raise Exception(f"Failed trying to export '{path}': '{out}'")


def initialize_contents():
    global URIFOREIGNEXPORTEDFOLDER
    global URIFOREIGNEXPORTEDFILE
    global URIEXPORTEDFOLDER
    global URIEXPORTEDFILE

    if cmd_es(WHOAMI) != osvar("MEGA_EMAIL_AUX"):
        cmd_ef(LOGOUT)
        cmd_ef(LOGIN+" " +osvar("MEGA_EMAIL_AUX")+" "+osvar("MEGA_PWD_AUX"))

    if len(os.listdir(".")):
        raise Exception("initialization folder not empty!")

    #initialize localtmp estructure:
    makedir('foreign')

    for f in ['cloud0'+a for a in ['1','2']] + \
     ['bin0'+a for a in ['1','2']] + \
     ['cloud0'+a+'/c0'+b+'s0'+c for a in ['1','2'] for b in ['1','2'] for c in ['1','2']] + \
     ['foreign'] + \
     ['foreign/sub0'+a for a in ['1','2']] + \
     ['bin0'+a+'/c0'+b+'s0'+c for a in ['1','2'] for b in ['1','2'] for c in ['1','2']]:
        makedir(f)
        out(f,f+'/fileat'+f.split('/')[-1]+'.txt')

    cmd_ef(PUT+' foreign /')
    cmd_ef(SHARE+' foreign -a --with='+osvar("MEGA_EMAIL"))

    URIFOREIGNEXPORTEDFOLDER=safe_export('foreign/sub01').decode()
    URIFOREIGNEXPORTEDFILE=safe_export('foreign/sub02/fileatsub02.txt').decode()

    logging.debug("URIFOREIGNEXPORTEDFOLDER="+URIFOREIGNEXPORTEDFILE)
    logging.debug("URIFOREIGNEXPORTEDFILE="+URIFOREIGNEXPORTEDFILE)

    cmd_ef(LOGOUT)
    cmd_ef(LOGIN+" " +osvar("MEGA_EMAIL")+" "+osvar("MEGA_PWD"))
    cmd_ec(IPC+" -a "+osvar("MEGA_EMAIL_AUX"))

    cmd_ef(PUT+' foreign /')

    #~ mega-put cloud0* /
    cmd_ef(PUT+" cloud01 cloud02 /")

    #~ mega-put bin0* //bin
    cmd_ef(PUT+" bin01 bin02 //bin")

    URIEXPORTEDFOLDER=safe_export('cloud01/c01s01').decode()
    URIEXPORTEDFILE=safe_export('cloud02/fileatcloud02.txt').decode()

    logging.debug("URIEXPORTEDFOLDER="+URIEXPORTEDFOLDER)
    logging.debug("URIEXPORTEDFILE=",URIEXPORTEDFILE)


class MEGAcmdGetTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        global URIEXPORTEDFOLDER
        global URIEXPORTEDFILE
        clean_all()
        makedir('origin')
        os.chdir('origin')
        initialize_contents()
        os.chdir(ABSPWD)

        makedir('megaDls')
        makedir('localDls')

        URIEXPORTEDFOLDER=safe_export('cloud01/c01s01').decode()
        URIEXPORTEDFILE=safe_export('cloud02/fileatcloud02.txt').decode()

        clear_dls()

    @classmethod
    def tearDownClass(cls):
        clean_all()

    def tearDown(self):
        clear_dls()
        cmd_ef(CD+" /")

    def check_failed(self, o, status):
        self.assertNotEqual(status, 0, o)

    def compare(self):
        megaDls=sort(find('megaDls'))
        localDls=sort(find('localDls'))
        self.assertEqual(megaDls, localDls)
        logging.debug(f"megaDls: {megaDls}, localDls: {localDls}")

    def test_01(self):
        cmd_ef(GET+' /cloud01/fileatcloud01.txt '+ABSMEGADLFOLDER+'/')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    def test_02(self):
        cmd_ef(GET+' //bin/bin01/fileatbin01.txt '+ABSMEGADLFOLDER)
        shutil.copy2('origin/bin01/fileatbin01.txt','localDls/')
        self.compare()

    def test_03(self):
        #Test 03
        cmd_ef(GET+' //bin/bin01/fileatbin01.txt '+ABSMEGADLFOLDER+'/')
        shutil.copy2('origin/bin01/fileatbin01.txt','localDls/')
        self.compare()

    def test_04(self):
        cmd_ef(GET+' '+osvar('MEGA_EMAIL_AUX')+':foreign/fileatforeign.txt '+ABSMEGADLFOLDER+'/')
        shutil.copy2('origin/foreign/fileatforeign.txt','localDls/')
        self.compare()

    def test_05(self):
        cmd_ef(GET+' '+osvar('MEGA_EMAIL_AUX')+':foreign/fileatforeign.txt '+ABSMEGADLFOLDER+'/')
        shutil.copy2('origin/foreign/fileatforeign.txt','localDls/')
        self.compare()

    def test_06(self):
        cmd_ef(CD+' cloud01')
        cmd_ef(GET+' "*.txt" '+ABSMEGADLFOLDER+'')
        copybyfilepattern('origin/cloud01/','*.txt','localDls/')
        self.compare()

    def test_07(self):
        cmd_ef(CD+' //bin/bin01')
        cmd_ef(GET+' "*.txt" '+ABSMEGADLFOLDER+'')
        copybyfilepattern('origin/bin01/','*.txt','localDls/')
        self.compare()

    def test_08(self):
        cmd_ef(CD+' '+osvar('MEGA_EMAIL_AUX')+':foreign')
        cmd_ef(GET+' "*.txt" '+ABSMEGADLFOLDER+'')
        copybyfilepattern('origin/foreign/','*.txt','localDls/')
        self.compare()

    def test_09(self):
        cmd_ef(GET+' cloud01/c01s01 '+ABSMEGADLFOLDER+'')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()

    def test_10(self):
        cmd_ef(GET+' cloud01/c01s01 '+ABSMEGADLFOLDER+'/')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()

    def test_11(self):
        cmd_ef(GET+' cloud01/c01s01 '+ABSMEGADLFOLDER+' -m')
        shutil.copytree('origin/cloud01/c01s01/', 'localDls/', dirs_exist_ok=True)
        self.compare()


    def test_12(self):
        cmd_ef(GET+' cloud01/c01s01 '+ABSMEGADLFOLDER+'/ -m')
        shutil.copytree('origin/cloud01/c01s01/', 'localDls/', dirs_exist_ok=True)
        self.compare()

    def test_13(self):
        #Test 13
        cmd_ef(GET+' "'+URIEXPORTEDFOLDER+'" '+ABSMEGADLFOLDER+'')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()

    def test_14(self):
        cmd_ef(GET+' "'+URIEXPORTEDFILE+'" '+ABSMEGADLFOLDER+'')
        shutil.copy2('origin/cloud02/fileatcloud02.txt','localDls/')
        self.compare()

    def test_15(self):
        cmd_ef(GET+' "'+URIEXPORTEDFOLDER+'" '+ABSMEGADLFOLDER+' -m')
        shutil.copytree('origin/cloud01/c01s01/', 'localDls/', dirs_exist_ok=True)
        self.compare()

    def test_16(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' . '+ABSMEGADLFOLDER+'')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()

    def test_17(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' . '+ABSMEGADLFOLDER+' -m')
        shutil.copytree('origin/cloud01/c01s01/', 'localDls/', dirs_exist_ok=True)
        self.compare()

    def test_18(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' ./ '+ABSMEGADLFOLDER+'')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()

    def test_19(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' ./ '+ABSMEGADLFOLDER+' -m')
        shutil.copytree('origin/cloud01/c01s01/', 'localDls/', dirs_exist_ok=True)
        self.compare()

    def test_20(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' .. '+ABSMEGADLFOLDER+' -m')
        shutil.copytree('origin/cloud01/', 'localDls/', dirs_exist_ok=True)
        self.compare()

    def test_21(self):
        cmd_ef(CD+' /cloud01/c01s01')
        cmd_ef(GET+' ../ '+ABSMEGADLFOLDER+'')
        copyfolder('origin/cloud01','localDls/')
        self.compare()

    def test_22(self):
        out("existing",ABSMEGADLFOLDER+'/existing')
        cmd_ef(GET+' /cloud01/fileatcloud01.txt '+ABSMEGADLFOLDER+'/existing')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/existing (1)')
        out("existing",'localDls/existing')
        self.compare()

    def test_23(self):
        out("existing",'megaDls/existing')
        cmd_ef(GET+' /cloud01/fileatcloud01.txt megaDls/existing')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/existing (1)')
        out("existing",'localDls/existing')
        self.compare()

    def test_24(self):
        cmd_ef(GET+' cloud01/c01s01 megaDls')
        copyfolder('origin/cloud01/c01s01','localDls/')
        self.compare()


    def test_25(self):
        cmd_ef(GET+' cloud01/fileatcloud01.txt megaDls')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    @unittest.skipIf(CMDSHELL, "only for non-CMDSHELL")
    def test_26(self):
        o,status=cmd_ec(GET+' cloud01/fileatcloud01.txt /no/where')
        self.check_failed(o,status)

    @unittest.skipIf(CMDSHELL, "only for non-CMDSHELL")
    def test_27(self):
        o,status=cmd_ec(GET+' /cloud01/cloud01/fileatcloud01.txt /no/where')
        self.check_failed(o,status)

    def test_28(self):
        cmd_ef(GET+' /cloud01/fileatcloud01.txt '+ABSMEGADLFOLDER+'/newfile')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/newfile')
        self.compare()

    def test_29(self):
        os.chdir(ABSMEGADLFOLDER)
        cmd_ef(GET+' /cloud01/fileatcloud01.txt .')
        os.chdir(ABSPWD)
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    def test_30(self):
        os.chdir(ABSMEGADLFOLDER)
        cmd_ef(GET+' /cloud01/fileatcloud01.txt ./')
        os.chdir(ABSPWD)
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    def test_31(self):
        makedir(ABSMEGADLFOLDER+'/newfol')
        os.chdir(ABSMEGADLFOLDER+'/newfol')
        cmd_ef(GET+' /cloud01/fileatcloud01.txt ..')
        os.chdir(ABSPWD)
        makedir('localDls/newfol')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    def test_32(self):
        makedir(ABSMEGADLFOLDER+'/newfol')
        os.chdir(ABSMEGADLFOLDER+'/newfol')
        cmd_ef(GET+' /cloud01/fileatcloud01.txt ../')
        os.chdir(ABSPWD)
        makedir('localDls/newfol')
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    @unittest.skipIf(CMDSHELL, "only for non-CMDSHELL")
    def test_33(self):
        o,status=cmd_ec(GET+' path/to/nowhere '+ABSMEGADLFOLDER+' > /dev/null')
        self.check_failed(o,status)

    @unittest.skipIf(CMDSHELL, "only for non-CMDSHELL")
    def test_34(self):
        o,status=cmd_ec(GET+' /path/to/nowhere '+ABSMEGADLFOLDER+' > /dev/null')
        self.check_failed(o,status)

    def test_35(self):
        os.chdir(ABSMEGADLFOLDER)
        cmd_ef(GET+' /cloud01/fileatcloud01.txt')
        os.chdir(ABSPWD)
        shutil.copy2('origin/cloud01/fileatcloud01.txt','localDls/')
        self.compare()

    def test_36_import_folder(self):
        cmd_ex(RM+' -rf /imported')
        cmd_ef(MKDIR+' -p /imported')
        cmd_ef(IMPORT+' '+URIFOREIGNEXPORTEDFOLDER+' /imported')
        cmd_ef(GET+' /imported/* '+ABSMEGADLFOLDER+'')
        copyfolder('origin/foreign/sub01','localDls/')
        self.compare()

    def test_37_import_file(self):
        cmd_ex(RM+' -rf /imported')
        cmd_ef(MKDIR+' -p /imported')
        cmd_ef(IMPORT+' '+URIFOREIGNEXPORTEDFILE+' /imported')
        cmd_ef(GET+' /imported/fileatsub02.txt '+ABSMEGADLFOLDER+'')
        shutil.copy2('origin/foreign/sub02/fileatsub02.txt','localDls/')
        self.compare()


    def test_37(self):
        """get from //from/XXX"""
        cmd_ex(GET+' //from/'+osvar('MEGA_EMAIL_AUX')+':foreign/sub02/fileatsub02.txt '+ABSMEGADLFOLDER+'')
        shutil.copy2('origin/foreign/sub02/fileatsub02.txt','localDls/')
        self.compare()

if __name__ == '__main__':
    if "OUT_DIR_JUNIT_XML" in os.environ:
        unittest.main(testRunner=xmlrunner.XMLTestRunner(output=os.environ["OUT_DIR_JUNIT_XML"]), failfast=False, buffer=False, catchbreak=False, exit=False)
    else:
        unittest.main()
