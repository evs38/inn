# $Id$
# Copyright (c)1998 G.J. Andruk
# newgroup.pl - The newgroup control message.
#  Parameters: params sender reply-to token site action[=log] approved
sub control_newgroup {
  my ($artfh, $qgroup);
  
  my @params = split(/\s+/,shift);
  my $sender = shift;
  my $replyto = shift;
  my $token = shift;
  my $site = shift;
  my ($action, $logging) = split(/=/, shift);
  my $approved = shift;
  
  my $groupname = $params[0];
  my $modflag = $params[1];
  
  my $pid = $$;
  my $tempfile = "$inn::tmpdir/newgroup.$pid";
  my ($errmsg, $status, $nc, @component, @oldgroup, $locktry,
      $ngname, $ngdesc, $modcmd);
  
  $modcmd = "y";
  $modcmd = "m" if ($modflag eq "moderated");
  
  # Check the group name.  This is partially derived from C News.
  # Some checks are commented out if I think they're too strict or
  # language-dependent.  Your mileage may vary.
 NEWGROUPSANITY:
  for ($groupname) {
    # whole-name checking
    if (/^$/) {
      $errmsg = "Empty group name";
    } elsif (/\s/) {
      $errmsg = "Whitespace in group name";
    } elsif (/[\`\/:;]/) {
      $errmsg = "unsafe group name";
    } elsif (/^\./ || /\.$/ || /\.\./) {
      $errmsg = "Bad dots in group name";
    }
    #elsif (/^[a-zA-Z0-9].+[a-zA-Z0-9]$/) {
    #    $errmsg = "Group name does not begin/end with alphanumeric";
    #}
    elsif (/^(junk|control)\./) {
      $errmsg = "Group name begins in control. or junk.";
    }
    #elsif (length($groupname) > 128) {
    #    $errmsg = "Group name too long";
    #}
    else {
      $errmsg = "";
    }
    
    last NEWGROUPSANITY if $errmsg;  
    
    # per-component checking
    $nc = (@component = split(/\./));
    for ($i = 0; $i <= $#component; $i++) {
      $c = $component[$i];
      if ($c =~ /^[0-9]+$/) {
	$errmsg = 'all-numeric name component';
      }
      #elsif ($c !~ /^[a-zA-Z0-9]/) {# A-Z caught later
      #    $errmsg = 'name component starts with non-alphanumeric';
      #}
      #elsif ($c !~ /[a-zA-Z]/) {# A-Z caught later
      #    $errmsg = 'name component does not contain letter';
      #}
      elsif ($c eq 'all' || $c eq 'ctl') {
	$errmsg = "`all' || `ctl' used as name component";
      }
      #elsif (length($c) > 30) {
      #    $errmsg = 'name component longer than 30 characters';
      #}
      #elsif ($c =~ /[A-Z]/) {
      # $errmsg = 'uppercase letter(s) in name';
      #}
      elsif ($c =~ /[^a-z0-9+_\-.]/) {
          $errmsg = 'illegal character(s) in name';
      }
      elsif ($c =~ /--|__|\+\+./) { # sigh, c++ etc must be allowed
	$errmsg = 'repeated punctuation in name';
      }
      #elsif ($i+2 <= $#component && ($c eq $component[$i + 1]) &&
      #				($c eq $component[$i + 2])) {
      #    $errmsg = 'repeated component(s) in name';
      #}
    }
    
    #prevent alt.a.b.c.d.e.f.g.w.x.y.z...
    $errmsg = "Too many components" if (! $errmsg) && ($nc > 9);
  }
  
  if ($errmsg) {
    if (!$logging) {
      logmsg('notice', 'skipping newgroup (%s)', $errmsg);
    } else {
      logger($token, $logging, "skipping newgroup ($errmsg)");
    }
  } else {			# Our group name survived.
    # Scan active to see what sort of change we are making.
    open ACTIVE, $inn::active;
    $qgroup = quotemeta ($groupname);
    @oldgroup = grep(/^$qgroup\s/, <ACTIVE>);
    @oldgroup = split(/\s+/, $oldgroup[0]);
    #print "newgroup: $oldgroup[0] $oldgroup[1]","
    #       $oldgroup[2] $oldgroup[3] $modflag\n";
    close ACTIVE;
    if (scalar @oldgroup) {
      if (($oldgroup[3] eq "m") && ($modflag ne "moderated")) {
	$status = "made unmoderated";
      } elsif (($oldgroup[3] ne "m") && ($modflag eq "moderated")) {
	$status = "made moderated";
      } else {
	$status = "no change";
      }
    } elsif (! $approved) {
      $status = "unapproved";
    } else {
      $status = "created";
    }
    
    if ($action eq "mail" && ($status !~ /(no change|unapproved)/)) {
      open(TEMPFILE, ">$tempfile");
      print TEMPFILE ("$sender asks for $groupname\n",
		      "to be $status.\n\n",
		      "If this is acceptable, type:\n",
		      "  $inn::newsbin/ctlinnd newgroup ",
		      "$groupname $modcmd $sender\n\n",
		      "The control message follows:\n\n");
      
      $artfh = open_article($token);
      next if (!defined($artfh));
      *ARTICLE = $artfh;
      
      print TEMPFILE $_ while <ARTICLE>;
      close(ARTICLE);
      close(TEMPFILE);
      logger($tempfile, "mail", "newgroup $groupname $modcmd $sender\n");
      unlink($tempfile);
    } elsif ($action eq "log") {
      if (!$logging) {
	logmsg ('notice', 'skipping newgroup %s %s %s (would be %s)',
		$groupname, $modcmd, $sender, $status);
      } else {
	logger($token, $logging,
	       "skipping newgroup $groupname " .
	       "$modcmd $sender (would be $status)");
      }
    } elsif (($action eq "doit") &&
	     ($status !~ /(no change|unapproved)/)) {
      # Create the group.
      system("$inn::newsbin/ctlinnd", "-s", "newgroup",
	     $groupname, $modcmd, $sender);
      
      # If there's a tag line, update newsgroups too.
      
      $artfh = open_article($token);
      next if (!defined($artfh));
      *ARTICLE = $artfh;
      
    NEWSGROUPS:
      while (<ARTICLE>) {
	chomp;
	if (/^For your newsgroups file:$/) {
	  $ngline = (<ARTICLE>);
	  chomp $ngline;
	  last NEWSGROUPS;
	}
      }
      ($ngname, $ngdesc) = split(/\s+/, $ngline, 2);
      if ($ngdesc) {
	$ngdesc =~ s/\s+\(moderated\)\s*$//i;
	$ngdesc .= " (Moderated)" if $modflag eq "moderated";  
      }
      if (($ngdesc) && ($ngname eq $groupname)) {
	# Get a lock on newsgroups
	$locktry = 0;
      GETNGLOCK:
	while ($locktry < 60) {
	  if (system("$inn::newsbin/shlock", "-p", "$pid",
		     "-f", "$inn::locks/LOCK.newsgroups")) {
	    $locktry++;
	    sleep(2);
	  } else {
	    $locktry = -1;
	    last GETNGLOCK;
	  }
	}
	
	if ($locktry > -1) {
	  logmsg ('err', 'Cannot get lock %s',
		  "$inn::locks/LOCK.newsgroups");
	} else {
	  open(NEWSGROUPS, "<$inn::newsgroups");
	  open(TEMPFILE, ">$tempfile");
	  while (<NEWSGROUPS>) {
	    print TEMPFILE $_ if ! /$qgroup\s/;  
	  }
	  print TEMPFILE ($ngname, "\t", $ngdesc, "\n");
	  
	  open(TEMPFILE, "<$tempfile");
	  open(NEWSGROUPS, ">$inn::newsgroups");
	  print NEWSGROUPS $_ while <TEMPFILE>;  
	  close TEMPFILE;
	  close NEWSGROUPS;
	  unlink "$inn::locks/LOCK.newsgroups";
	  unlink($tempfile);
	}
      }				# update newsgroups
      
      # Now, log what we did.
      if ($logging) {
	$errmsg = "newgroup $groupname ";
	($modflag eq "moderated" ? $errmsg .= "m " :  $errmsg .= "y ");
	$errmsg .=  $status . " " . $sender;
	logger($token, $logging, $errmsg);
      }
    }
  }
}

