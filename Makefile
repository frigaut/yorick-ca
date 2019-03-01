# these values filled in by    yorick -batch make.i
Y_MAKEDIR=/home/frigaut/packages/yorick-linux/yorick/relocate
Y_EXE=/home/frigaut/packages/yorick-linux/yorick/relocate/bin/yorick
Y_EXE_PKGS=
Y_EXE_HOME=/home/frigaut/packages/yorick-linux/yorick/relocate
Y_EXE_SITE=/home/frigaut/packages/yorick-linux/yorick/relocate
Y_HOME_PKG=

# ----------------------------------------------------- optimization flags

# options for make command line, e.g.-   make COPT=-g TGT=exe
COPT=$(COPT_DEFAULT)
TGT=$(DEFAULT_TGT)

# ------------------------------------------------ macros for this package

PKG_NAME=ca
PKG_I=ca.i

OBJS=yorick-ca.o

# change to give the executable a name other than yorick
PKG_EXENAME=yorick

# PKG_DEPLIBS=-Lsomedir -lsomelib   for dependencies of this package
PKG_DEPLIBS=-L$(EPICS)/base/lib/$(EPICS_HOST_ARCH) -lca
# set compiler (or rarely loader) flags specific to this package
PKG_CFLAGS=-Wall -I$(EPICS)/base/include -I$(EPICS)/base/include/os/$(HOST_ARCH)
#linux:
PKG_LDFLAGS=-Wl,--rpath=$(EPICS)/base/lib/$(EPICS_HOST_ARCH)
#OSX:
# PKG_LDFLAGS=-Wl #,--rpath=$(EPICS)/base/lib/$(EPICS_HOST_ARCH)

# list of additional package names you want in PKG_EXENAME
# (typically Y_EXE_PKGS should be first here)
EXTRA_PKGS=$(Y_EXE_PKGS)

# list of additional files for clean
PKG_CLEAN=

# autoload file for this package, if any
PKG_I_START=ca_start.i
# non-pkg.i include files for this package, if any
PKG_I_EXTRA=

# -------------------------------- standard targets and rules (in Makepkg)

# set macros Makepkg uses in target and dependency names
# DLL_TARGETS, LIB_TARGETS, EXE_TARGETS
# are any additional targets (defined below) prerequisite to
# the plugin library, archive library, and executable, respectively
PKG_I_DEPS=$(PKG_I)
Y_DISTMAKE=distmake

include $(Y_MAKEDIR)/Make.cfg
include $(Y_MAKEDIR)/Makepkg
include $(Y_MAKEDIR)/Make$(TGT)

# override macros Makepkg sets for rules and other macros
# Y_HOME and Y_SITE in Make.cfg may not be correct (e.g.- relocatable)
Y_HOME=$(Y_EXE_HOME)
Y_SITE=$(Y_EXE_SITE)

# reduce chance of yorick-1.5 corrupting this Makefile
MAKE_TEMPLATE = protect-against-1.5

# ------------------------------------- targets and rules for this package

# simple example:
#myfunc.o: myapi.h
# more complex example (also consider using PKG_CFLAGS above):
#myfunc.o: myapi.h myfunc.c
#	$(CC) $(CPPFLAGS) $(CFLAGS) -DMY_SWITCH -o $@ -c myfunc.c

# -------------------------------------------------------- end of Makefile


# for the binary package production (add full path to lib*.a below):
# macosx:
PKG_ARCH = $(OSTYPE)-$(MACHTYPE)
# or linux or windows
PKG_VERSION = $(shell (awk '{if ($$1=="Version:") print $$2}' $(PKG_NAME).info))
# .info might not exist, in which case he line above will exit in error.

# packages or devel_pkgs:
PKG_DEST_URL = packages

distsrc:
	make clean; rm -rf binaries
	cd ..; tar --exclude binaries --exclude .svn --exclude CVS --exclude *.spec -zcvf \
	   $(PKG_NAME)-$(PKG_VERSION)-src.tgz yorick-$(PKG_NAME)-$(PKG_VERSION);\
	ncftpput -f $(HOME)/.ncftp/maumae www/yorick/$(PKG_DEST_URL)/src/ \
	   $(PKG_NAME)-$(PKG_VERSION)-src.tgz
	cd ..; ncftpput -f $(HOME)/.ncftp/maumae www/yorick/contrib/ \
	   $(PKG_NAME)-$(PKG_VERSION)-src.tgz


# -------------------------------------------------------- end of Makefile
