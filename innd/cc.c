/*  $Id$
**
**  Routines for the control channel.
**
**  Create a Unix-domain datagram socket that processes on the local server
**  send messages to.  The control channel is used only by ctlinnd to tell
**  the server to perform special functions.  We use datagrams so that we
**  don't need to do an accept() and tie up another descriptor.  recvfrom
**  seems to be broken on several systems, so the client passes in the
**  socket name.
**
**  This module completely rips away all pretense of software layering.
*/

#include "config.h"
#include "clibrary.h"
#include <netinet/in.h>

#ifdef HAVE_UNIX_DOMAIN_SOCKETS
# include <sys/un.h>
#endif

#include "inn/qio.h"
#include "innd.h"
#include "inndcomm.h"

/*
**  An entry in the dispatch table.  The name, and implementing function,
**  of every command we support.
*/
typedef struct _CCDISPATCH {
    char Name;
    int	argc;
    const char * (*Function)(char *av[]);
} CCDISPATCH;


static const char *	CCallow(char *av[]);
static const char *	CCbegin(char *av[]);
static const char *	CCchgroup(char *av[]);
static const char *	CCdrop(char *av[]);
static const char *	CCfeedinfo(char *av[]);
static const char *	CCflush(char *av[]);
static const char *	CCflushlogs(char *unused[]);
static const char *	CCgo(char *av[]);
static const char *	CChangup(char *av[]);
static const char *	CCreserve(char *av[]);
static const char *	CClogmode(char *unused[]);
static const char *	CCmode(char *unused[]);
static const char *	CCname(char *av[]);
static const char *	CCnewgroup(char *av[]);
static const char *	CCparam(char *av[]);
static const char *	CCpause(char *av[]);
static const char *	CCreaders(char *av[]);
static const char *	CCreject(char *av[]);
static const char *	CCreload(char *av[]);
static const char *	CCrenumber(char *av[]);
static const char *	CCrmgroup(char *av[]);
static const char *	CCsend(char *av[]);
static const char *	CCshutdown(char *av[]);
static const char *	CCsignal(char *av[]);
static const char *	CCstatus(char *av[]);
static const char *	CCthrottle(char *av[]);
static const char *	CCtimer(char *av[]);
static const char *	CCtrace(char *av[]);
static const char *	CCxabort(char *av[]);
static const char *	CCxexec(char *av[]);
#if defined(DO_TCL)
static const char *	CCfilter(char *av[]);
#endif /* defined(DO_TCL) */
#if defined(DO_PERL)
static const char *	CCperl(char *av[]);
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
static const char *	CCpython(char *av[]);
#endif /* defined(DO_PYTHON) */
static const char *	CClowmark(char *av[]);


static char		*CCpath = NULL;
static char		**CCargv;
static char		CCnosite[] = "1 No such site";
static char		CCwrongtype[] = "1 Wrong site type";
static char		CCnogroup[] = "1 No such group";
static char		CCnochannel[] = "1 No such channel";
static char		CCnoreason[] = "1 Empty reason";
static char		CCbigreason[] = "1 Reason too long";
static char		CCnotrunning[] = "1 Must be running";
static BUFFER		CCreply;
static CHANNEL		*CCchan;
static int		CCwriter;
static CCDISPATCH	CCcommands[] = {
    {	SC_ADDHIST,	5, CCaddhist	},
    {	SC_ALLOW,	1, CCallow	},
    {	SC_BEGIN,	1, CCbegin	},
    {	SC_CANCEL,	1, CCcancel	},
    {	SC_CHANGEGROUP,	2, CCchgroup	},
    {	SC_CHECKFILE,	0, CCcheckfile	},
    {	SC_DROP,	1, CCdrop	},
    {	SC_FEEDINFO,	1, CCfeedinfo	},
#if defined(DO_TCL)
    {	SC_FILTER,	1, CCfilter     },
#endif /* defined(DO_TCL) */
#if defined(DO_PERL)
    {	SC_PERL,	1, CCperl       },
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    {	SC_PYTHON,	1, CCpython     },
#endif /* defined(DO_PYTHON) */
    {	SC_FLUSH,	1, CCflush	},
    {	SC_FLUSHLOGS,	0, CCflushlogs	},
    {	SC_GO,		1, CCgo		},
    {	SC_HANGUP,	1, CChangup	},
    {	SC_LOGMODE,	0, CClogmode	},
    {	SC_MODE,	0, CCmode	},
    {	SC_NAME,	1, CCname	},
    {	SC_NEWGROUP,	3, CCnewgroup	},
    {	SC_PARAM,	2, CCparam	},
    {	SC_PAUSE,	1, CCpause	},
    {	SC_READERS,	2, CCreaders	},
    {	SC_REJECT,	1, CCreject	},
    {	SC_RENUMBER,	1, CCrenumber	},
    {	SC_RELOAD,	2, CCreload	},
    {	SC_RESERVE,	1, CCreserve	},
    {	SC_RMGROUP,	1, CCrmgroup	},
    {	SC_SEND,	2, CCsend	},
    {	SC_SHUTDOWN,	1, CCshutdown	},
    {	SC_SIGNAL,	2, CCsignal	},
    {	SC_STATUS,	1, CCstatus	},
    {	SC_THROTTLE,	1, CCthrottle	},
    {   SC_TIMER,       1, CCtimer      },
    {	SC_TRACE,	2, CCtrace	},
    {	SC_XABORT,	1, CCxabort	},
    {	SC_LOWMARK,	1, CClowmark	},
    {	SC_XEXEC,	1, CCxexec	}
};

static RETSIGTYPE CCresetup(int unused);


void
CCcopyargv(char *av[])
{
    register char	**v;
    register int	i;

    /* Get the vector size. */
    for (i = 0; av[i]; i++)
	continue;

    /* Get the vector, copy each element. */
    for (v = CCargv = NEW(char*, i + 1); *av; av++) {
	/* not to renumber */
	if (strncmp(*av, "-r", 2) == 0)
	    continue;
	*v++ = COPY(*av);
    }
    *v = NULL;
}


/*
**  Return a string representing our operating mode.
*/
static const char *
CCcurrmode(void)
{
    static char		buff[32];

    /* Server's mode. */
    switch (Mode) {
    default:
	(void)sprintf(buff, "Unknown %d", Mode);
	return buff;
    case OMrunning:
	return "running";
    case OMpaused:
	return "paused";
    case OMthrottled:
	return "throttled";
    }
}


/*
**  Add <> around Message-ID if needed.
*/
static const char *
CCgetid(char *p, const char **store)
{
    static char		NULLMESGID[] = "1 Empty Message-ID";
    static BUFFER	Save;
    int			i;

    if (*p == '\0')
	return NULLMESGID;
    if (*p == '<') {
	if (p[1] == '\0' || p[1] == '>')
	    return NULLMESGID;
	*store = p;
	return NULL;
    }

    /* Make sure the Message-ID buffer has room. */
    i = 1 + strlen(p) + 1 + 1;
    if (Save.Data == NULL) {
	Save.Size = i;
	Save.Data = NEW(char, Save.Size);
    }
    else if (Save.Size < i) {
	Save.Size = i;
	RENEW(Save.Data, char, Save.Size);
    }
    *store = Save.Data;
    (void)sprintf((char *)*store, "<%s>", p);
    return NULL;
}


/*
**  Abort and dump core.
*/
static const char *
CCxabort(char *av[])
{
    syslog(L_FATAL, "%s abort %s", LogName, av[0]);
    abort();
    syslog(L_FATAL, "%s cant abort %m", LogName);
    CleanupAndExit(1, av[0]);
    /* NOTREACHED */
}


/*
**  Do the work needed to add a history entry.
*/
const char *
CCaddhist(char *av[])
{
    static char		DIGITS[] = "0123456789";
    ARTDATA		Data;
    const char *	p;
    bool		ok;
    HASH                hash;
    int			i;

    /* Check to see if we were passed a hash first. */
    i = strlen(av[0]);
    if (av[0][0]=='[' && av[0][i-1] == ']') {
	if (i != ((sizeof(HASH) * 2) + 2))
	    return "1 Bad Hash";
	if (av[4] != NULL && *av[4] != '\0') {
	    if (!IsToken(av[4]))
		return "1 Bad Token";
	} 

        hash = TextToHash(&av[0][1]);
	/* Put something bogus in here.  This should never be referred
	   to unless someone tries to add a [msgidhash].... history
	   entry to an innd set with 'storageapi' off, which isn't a
	   very sensible thing to do. */
	Data.MessageID = "<bogus@messsageid>";
    } else {
        /* Try to take what we were given as a <messageid> */
        if ((p = CCgetid(av[0], &Data.MessageID)) != NULL)
	  return p;
	hash = HashMessageID(Data.MessageID);
    }

    /* If paused, don't try to use the history database since expire may be
       running */
    if (Mode == OMpaused)
	return "1 Server paused";

    /* If throttled by admin, briefly open the history database. */
    if (Mode != OMrunning) {
	if (ThrottledbyIOError)
	    return "1 Server throttled";
	HISsetup();
    }

    if (HIShavearticle(hash)) {
	if (Mode != OMrunning) HISclose();
	return "1 Duplicate";
    }
    if (Mode != OMrunning) HISclose();
    if (strspn(av[1], DIGITS) != strlen(av[1]))
	return "1 Bad arrival date";
    Data.Arrived = atol(av[1]);
    if (strspn(av[2], DIGITS) != strlen(av[2]))
	return "1 Bad expiration date";
    Data.Expires = atol(av[2]);
    if (strspn(av[3], DIGITS) != strlen(av[3]))
	return "1 Bad posted date";
    Data.Posted = atol(av[3]);

    if (Mode == OMrunning)
	ok = HISwrite(&Data, hash, av[4]);
    else {
	/* Possible race condition, but documented in ctlinnd manpage. */
	HISsetup();
	ok = HISwrite(&Data, hash, av[4]);
	HISclose();
    }
    return ok ? NULL : "1 Write failed";
}


/*
**  Do the work to allow foreign connectiosn.
*/
static const char *
CCallow(char *av[])
{
    char	*p;

    if (RejectReason == NULL)
	return "1 Already allowed";
    p = av[0];
    if (*p && !EQ(p, RejectReason))
	return "1 Wrong reason";
    DISPOSE(RejectReason);
    RejectReason = NULL;
    return NULL;
}


/*
**  Do the work needed to start feeding a (new) site.
*/
static const char *
CCbegin(char *av[])
{
    SITE	*sp;
    int		i;
    int		length;
    char	*p;
    const char	*p1;
    char	**strings;
    NEWSGROUP	*ngp;
    const char	*error;
    char	*subbed;
    char	*poison;

    /* If site already exists, drop it. */
    if (SITEfind(av[0]) != NULL && (p1 = CCdrop(av)) != NULL)
	return p1;

    /* Find the named site. */
    length = strlen(av[0]);
    for (strings = SITEreadfile(TRUE), i = 0; (p = strings[i]) != NULL; i++)
	if ((p[length] == NF_FIELD_SEP || p[length] == NF_SUBFIELD_SEP)
	 && caseEQn(p, av[0], length)) {
	    p = COPY(p);
	    break;
	}
    if (p == NULL)
	return CCnosite;

    if (p[0] == 'M' && p[1] == 'E' && p[2] == NF_FIELD_SEP)
	sp = &ME;
    else {
	/* Get space for the new site entry, and space for it in all
	 * the groups. */
	for (i = nSites, sp = Sites; --i >= 0; sp++)
	    if (sp->Name == NULL)
		break;
	if (i < 0) {
	    nSites++;
	    RENEW(Sites, SITE, nSites);
	    sp = &Sites[nSites - 1];
	    sp->Next = sp->Prev = NOSITE;
	    for (i = nGroups, ngp = Groups; --i >= 0; ngp++) {
		RENEW(ngp->Sites, int, nSites);
		RENEW(ngp->Poison, int, nSites);
	    }
	}
    }

    /* Parse. */
    subbed = NEW(char, nGroups);
    poison = NEW(char, nGroups);
    error = SITEparseone(p, sp, subbed, poison);
    DISPOSE(subbed);
    DISPOSE(poison);
    if (error != NULL) {
	DISPOSE((void *)p);
	syslog(L_ERROR, "%s bad_newsfeeds %s", av[0], error);
	return "1 Parse error";
    }

    if (sp != &ME && (!SITEsetup(sp) || !SITEfunnelpatch()))
	return "1 Startup error";
    SITEforward(sp, "begin");
    return NULL;
}


/*
**  Common code to change a group's flags.
*/
static const char *
CCdochange(register NEWSGROUP *ngp, char *Rest)
{
    int			length;
    char		*p;

    if (ngp->Rest[0] == Rest[0]) {
	length = strlen(Rest);
	if (ngp->Rest[length] == '\n' && EQn(ngp->Rest, Rest, length))
	    return "0 Group status unchanged";
    }
    if (Mode != OMrunning)
	return CCnotrunning;

    p = COPY(ngp->Name);
    if (!ICDchangegroup(ngp, Rest)) {
	syslog(L_NOTICE, "%s cant change_group %s to %s", LogName, p, Rest);
	DISPOSE(p);
	return "1 Change failed (probably can't write active?)";
    }
    syslog(L_NOTICE, "%s change_group %s to %s", LogName, p, Rest);
    DISPOSE(p);
    return NULL;
}


/*
**  Change the mode of a newsgroup.
*/
static const char *
CCchgroup(char *av[])
{
    NEWSGROUP	*ngp;
    char	*Rest;

    if ((ngp = NGfind(av[0])) == NULL)
	return CCnogroup;
    Rest = av[1];
    if (Rest[0] != NF_FLAG_ALIAS) {
	Rest[1] = '\0';
	if (CTYPE(isupper, Rest[0]))
	    Rest[0] = tolower(Rest[0]);
    }
    return CCdochange(ngp, Rest);
}


/*
**  Cancel a message.
*/
const char *
CCcancel(char *av[])
{
    ARTDATA	Data;
    const char *	p;

    Data.Posted = Data.Arrived = Now.time;
    Data.Expires = 0;
    Data.Feedsite = "?";
    if ((p = CCgetid(av[0], &Data.MessageID)) != NULL)
	return p;

    if (Mode == OMrunning)
	ARTcancel(&Data, Data.MessageID, TRUE);
    else {
	/* If paused, don't try to use the history database since expire may be
	   running */
	if (Mode == OMpaused)
	    return "1 Server paused";
	if (ThrottledbyIOError)
	    return "1 Server throttled";
	/* Possible race condition, but documented in ctlinnd manpage. */
	HISsetup();
	ARTcancel(&Data, Data.MessageID, TRUE);
	HISclose();
    }
    if (innconf->logcancelcomm)
	syslog(L_NOTICE, "%s cancelled %s", LogName, Data.MessageID);
    return NULL;
}


/*
**  Syntax-check the newsfeeds file.
*/
const char *
CCcheckfile(char *unused[])
{
    register char	**strings;
    register char	*p;
    register int	i;
    register int	errors;
    const char *		error;
    SITE		fake;

    unused = unused;		/* ARGSUSED */
    /* Parse all site entries. */
    strings = SITEreadfile(FALSE);
    fake.Buffer.Size = 0;
    fake.Buffer.Data = NULL;
    for (errors = 0, i = 0; (p = strings[i]) != NULL; i++) {
	if ((error = SITEparseone(p, &fake, (char *)NULL, (char *)NULL)) != NULL) {
	    syslog(L_ERROR, "%s bad_newsfeeds %s", MaxLength(p, p), error);
	    errors++;
	}
	SITEfree(&fake);
    }
    DISPOSE(strings);

    if (errors == 0)
	return NULL;

    if (CCreply.Data == NULL) {
	/* If we got the "-s" flag, then CCsetup hasn't been called yet. */
	CCreply.Size = SMBUF;
	CCreply.Data = NEW(char, CCreply.Size);
    } else if (CCreply.Size < SMBUF) {
	CCreply.Size = SMBUF;
	RENEW(CCreply.Data, char, CCreply.Size);
    }

    (void)sprintf(CCreply.Data, "1 Found %d errors -- see syslog", errors);
    return CCreply.Data;
}


/*
**  Drop a site.
*/
static const char *
CCdrop(char *av[])
{
    SITE		*sp;
    register NEWSGROUP	*ngp;
    register int	*ip;
    register int	idx;
    register int	i;
    register int	j;

    if ((sp = SITEfind(av[0])) == NULL)
	return CCnosite;

    SITEdrop(sp);

    /* Loop over all groups, and if the site is in a group, clobber it. */
    for (idx = sp - Sites, i = nGroups, ngp = Groups; --i >= 0; ngp++) {
	for (j = ngp->nSites, ip = ngp->Sites; --j >= 0; ip++)
	    if (*ip == idx)
		*ip = NOSITE;
	for (j = ngp->nPoison, ip = ngp->Poison; --j >= 0; ip++)
	    if (*ip == idx)
		*ip = NOSITE;
    }

	return NULL;
}

/*
**  Return info on the feeds for one, or all, sites
*/
static const char *
CCfeedinfo(char *av[])
{
    register SITE	*sp;
    register char	*p;
    register int	i;

    BUFFset(&CCreply, "0 ", 2);
    p = av[0];
    if (*p != '\0') {
	if ((sp = SITEfind(p)) == NULL)
	    return "1 No such site";

	SITEinfo(&CCreply, sp, TRUE);
	while ((sp = SITEfindnext(p, sp)) != NULL)
	    SITEinfo(&CCreply, sp, TRUE);
    }
    else
	for (i = nSites, sp = Sites; --i >= 0; sp++)
	    if (sp->Name)
		SITEinfo(&CCreply, sp, FALSE);

    BUFFappend(&CCreply, "", 1);
    return CCreply.Data;
}


#if defined(DO_TCL)
static const char *
CCfilter(char *av[])
{
    char	*p;

    switch (av[0][0]) {
    default:
	return "1 Bad flag";
    case 'y':
	if (TCLFilterActive)
	    return "1 tcl filter already enabled";
	TCLfilter(TRUE);
	break;
    case 'n':
	if (!TCLFilterActive)
	    return "1 tcl filter already disabled";
	TCLfilter(FALSE);
	break;
    }
    return NULL;
}
#endif /* defined(DO_TCL) */


#if defined(DO_PERL)

#include <EXTERN.h>
#include <perl.h>

extern CV *perl_filter_cv;

/* From lib/perl.c. */
extern void PerlFilter(bool value);
extern int  PERLreadfilter(char *filterfile, char *function);

static const char *
CCperl(char *av[])
{
    extern int	PerlFilterActive;

    switch (av[0][0]) {
    default:
	return "1 Bad flag";
    case 'y':
	if (PerlFilterActive)
	    return "1 Perl filter already enabled";
        else if (perl_filter_cv == NULL)
            return "1 Perl filter not defined" ;
	PerlFilter(TRUE);
	break;
    case 'n':
	if (!PerlFilterActive)
	    return "1 Perl filter already disabled";
	PerlFilter(FALSE);
	break;
    }
    return NULL;
}
#endif /* defined(DO_PERL) */


#if defined(DO_PYTHON)
static const char *
CCpython(char *av[])
{
    return PYcontrol(av);
}
#endif /* defined(DO_PYTHON) */


/*
**  Flush all sites or one site.
*/
static const char *
CCflush(char *av[])
{
    register SITE	*sp;
    register int	i;
    register char	*p;

    p = av[0];
    if (*p == '\0') {
	ICDwrite();
	for (sp = Sites, i = nSites; --i >= 0; sp++)
	    SITEflush(sp, TRUE);
	syslog(L_NOTICE, "%s flush_all", LogName);
    }
    else  {
	if ((sp = SITEfind(p)) == NULL)
	    return CCnosite;
	syslog(L_NOTICE, "%s flush", sp->Name);
	SITEflush(sp, TRUE);
    }
    return NULL;
}


/*
**  Flush the log files.
*/
static const char *
CCflushlogs(char *unused[])
{
    unused = unused;		/* ARGSUSED */

    if (Debug)
	return "1 In debug mode";

    ICDwrite();
    syslog(L_NOTICE, "%s flushlogs %s", LogName, CCcurrmode());
    ReopenLog(Log);
    ReopenLog(Errlog);
    return NULL;
}


/*
**  Leave paused or throttled mode.
*/
static const char *
CCgo(char *av[])
{
    static char	YES[] = "y";
    char	*p;

    p = av[0];
    if (Reservation && EQ(p, Reservation)) {
	DISPOSE(Reservation);
	Reservation = NULL;
    }
    if (RejectReason && EQ(p, RejectReason)) {
	DISPOSE(RejectReason);
	RejectReason = NULL;
    }

    if (Mode == OMrunning)
	return "1 Already running";
    if (*p && !EQ(p, ModeReason))
	return "1 Wrong reason";

#if defined(DO_PERL)
    PLmode(Mode, OMrunning, p);
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    PYmode(Mode, OMrunning, p);
#endif /* defined(DO_PYTHON) */
    
    DISPOSE(ModeReason);
    ModeReason = NULL;
    Mode = OMrunning;
    ThrottledbyIOError = FALSE;

    if (NNRPReason && !innconf->readerswhenstopped) {
	av[0] = YES;
	av[1] = p;
	(void)CCreaders(av);
    }
    if (ErrorCount < 0)
	ErrorCount = IO_ERROR_COUNT;
    HISsetup();
    syslog(L_NOTICE, "%s running", LogName);
    if (ICDneedsetup)
	ICDsetup(TRUE);
    SCHANwakeup(&Mode);
    return NULL;
}


/*
**  Hangup a channel.
*/
static const char *
CChangup(char *av[])
{
    register CHANNEL	*cp;
    register int	fd;
    register char	*p;
    int			i;

    /* Parse the argument, a channel number. */
    for (p = av[0], fd = 0; *p; p++) {
	if (!CTYPE(isdigit, *p))
	    return "1 Bad channel number";
	fd = fd * 10 + *p - '0';
    }

    /* Loop over all channels for the desired one. */
    for (i = 0; (cp = CHANiter(&i, CTany)) != NULL; )
	if (cp->fd == fd) {
	    p = CHANname(cp);
	    switch (cp->Type) {
	    default:
		(void)sprintf(CCreply.Data, "1 Can't close %s", p);
		return CCreply.Data;
	    case CTexploder:
	    case CTprocess:
	    case CTfile:
	    case CTnntp:
	    case CTreject:
		syslog(L_NOTICE, "%s hangup", p);
		CHANclose(cp, p);
		return NULL;
	    }
	}
    return "1 Not active";
}


/*
**  Return our operating mode.
*/
static const char *
CCmode(char *unused[])
{
    register char	*p;
    register int	i;
    int			h;
    char		buff[BUFSIZ];
#if defined(DO_PERL)
    extern int		PerlFilterActive;
#endif /* defined(DO_PERL) */

    unused = unused;		/* ARGSUSED */

    /* nb: We assume here that BUFSIZ is >= 512, and that none of
     * ModeReason RejectReason Reservation or NNRPReason is longer than
     * MAX_REASON_LEN bytes (or actually, the average of their lengths is
     * <= MAX_REASON_LEN).  If this is not true, the sprintf's/strcpy's
     * below are likely to overflow buff with somewhat nasty
     * consequences...
     */

    p = buff;
    p += strlen(strcpy(buff, "0 Server "));

    /* Server's mode. */
    switch (Mode) {
    default:
	(void)sprintf(p, "Unknown %d", Mode);
	p += strlen(p);
	break;
    case OMrunning:
	p += strlen(strcpy(p, "running"));
	break;
    case OMpaused:
	p += strlen(strcpy(p, "paused "));
	p += strlen(strcpy(p, ModeReason));
	break;
    case OMthrottled:
	p += strlen(strcpy(p, "throttled "));
	p += strlen(strcpy(p, ModeReason));
	break;
    }
    *p++ = '\n';
    if (RejectReason) {
	p += strlen(strcpy(p, "Rejecting "));
	p += strlen(strcpy(p, RejectReason));
    }
    else
	p += strlen(strcpy(p, "Allowing remote connections"));

    /* Server parameters. */
    for (i = 0, h = 0; CHANiter(&h, CTnntp) != NULL; )
	i++;
    *p++ = '\n';
    (void)sprintf(p, "Parameters c %ld i %d (%d) l %ld o %d t %ld H %d T %d X %d %s %s",
	    (long)innconf->artcutoff / (24L * 60L * 60L),
	    innconf->maxconnections, i,
	    innconf->maxartsize, MaxOutgoing, (long)TimeOut.tv_sec,
	    RemoteLimit, RemoteTotal, (int) RemoteTimer,
	    innconf->xrefslave ? "slave" : "normal",
	    AnyIncoming ? "any" : "specified");
    p += strlen(p);

    /* Reservation. */
    *p++ = '\n';
    if (Reservation) {
	(void)sprintf(p, "Reserved %s", Reservation);
	p += strlen(p);
    }
    else
	p += strlen(strcpy(p, "Not reserved"));

    /* Newsreaders. */
    *p++ = '\n';
    p += strlen(strcpy(p, "Readers "));
    if (innconf->readerswhenstopped)
	p += strlen(strcpy(p, "follow "));
    else
	p += strlen(strcpy(p, "separate "));
    if (NNRPReason == NULL)
	p += strlen(strcpy(p, "enabled"));
    else {
	(void)sprintf(p, "disabled %s", NNRPReason);
	p += strlen(p);
    }

#if defined(DO_TCL)
    *p++ = '\n';
    p += strlen(strcpy(p, "Tcl filtering "));
    if (TCLFilterActive)
	p += strlen(strcpy(p, "enabled"));
    else
	p += strlen(strcpy(p, "disabled"));
#endif /* defined(DO_TCL) */
#if defined(DO_PERL)
    *p++ = '\n';
    p += strlen(strcpy(p, "Perl filtering "));
    if (PerlFilterActive)
        p += strlen(strcpy(p, "enabled"));
    else
        p += strlen(strcpy(p, "disabled"));
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    *p++ = '\n';
    p += strlen(strcpy(p, "Python filtering "));
    if (PythonFilterActive)
        p += strlen(strcpy(p, "enabled"));
    else
        p += strlen(strcpy(p, "disabled"));
#endif /* defined(DO_PYTHON) */

     i = strlen(buff);
    if (CCreply.Size <= i) {
	CCreply.Size = i;
	RENEW(CCreply.Data, char, CCreply.Size + 1);
    }
    (void)strcpy(CCreply.Data, buff);
    return CCreply.Data;
}


/*
**  Log our operating mode (via syslog).
*/
static const char *
CClogmode(char *unused[])
{
    unused = unused;		/* ARGSUSED */
    syslog(L_NOTICE, "%s servermode %s", LogName, CCcurrmode());
    return NULL;
}


/*
**  Name the channels.  ("Name the bats -- simple names.")
*/
static const char *
CCname(char *av[])
{
    static char		NL[] = "\n";
    static char		NIL[] = "\0";
    char		buff[SMBUF];
    CHANNEL		*cp;
    char		*p;
    int			count;
    int			i;

    p = av[0];
    if (*p != '\0') {
	if ((cp = CHANfromdescriptor(atoi(p))) == NULL)
	    return COPY(CCnochannel);
	(void)sprintf(CCreply.Data, "0 %s", CHANname(cp));
	return CCreply.Data;
    }
    BUFFset(&CCreply, "0 ", 2);
    for (count = 0, i = 0; (cp = CHANiter(&i, CTany)) != NULL; ) {
	if (cp->Type == CTfree)
	    continue;
	if (++count > 1)
	    BUFFappend(&CCreply, NL, 1);
	p = CHANname(cp);
	BUFFappend(&CCreply, p, strlen(p));
	switch (cp->Type) {
	case CTremconn:
	    sprintf(buff, ":remconn::");
	    break;
	case CTreject:
	    sprintf(buff, ":reject::");
	    break;
	case CTnntp:
           sprintf(buff, ":%s:%ld:%s",
                    cp->State == CScancel ? "cancel" : "nntp",
                    (long) Now.time - cp->LastActive,
                    (cp->MaxCnx > 0 && cp->ActiveCnx == 0) ? "paused" : "");
	    break;
	case CTlocalconn:
	    sprintf(buff, ":localconn::");
	    break;
	case CTcontrol:
	    sprintf(buff, ":control::");
	    break;
	case CTfile:
	    sprintf(buff, "::");
	    break;
	case CTexploder:
	    sprintf(buff, ":exploder::");
	    break;
	case CTprocess:
	    sprintf(buff, ":");
	    break;
	default:
	    sprintf(buff, ":unknown::");
	    break;
	}
	p = buff;
	BUFFappend(&CCreply, p, strlen(p));
    }
    BUFFappend(&CCreply, NIL, 1);
    return CCreply.Data;
}


/*
**  Create a newsgroup.
*/
static const char *
CCnewgroup(char *av[])
{
    static char		*TIMES = NULL;
    static char		WHEN[] = "updating active.times";
    register int	fd;
    register char	*p;
    NEWSGROUP		*ngp;
    char		*Name;
    char		*Rest;
    const char *		who;
    char		*buff;
    int			oerrno;

    if (TIMES == NULL)
	TIMES = COPY(cpcatpath(innconf->pathdb, _PATH_ACTIVETIMES));

    Name = av[0];
    if (Name[0] == '.' || strspn(Name, "0123456789") == strlen(Name))
	return "1 Illegal newsgroup name";
    for (p = Name; *p; p++)
	if (*p == '.') {
	    if (p[1] == '.' || p[1] == '\0')
		return "1 Double or trailing period in newsgroup name";
	}
	else if (ISWHITE(*p) || *p == ':' || *p == '!' || *p == '/')
	    return "1 Illegal character in newsgroup name";

    Rest = av[1];
    if (Rest[0] != NF_FLAG_ALIAS) {
	Rest[1] = '\0';
	if (CTYPE(isupper, Rest[0]))
	    Rest[0] = tolower(Rest[0]);
    }
    if (strlen(Name) + strlen(Rest) > SMBUF - 24)
	return "1 Name too long";

    if ((ngp = NGfind(Name)) != NULL)
	return CCdochange(ngp, Rest);

    if (Mode == OMthrottled && ThrottledbyIOError)
	return "1 server throttled";

    /* Update the log of groups created.  Don't use stdio because SunOS
     * 4.1 has broken libc which can't handle fd's greater than 127. */
    if ((fd = open(TIMES, O_WRONLY | O_APPEND | O_CREAT, 0664)) < 0) {
	oerrno = errno;
	syslog(L_ERROR, "%s cant open %s %m", LogName, TIMES);
	IOError(WHEN, oerrno);
    }
    else {
	who = av[2];
	if (*who == '\0')
	    who = NEWSMASTER;

	/* %s + ' ' + %ld + ' ' + %s + '\n' + terminator */
	buff = NEW(char, strlen(Name) + 1 + 20 + 1 + strlen(who) + 1 + 1);

	(void)sprintf(buff, "%s %ld %s\n", Name, Now.time, who);
	if (xwrite(fd, buff, strlen(buff)) < 0) {
	    oerrno = errno;
	    syslog(L_ERROR, "%s cant write %s %m", LogName, TIMES);
	    IOError(WHEN, oerrno);
	}

	DISPOSE(buff);
	
	if (close(fd) < 0) {
	    oerrno = errno;
	    syslog(L_ERROR, "%s cant close %s %m", LogName, TIMES);
	    IOError(WHEN, oerrno);
	}
    }

    /* Update the in-core data. */
    if (!ICDnewgroup(Name, Rest))
	return "1 Failed";
    syslog(L_NOTICE, "%s newgroup %s as %s", LogName, Name, Rest);

    return NULL;
}


/*
**  Parse and set a boolean flag.
*/
static bool
CCparsebool(char name, bool *bp, char value)
{
    switch (value) {
    default:
	return FALSE;
    case 'y':
	*bp = FALSE;
	break;
    case 'n':
	*bp = TRUE;
	break;
    }
    syslog(L_NOTICE, "%s changed -%c %c", LogName, name, value);
    return TRUE;
}


/*
**  Change a running parameter.
*/
static const char *
CCparam(char *av[])
{
    static char	BADVAL[] = "1 Bad value";
    char	*p;
    int		temp;

    p = av[1];
    switch (av[0][0]) {
    default:
	return "1 Unknown parameter";
    case 'a':
	if (!CCparsebool('a', &AnyIncoming, *p))
	    return BADVAL;
	break;
    case 'c':
	innconf->artcutoff = atoi(p) * 24 * 60 * 60;
	syslog(L_NOTICE, "%s changed -c %d", LogName, innconf->artcutoff);
	break;
    case 'i':
	innconf->maxconnections = atoi(p);
	syslog(L_NOTICE, "%s changed -i %d", LogName, innconf->maxconnections);
	break;
    case 'l':
	innconf->maxartsize = atol(p);
	syslog(L_NOTICE, "%s changed -l %ld", LogName, innconf->maxartsize);
	break;
    case 'n':
	if (!CCparsebool('n', &innconf->readerswhenstopped, *p))
	    return BADVAL;
	break;
    case 'o':
	MaxOutgoing = atoi(p);
	syslog(L_NOTICE, "%s changed -o %d", LogName, MaxOutgoing);
	break;
    case 't':
	TimeOut.tv_sec = atol(p);
	syslog(L_NOTICE, "%s changed -t %ld", LogName, (long)TimeOut.tv_sec);
	break;
    case 'H':
	RemoteLimit = atoi(p);
	syslog(L_NOTICE, "%s changed -H %d", LogName, RemoteLimit);
	break;
    case 'T':
        temp = atoi(p);
	if (temp > REMOTETABLESIZE) {
	    syslog(L_NOTICE, "%s -T must be lower than %d",
		   LogName, REMOTETABLESIZE+1);
	    temp = REMOTETABLESIZE;
	}
	syslog(L_NOTICE, "%s changed -T from %d to %d",
	       LogName, RemoteTotal, temp);
	RemoteTotal = temp;
	break;
    case 'X':
	RemoteTimer = (time_t) atoi(p);
	syslog(L_NOTICE, "%s changed -X %d", LogName, (int) RemoteTimer);
	break;
    }
    return NULL;
}


/*
**  Common code to implement a pause or throttle.
*/
const char *
CCblock(OPERATINGMODE NewMode, char *reason)
{
    static char		NO[] = "n";
    char *		av[2];

    if (*reason == '\0')
	return CCnoreason;

    if (strlen(reason) > MAX_REASON_LEN) /* MAX_REASON_LEN is as big as is safe */
	return CCbigreason;

    if (Reservation) {
	if (!EQ(reason, Reservation)) {
	    (void)sprintf(CCreply.Data, "1 Reserved \"%s\"", Reservation);
	    return CCreply.Data;
	}
	DISPOSE(Reservation);
	Reservation = NULL;
    }

#if defined(DO_PERL)
    PLmode(Mode, NewMode, reason);
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    PYmode(Mode, NewMode, reason);
#endif /* defined(DO_PYTHON) */

    ICDwrite();
    HISclose();
    Mode = NewMode;
    if (ModeReason)
	DISPOSE(ModeReason);
    ModeReason = COPY(reason);
    if (NNRPReason == NULL && !innconf->readerswhenstopped) {
	av[0] = NO;
	av[1] = ModeReason;
	(void)CCreaders(av);
    }
    syslog(L_NOTICE, "%s %s %s",
	LogName, NewMode == OMpaused ? "paused" : "throttled", reason);
    return NULL;
}


/*
**  Enter paused mode.
*/
static const char *
CCpause(char *av[])
{
    switch (Mode) {
    case OMrunning:
	return CCblock(OMpaused, av[0]);
    case OMpaused:
	return "1 Already paused";
    case OMthrottled:
	return "1 Already throttled";
    }
    return "1 Unknown mode";
}


/*
**  Allow or disallow newsreaders.
*/
static const char *
CCreaders(char *av[])
{
    const char	*p;

    switch (av[0][0]) {
    default:
	return "1 Bad flag";
    case 'y':
	if (NNRPReason == NULL)
	    return "1 Already allowing readers";
	p = av[1];
	if (*p && !EQ(p, NNRPReason))
	    return "1 Wrong reason";
	DISPOSE(NNRPReason);
	NNRPReason = NULL;
	break;
    case 'n':
	if (NNRPReason)
	    return "1 Already not allowing readers";
	p = av[1];
	if (*p == '\0')
	    return CCnoreason;
	if (strlen(p) > MAX_REASON_LEN) /* MAX_REASON_LEN is as big as is safe */
	    return CCbigreason;
	NNRPReason = COPY(p);
	break;
    }
    return NULL;
}


/*
**  Re-exec ourselves.
*/
static const char *
CCxexec(char *av[])
{
    char	*inndstart;
    char	*p;
    int		i;

    if (CCargv == NULL)
	return "1 no argv!";
    
    inndstart = COPY(cpcatpath(innconf->pathbin, "inndstart"));
    /* Get the pathname. */
    p = av[0];
    if (*p == '\0' || EQ(p, "innd") || EQ(p, "inndstart"))
	CCargv[0] = inndstart;
    else
	return "1 Bad value";

    JustCleanup();
    syslog(L_NOTICE, "%s execv %s", LogName, CCargv[0]);

    /* Close all fds to protect possible fd leaking accross successive innds. */
    for (i=3; i<30; i++)
        (void)close(i);

    (void)execv(CCargv[0], CCargv);
    syslog(L_FATAL, "%s cant execv %s %m", LogName, CCargv[0]);
    _exit(1);
    /* NOTREACHED */
    return "1 Exit failed";
}

/*
**  Reject remote readers.
*/
static const char *
CCreject(char *av[])
{
    if (RejectReason)
	return "1 Already rejecting";
    if (strlen(av[0]) > MAX_REASON_LEN)	/* MAX_REASON_LEN is as big as is safe */
	return CCbigreason;
    RejectReason = COPY(av[0]);
    return NULL;
}


/*
**  Re-read all in-core data.
*/
static const char *
CCreload(char *av[])
{
    static char	BADSCHEMA[] = "1 Can't read schema";
#if defined(DO_PERL)
    static char BADPERLRELOAD[] = "1 Failed to define filter_art" ;
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    static char BADPYRELOAD[] = "1 Failed to reload filter_innd.py" ;
#endif /* defined(DO_PYTHON) */
    const char *	p;

    p = av[0];
    if (*p == '\0' || EQ(p, "all")) {
	SITEflushall(FALSE);
	HISclose();
	RCreadlist();
	HISsetup();
	ICDwrite();
	ICDsetup(TRUE);
	if (!ARTreadschema())
	    return BADSCHEMA;
#if defined(DO_TCL)
	TCLreadfilter();
#endif /* defined(DO_TCL) */
#if defined(DO_PERL)
        PERLreadfilter ((char *)cpcatpath(innconf->pathfilter,_PATH_PERL_FILTER_INND),"filter_art") ;
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
	syslog(L_NOTICE, "reloading pyfilter");
	(void) PYreadfilter();
	syslog(L_NOTICE, "reloaded pyfilter OK");
#endif /* DO_PYTHON */
	p = "all";
    }
    else if (EQ(p, "active") || EQ(p, "newsfeeds")) {
	SITEflushall(FALSE);
	ICDwrite();
	ICDsetup(TRUE);
    }
    else if (EQ(p, "history")) {
	HISclose();
	HISsetup();
    }
    else if (EQ(p, "incoming.conf"))
	RCreadlist();
    else if (EQ(p, "overview.fmt")) {
	if (!ARTreadschema())
	    return BADSCHEMA;
    }
#if 0 /* we should check almost all innconf parameter, but the code
         is still incomplete for innd, so just commented out */
    else if (EQ(p, "inn.conf")) {
	ReadInnConf();
	if (innconf->pathhost == NULL) {
	    syslog(L_FATAL, "%s No pathhost set", LogName);
	    exit(1);
	}   
	DISPOSE(Path.Data);
	Path.Used = strlen(innconf->pathhost) + 1;
	Path.Data = NEW(char, Path.Used + 1);
	(void)sprintf(Path.Data, "%s!", innconf->pathhost);
	if (Pathalias.Used > 0)
	    DISPOSE(Pathalias.Data);
	if (innconf->pathalias == NULL) {
	    Pathalias.Used = 0;
	    Pathalias.Data = NULL;
	} else {
	    Pathalias.Used = strlen(innconf->pathalias) + 1;
	    Pathalias.Data = NEW(char, Pathalias.Used + 1);
	    (void)sprintf(Pathalias.Data, "%s!", innconf->pathalias);
	}
    }
#endif
#if defined(DO_TCL)
    else if (EQ(p, "filter.tcl")) {
	TCLreadfilter();
    }
#endif /* defined(DO_TCL) */
#if defined(DO_PERL)
    else if (EQ(p, "filter.perl")) {
        if (!PERLreadfilter ((char *)cpcatpath(innconf->pathfilter,_PATH_PERL_FILTER_INND),"filter_art"))
            return BADPERLRELOAD ;
    }
#endif /* defined(DO_PERL) */
#if defined(DO_PYTHON)
    else if (EQ(p, "filter.python")) {
	if (!PYreadfilter())
	    return BADPYRELOAD;
    }
#endif /* defined(DO_PYTHON) */
    else
	return "1 Unknown reload type";

    syslog(L_NOTICE, "%s reload %s %s", LogName, p, av[1]);
    return NULL;
}


/*
**  Renumber the active file.
*/
static const char *
CCrenumber(char *av[])
{
    static char	CANTRENUMBER[] = "1 Failed (see syslog)";
    char	*p;
    NEWSGROUP	*ngp;

    if (Mode != OMrunning)
	return CCnotrunning;
    if (ICDneedsetup)
	return "1 Must first reload newsfeeds";
    p = av[0];
    if (*p) {
	if ((ngp = NGfind(p)) == NULL)
	    return CCnogroup;
	if (!NGrenumber(ngp))
	    return CANTRENUMBER;
    }
    else if (!ICDrenumberactive())
	return CANTRENUMBER;
    return NULL;
}


/*
**  Reserve a lock.
*/
static const char *
CCreserve(char *av[])
{
    char	*p;

    if (Mode != OMrunning)
	return CCnotrunning;
    p = av[0];
    if (*p) {
	/* Trying to make a reservation. */
	if (Reservation)
	    return "1 Already reserved";
	if (strlen(p) > MAX_REASON_LEN) /* MAX_REASON_LEN is as big as is safe */
	    return CCbigreason;
	Reservation = COPY(p);
    }
    else {
	/* Trying to remove a reservation. */
	if (Reservation == NULL)
	    return "1 Not reserved";
	DISPOSE(Reservation);
	Reservation = NULL;
    }
    return NULL;
}


/*
**  Remove a newsgroup.
*/
static const char *
CCrmgroup(char *av[])
{
    NEWSGROUP	*ngp;

    if ((ngp = NGfind(av[0])) == NULL)
	return CCnogroup;

    if (Mode == OMthrottled && ThrottledbyIOError)
	return "1 server throttled";

    /* Update the in-core data. */
    if (!ICDrmgroup(ngp))
	return "1 Failed";
    syslog(L_NOTICE, "%s rmgroup %s", LogName, av[0]);
    return NULL;
}


/*
**  Send a command line to an exploder.
*/
static const char *
CCsend(char *av[])
{
    SITE		*sp;

    if ((sp = SITEfind(av[0])) == NULL)
	return CCnosite;
    if (sp->Type != FTexploder)
	return CCwrongtype;
    SITEwrite(sp, av[1]);
    return NULL;
}


/*
**  Shut down the system.
*/
static const char *
CCshutdown(char *av[])
{
    syslog(L_NOTICE, "%s shutdown %s", LogName, av[0]);
    CleanupAndExit(0, av[0]);
    /* NOTREACHED */
    return "1 Exit failed";
}


/*
**  Send a signal to a site's feed.
*/
static const char *
CCsignal(char *av[])
{
    register SITE	*sp;
    register char	*p;
    int			s;
    int			oerrno;

    /* Parse the signal. */
    p = av[0];
    if (*p == '-')
	p++;
    if (caseEQ(p, "HUP"))
	s = SIGHUP;
    else if (caseEQ(p, "INT"))
	s = SIGINT;
    else if (caseEQ(p, "TERM"))
	s = SIGTERM;
    else if ((s = atoi(p)) <= 0)
	return "1 Invalid signal";

    /* Parse the site. */
    p = av[1];
    if ((sp = SITEfind(p)) == NULL)
	return CCnosite;
    if (sp->Type != FTchannel && sp->Type != FTexploder)
	return CCwrongtype;
    if (sp->Process < 0)
	return "1 Site has no process";

    /* Do it. */
    if (kill(sp->pid, s) < 0) {
	oerrno = errno;
	syslog(L_ERROR, "%s cant kill %ld %d site %s, %m", LogName, 
		(long) sp->pid, s, p);
	(void)sprintf(CCreply.Data, "1 Can't signal process %ld, %s",
		(long) sp->pid, strerror(oerrno));
	return CCreply.Data;
    }

    return NULL;
}


/*
**  Enter throttled mode.
*/
static const char *
CCthrottle(char *av[])
{
    char	*p;

    p = av[0];
    switch (Mode) {
    case OMpaused:
	if (*p && !EQ(p, ModeReason))
	    return "1 Already paused";
	/* FALLTHROUGH */
    case OMrunning:
	return CCblock(OMthrottled, p);
    case OMthrottled:
	return "1 Already throttled";
    }
    return "1 unknown mode";
}

/*
**  Turn on or off performance monitoring
*/
static const char *
CCtimer(char *av[])
{
    int                 value;
    char                *p;
    
    if (EQ(av[0], "off"))
	value = 0;
    else {
	for (p = av[0]; *p; p++) {
	    if (!isdigit(*p))
		return "1 parameter should be a number or 'off'";
	}
	value = atoi(av[0]);
    }
    innconf->timer = value;
    return NULL;
}

/*
**  Turn innd status creation on or off
*/
static const char *
CCstatus(char *av[])
{
    int                 value;
    char                *p;
    
    if (EQ(av[0], "off"))
	value = 0;
    else {
	for (p = av[0]; *p; p++) {
	    if (!isdigit(*p))
		return "1 parameter should be a number or 'off'";
	}
	value = atoi(av[0]);
    }
    innconf->status = value;
    return NULL;
}

/*
**  Add or remove tracing.
*/
static const char *
CCtrace(char *av[])
{
    char	*p;
    bool	Flag;
    const char *	word;
    CHANNEL	*cp;

    /* Parse the flag. */
    p = av[1];
    switch (p[0]) {
    default:			return "1 Bad trace flag";
    case 'y': case 'Y':		Flag = TRUE;	word = "on";	break;
    case 'n': case 'N':		Flag = FALSE;	word = "off";	break;
    }

    /* Parse what's being traced. */
    p = av[0];
    switch (*p) {
    default:
	return "1 Bad trace item";
    case 'i': case 'I':
	Tracing = Flag;
	syslog(L_NOTICE, "%s trace innd %s", LogName, word);
	break;
    case 'n': case 'N':
	NNRPTracing = Flag;
	syslog(L_NOTICE, "%s trace nnrpd %s", LogName, word);
	break;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
	if ((cp = CHANfromdescriptor(atoi(p))) == NULL)
	    return CCnochannel;
	CHANtracing(cp, Flag);
	break;
    }
    return NULL;
}



/*
**  Split up the text into fields and stuff them in argv.  Return the
**  number of elements or -1 on error.
*/
static int
CCargsplit(register char *p, register char *end, register char **argv,
	   register int size)
{
    char		**save;

    for (save = argv, *argv++ = p, size--; p < end; p++)
	if (*p == SC_SEP) {
	    if (--size <= 0)
		return -1;
	    *p = '\0';
	    *argv++ = p + 1;
	}
    *argv = NULL;
    return argv - save;
}


/*
**  Read function.  Read and process the message.
*/
static void
CCreader(CHANNEL *cp)
{
    static char		TOOLONG[] = "0 Reply too long for server to send";
    register CCDISPATCH	*dp;
    register const char *	p;
    register char	*q;
    ICC_MSGLENTYPE	bufflen;
    ICC_PROTOCOLTYPE	protocol ;
#if	defined(HAVE_UNIX_DOMAIN_SOCKETS)
    struct sockaddr_un	client;
#else
    int			written;
#endif	/* defined(HAVE_UNIX_DOMAIN_SOCKETS) */
    int			i;
    char                buff[BIG_BUFFER + 2];
    char                copy[BIG_BUFFER + 2];
    char		*argv[SC_MAXFIELDS + 2];
    int			argc;
    int			len;
    char		*tbuff ;

    if (cp != CCchan) {
	syslog(L_ERROR, "%s internal CCreader wrong channel 0x%p not 0x%p",
	    LogName, cp, CCchan);
	return;
    }

#if defined (HAVE_UNIX_DOMAIN_SOCKETS)
    
    i = RECVorREAD(CCchan->fd, buff, BIG_BUFFER) ;
    if (i < 0) {
	syslog(L_ERROR, "%s cant recv CCreader %m", LogName);
	return;
    } else if (i == 0) {
	syslog(L_ERROR, "%s cant recv CCreader empty", LogName);
	return;
    } else if (i < (int)HEADER_SIZE) {
	syslog(L_ERROR, "%s cant recv CCreader header-length %m", LogName);
	return;
    }

    memcpy (&protocol,buff,sizeof (protocol)) ;
    memcpy (&bufflen,buff + sizeof (protocol),sizeof (bufflen)) ;
    bufflen = ntohs (bufflen) ;
    
    if (i != bufflen) {
	syslog(L_ERROR, "%s cant recv CCreader short-read %m", LogName);
	return;
    }

    i -= HEADER_SIZE ;
    memmove (buff,buff + HEADER_SIZE,i) ;
    buff[i] = '\0';

    if (protocol != ICC_PROTOCOL_1) {
        syslog(L_ERROR, "%s CCreader protocol mismatch", LogName) ;
        return ;
    }

#else  /* defined (HAVE_UNIX_DOMAIN_SOCKETS) */

    i = RECVorREAD(CCchan->fd, buff, HEADER_SIZE) ;
    if (i < 0) {
	syslog(L_ERROR, "%s cant read CCreader header %m", LogName);
	return;
    } else if (i == 0) {
	syslog(L_ERROR, "%s cant read CCreader header empty", LogName);
	return;
    } else if (i != HEADER_SIZE) {
	syslog(L_ERROR, "%s cant read CCreader header-length %m", LogName);
	return;
    }

    memcpy (&protocol,buff,sizeof (protocol)) ;
    memcpy (&bufflen,buff + sizeof (protocol),sizeof (bufflen)) ;
    bufflen = ntohs (bufflen) - HEADER_SIZE ;
    
    i = RECVorREAD(CCchan->fd, buff, bufflen) ;

    if (i < 0) {
	syslog(L_ERROR, "%s cant read CCreader buffer %m", LogName);
	return;
    } else if (i == 0) {
	syslog(L_ERROR, "%s cant read CCreader buffer empty", LogName);
	return;
    } else if (i != bufflen) {
	syslog(L_ERROR, "%s cant read CCreader buffer-length %m", LogName);
	return;
    }

    buff[i] = '\0';

    if (protocol != ICC_PROTOCOL_1) {
        syslog(L_ERROR, "%s CCreader protocol mismatch", LogName) ;
        return ;
    }

#endif /* defined (HAVE_UNIX_DOMAIN_SOCKETS) */
    
    /* Copy to a printable buffer, and log. */
    (void)strcpy(copy, buff);
    for (p = NULL, q = copy; *q; q++)
	if (*q == SC_SEP) {
	    *q = ':';
	    if (p == NULL)
		p = q + 1;
	}
    syslog(L_CC_CMD, "%s", p ? p : copy);

    /* Split up the fields, get the command letter. */
    if ((argc = CCargsplit(buff, &buff[i], argv, SIZEOF(argv))) < 2
     || argc == SIZEOF(argv)) {
	syslog(L_ERROR, "%s bad_fields CCreader", LogName);
	return;
    }
    p = argv[1];
    i = *p;

    /* Dispatch to the command function. */
    for (argc -= 2, dp = CCcommands; dp < ENDOF(CCcommands); dp++)
	if (i == dp->Name) {
	    if (argc != dp->argc)
		p = "1 Wrong number of parameters";
	    else
		p = (*dp->Function)(&argv[2]);
	    break;
	}
    if (dp == ENDOF(CCcommands)) {
	syslog(L_NOTICE, "%s bad_message %c", LogName, i);
	p = "1 Bad command";
    }
    else if (p == NULL)
	p = "0 Ok";

    /* Build the reply address and send the reply. */
    len = strlen(p) + HEADER_SIZE ;
    tbuff = NEW(char,len + 1);
    
    protocol = ICC_PROTOCOL_1 ;
    memcpy (tbuff,&protocol,sizeof (protocol)) ;
    tbuff += sizeof (protocol) ;
    
    bufflen = htons (len) ;
    memcpy (tbuff,&bufflen,sizeof (bufflen)) ;
    tbuff += sizeof (bufflen) ;

    strcpy (tbuff,p) ;
    tbuff -= HEADER_SIZE ;

#if	defined(HAVE_UNIX_DOMAIN_SOCKETS)
    memset(&client, 0, sizeof client);
    client.sun_family = AF_UNIX;
    strcpy(client.sun_path, argv[0]);
    if (sendto(CCwriter, tbuff, len, 0,
	    (struct sockaddr *) &client, SUN_LEN(&client)) < 0) {
	i = errno;
	syslog(i == ENOENT ? L_NOTICE : L_ERROR,
	    "%s cant sendto CCreader bytes %d %m", LogName, len);
	if (i == EMSGSIZE)
	    sendto(CCwriter, TOOLONG, STRLEN(TOOLONG), 0,
		(struct sockaddr *) &client, SUN_LEN(&client));
    }
#else
    if ((i = open(argv[0], O_WRONLY | O_NDELAY)) < 0)
	syslog(L_ERROR, "%s cant open %s %m", LogName, argv[0]);
    else {
	if ((written = write(i, tbuff, len)) != len)
	    if (written < 0)
		syslog(L_ERROR, "%s cant write %s %m", LogName, argv[0]);
	    else
		syslog(L_ERROR, "%s cant write %s", LogName, argv[0]);
	if (close(i) < 0)
	    syslog(L_ERROR, "%s cant close %s %m", LogName, argv[0]);
    }
#endif	/* defined(HAVE_UNIX_DOMAIN_SOCKETS) */
    DISPOSE (tbuff) ;
}


/*
**  Called when a write-in-progress is done on the channel.  Shouldn't happen.
*/
static void
CCwritedone(CHANNEL *unused)
{
    unused = unused;		/* ARGSUSED */
    syslog(L_ERROR, "%s internal CCwritedone", LogName);
}


/*
**  Create the channel.
*/
void
CCsetup(void)
{
    int			i;
#if	defined(HAVE_UNIX_DOMAIN_SOCKETS)
    struct sockaddr_un	server;
#endif	/* defined(HAVE_UNIX_DOMAIN_SOCKETS) */

    if (CCpath == NULL)
	CCpath = COPY(cpcatpath(innconf->pathrun, _PATH_NEWSCONTROL));
    /* Remove old detritus. */
    if (unlink(CCpath) < 0 && errno != ENOENT) {
	syslog(L_FATAL, "%s cant unlink %s %m", LogName, CCpath);
	exit(1);
    }

#if	defined(HAVE_UNIX_DOMAIN_SOCKETS)
    /* Create a socket and name it. */
    if ((i = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
	syslog(L_FATAL, "%s cant socket %s %m", LogName, CCpath);
	exit(1);
    }
    memset(&server, 0, sizeof server);
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, CCpath);
    if (bind(i, (struct sockaddr *) &server, SUN_LEN(&server)) < 0) {
	syslog(L_FATAL, "%s cant bind %s %m", LogName, CCpath);
	exit(1);
    }

    /* Create an unbound socket to reply on. */
    if ((CCwriter = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
	syslog(L_FATAL, "%s cant socket unbound %m", LogName);
	exit(1);
    }
#else
    /* Create a named pipe and open it. */
    if (mkfifo(CCpath, 0666) < 0) {
	syslog(L_FATAL, "%s cant mkfifo %s %m", LogName, CCpath);
	exit(1);
    }
    if ((i = open(CCpath, O_RDWR)) < 0) {
	syslog(L_FATAL, "%s cant open %s %m", LogName, CCpath);
	exit(1);
    }
#endif	/* defined(HAVE_UNIX_DOMAIN_SOCKETS) */

    CCchan = CHANcreate(i, CTcontrol, CSwaiting, CCreader, CCwritedone);
    syslog(L_NOTICE, "%s ccsetup %s", LogName, CHANname(CCchan));
    RCHANadd(CCchan);

    if (CCreply.Size == 0) {
	CCreply.Size = SMBUF;
	CCreply.Data = NEW(char, CCreply.Size);
    }

    /*
     *  Catch SIGUSR1 so that we can recreate the control channel when
     *  needed (i.e. something has deleted our named socket.
     */
#if     defined(SIGUSR1)
    (void)xsignal(SIGUSR1, CCresetup);
#endif  /* defined(SIGUSR1) */
}


/*
**  Cleanly shut down the channel.
*/
void
CCclose(void)
{
    CHANclose(CCchan, CHANname(CCchan));
    CCchan = NULL;
    if (unlink(CCpath) < 0)
	syslog(L_ERROR, "%s cant unlink %s %m", LogName, CCpath);
    DISPOSE(CCpath);
    CCpath = NULL;
    DISPOSE(CCreply.Data);
    CCreply.Data = NULL;
#if	defined(HAVE_UNIX_DOMAIN_SOCKETS)
    if (close(CCwriter) < 0)
	syslog(L_ERROR, "%s cant close unbound %m", LogName);
#endif	/* defined(HAVE_UNIX_DOMAIN_SOCKETS) */
}


/*
**  Restablish the control channel.
*/
static RETSIGTYPE
CCresetup(int unused)
{
#ifndef HAVE_SIGACTION
    xsignal(s, CCresetup);
#else
    unused = unused;		/* ARGSUSED */
#endif
    CCclose();
    CCsetup();
}


/*
 * Read a file containing lines of the form "newsgroup lowmark",
 * and reset the low article number for the specified groups.
 */
static const char *
CClowmark(char *av[])
{
    long lo;
    char *line, *cp;
    const char  *ret = NULL;
    QIOSTATE *qp;
    NEWSGROUP *ngp;

    if (Mode != OMrunning)
	return CCnotrunning;
    if (ICDneedsetup)
	return "1 Must first reload newsfeeds";
    if ((qp = QIOopen(av[0])) == NULL) {
	syslog(L_ERROR, "%s cant open %s %m", LogName, av[0]);
	return "1 Cannot read input file";
    }
    while ((line = QIOread(qp)) != NULL) {
	if (QIOerror(qp))
		break;
	if (QIOtoolong(qp) || (cp = strchr(line, ' ')) == NULL) {
	    ret = "1 Malformed input line";
	    break;
	}
	*cp++ = '\0';
	if ((lo = atol(cp)) == 0 && cp[0] != '0') {
	    ret = "1 Malformed input line (missing low mark)";
	    break;
	}
        if ((ngp = NGfind(line)) == NULL) {
	    /* ret = CCnogroup; break; */
	    continue;
	}
        if (!NGlowmark(ngp, lo)) {
	    ret = "1 Cannot set low mark - see syslog";
	    break;
	}
    }
    if (ret == NULL && QIOerror(qp))
	ret = "1 Error reading input file";
    QIOclose(qp);
    ICDwrite();
    return ret;
}
