##  All the actual installation work of any files in the samples directory
##  is done via the site directory, so that one can maintain one's news
##  configuration in the site directory and use make commands to update the
##  server automatically.  All this Makefile does is run fixconfig or
##  fixscript on a few files that need configure results (and remove them on
##  make clean).

include ../Makefile.global

top	      = ..

ALL           = buffindexed.conf inn.conf innreport.conf newsfeeds \
		nnrpd_auth.pl nnrpd_access.pl

all: $(ALL)

install:
bootstrap:

clean clobber distclean maintclean:
	rm -f $(ALL)

depend:

profiled: all

$(FIXCONFIG) $(FIXSCRIPT):
	@echo Run configure before running make.  See INSTALL for details.
	@exit 1


##  Build rules.

FIXC		= $(FIXCONFIG)
FIXS            = $(FIXSCRIPT)

buffindexed.conf:        buffindexed.conf.in        $(FIXC) ; $(FIXC) $@.in
inn.conf:                inn.conf.in                $(FIXC) ; $(FIXC) $@.in
innreport.conf:          innreport.conf.in          $(FIXC) ; $(FIXC) $@.in
newsfeeds:               newsfeeds.in               $(FIXC) ; $(FIXC) $@.in
nnrpd_auth.pl:           nnrpd_auth.pl.in           $(FIXS) ; $(FIXS) $@.in
nnrpd_access.pl:         nnrpd_access.pl.in         $(FIXS) ; $(FIXS) $@.in
