#!/usr/bin/perl
# 
#  Copyright Andreas Lamrecht 1998
#  <Andreas.Lamprect@siemens.at>
#
#  Modified by Kjetil T. Homme 1998
#  <kjetilho@ifi.uio.no>

use vars qw($opt_l $opt_h $opt_a);
use Getopt::Long;

# Set common paths (actually just $pathetc ...)
require '/var/news/lib/innshellvars.pl';

my($conffile) = "$inn::pathetc/cycbuff.conf";
my($storagectl) = "$inn::pathetc/storage.ctl";
my($storageconf) = "$inn::pathetc/storage.conf";

sub usage {
    print <<_end_;
Summary tool for CNFS

Usage:
	$0 [-c CLASS] [-l [seconds]]

	If called without args, does a one-time status of all CNFS buffers
	-a:         print the age of the oldest article in the cycbuff
	-c <CLASS>: prints out status of CNFS buffers in class CLASS
	-l seconds: loops like vmstat, default seconds = 600
	-h:         This information
_end_
    exit(1);
}

my(@line, %class, %buff, %stor, $c, @buffers);

my($gr, $cl, $min, $max, @storsort, $oclass, $header_printed);

GetOptions("-a", "-c=s", \$oclass, "-h", "-l:i");

&usage if $opt_h;

my($sleeptime) = (defined($opt_l) && $opt_l > 0) ? $opt_l : 600;

unless (&read_cycbuffconf) {
    print STDERR "Cannot open CycBuff Conffile $conffile ...\n";
    exit (1);
}

unless (&read_storageconf || &read_storagectl) {
    print STDERR "No valid $storageconf or $storagectl.\n";
    exit (1);
}

sub read_cycbuffconf {
    return 0 unless open (CONFFILE, $conffile);

    while(<CONFFILE>) {
	$_ =~ s/^\s*(.*?)\s*$/$1/;
	# \x23 below is #.  Emacs perl-mode gets confused by the "comment"
	next if($_ =~ /^\s*$/ || $_ =~ /^\x23/);
	
	if($_ =~ /^metacycbuff:/) {
	    @line = split(/:/, $_);
	    if($class{$line[1]}) {
		print STDERR "Class $line[1] more than one time in CycBuff Conffile $conffile ...\n";
		return 0;
	    }

	    $class{$line[1]} = $line[2];
	    next;
	}

	if ($_ =~ /^cycbuff/) {
	    @line = split(/:/, $_);
	    if($buff{$line[1]}) {
		print STDERR "Buff $line[1] more than one time in CycBuff Conffile $conffile ...\n";
		return 1;
	    }
	    $buff{$line[1]} = $line[2];
	    next;
	}

	print STDERR "Unknown config line \"$_\" in CycBuff Conffile $conffile ...\n";
    }
    close(CONFFILE);
    return 1;
}

sub read_storagectl {
    return 0 unless open (STOR, $storagectl);

    while (<STOR>) {
	$_ =~ s/^\s*(.*?)\s*$/$1/;
	next if $_ =~ /^\s*$/ || $_ =~ /^#/;

	if ($_ =~ /^cnfs:/) {
	    @line = split(/:/, $_);
	    if($#line != 5) {
		print STDERR "Wrong Storage Control Line \"$_\" in $storagectl ...\n";
		return 0;
	    }
	    
	    if($stor{$line[5]}) {
		print STDERR "CNFS Storage Line \"$_\" more than one time in $storagectl ...\n";
		return 0;
	    }
	    $stor{$line[5]} = join(":", @line[1 .. 4]);
	    push(@storsort, $line[5]);
	}
    }
    close(STOR);
    return 1;
}

sub read_storageconf {
    my $line = 0;
    return 0 unless open (STOR, $storageconf);

    while (<STOR>) {
	++$line;
	next if /^\s*#/;

	# defaults
	%key = ("NEWSGROUPS" => "*",
		"SIZE" => "0,0");
		
	if (/method\s+cnfs\s+\{/) {
	    while (<STOR>) {
		++$line;
		next if /^\s*#/;
		last if /\}/;
		if (/(\w+):\s+(\S+)/i) {
		    $key{uc($1)} = $2;
		}
	    }
	    unless (defined $key{'CLASS'} && defined $key{'OPTIONS'}) {
		print STDERR "storage.conf:$line: ".
			"Missing 'class' or 'options'\n";
		return 0;
	    }

	    $key{'SIZE'} .= ",0" unless $key{'SIZE'} =~ /,/;
	    $key{'SIZE'} =~ s/,/:/;
	    
	    if (defined $stor{$key{'OPTIONS'}}) {
		print STDERR "storage.conf:$line: ".
			"Class $key{'CLASS'} has several criteria\n";
	    } else {
		$stor{$key{'OPTIONS'}} = "$key{'NEWSGROUPS'}:$key{'CLASS'}:" .
			"$key{'SIZE'}:$key{'OPTIONS'}";
		push(@storsort, $key{'OPTIONS'});
	    }
	}
    }
    return 1;
}


#foreach $c (keys(%class)) {
#  print "Class: $c, definition: $class{$c}\n";
#}
#foreach $c (keys(%buff)) {
#  print "Buff: $c, definition: $buff{$c}\n";
#}
# exit(0);

START:

if ($oclass) {
    if ($class{$oclass}) {
	if (!$header_printed) {
	    print STDOUT "Class $oclass";
	    ($gr, $cl, $min, $max) = split(/:/, $stor{$oclass});
	    print STDOUT " for groups matching \"$gr\"";
	    if ($min || $max) {
		print STDOUT ", article size min/max: $min/$max";
	    }
	    print STDOUT "\n";
	    $header_printed = 1;
	}
	
	@buffers = split(/,/, $class{$oclass});
	if (! @buffers) {
	    print STDERR "No buffers in Class $main::ARGV[0] ...\n";
	    next;
	}
	
	foreach $b (@buffers) {
	    if (! $buff{$b} ) {
		print STDERR "No buffer definition for buffer $b ...\n";
		next;
	    }
	    &print_cycbuff_head($buff{$b});
	}
    } else {
	print STDERR "Class $ARGV[1] not found ...\n";
    }
} else { # Print all Classes
    
    foreach $c (@storsort) {
	print STDOUT "Class $c ";
	($gr, $cl, $min, $max) = split(/:/, $stor{$c});
	print STDOUT " for groups matching \"$gr\"";
	if($min || $max) {
	    print STDOUT ", article size min/max: $min/$max";
	}
	print STDOUT "\n";
	@buffers = split(/,/, $class{$c});
	if(! @buffers) {
	    print STDERR "No buffers in Class $c ...\n";
	    next;
	}
	
	foreach $b (@buffers) {
	    if(! $buff{$b} ) {
		print STDERR "No buffer definition for buffer $b ...\n";
		next;
	    }
	    &print_cycbuff_head($buff{$b});
	}
	print STDOUT "\n";
    }
}

if(defined($opt_l)) {
    sleep($sleeptime);
    print STDOUT "$sleeptime seconds later:\n";
    goto START;
}




sub print_cycbuff_head {
    my($buffpath) = $_[0];
    
    my($CNFSMASIZ)=8;
    my($CNFSNASIZ)=16;
    my($CNFSPASIZ)=64;
    my($CNFSLASIZ)=16;
    my($headerlength) = $CNFSMASIZ + $CNFSNASIZ + $CNFSPASIZ + (4 * $CNFSLASIZ);
    
    my($buff, @entries, $e);
    my($magic, $name, $path, $lena, $freea, $updatea, $cyclenuma);
    
    if(! open(BUFF, "< $buffpath") ) {
	print STDERR "Cannot open Cycbuff $buffpath ...\n";
	exit(1);
    }
    
    $buff = "";
    if(! read(BUFF, $buff, $headerlength) ) {
	print STDERR "Cannot read $headerlength bytes from file $buffpath...\n";
	exit(1);
    }
    
    ($magic, $name, $path, $lena, $freea, $updatea, $cyclenuma)  = unpack("a8 a16 a64 a16 a16 a16 a16", $buff);
    
    if(!$magic) {
	print STDERR "Error while unpacking header ...\n";
	exit(1);
    }
    
    my($len) = hex($lena);
    my($free) = hex($freea);
    my($update) = hex($updatea);
    my($cyclenum) = hex($cyclenuma) - 1;
    
    print " Buffer $name, len: ";
    print $len / (1024 * 1024);
    print " Mbytes, used: ";
    printf("%.2f Mbytes", $free / (1024 * 1024));
    printf(" (%4.1f%%) %3d cycles", 100 * $free/$len, $cyclenum);
    
    my ($update_str, $ago_str) = &make_time ($update);
    print "\n  Newest: $update_str, $ago_str ago\n";
    
    if ($opt_a) {
	
	# The 16384 is a fuzz factor.  New articles are actually
	# written a little past free.
	
	$offset = 0;
  do_seek:
	while (1) {
	    $offset += 16384;
	    seek (BUFF, $cyclenum ? $free + $offset : 0, 0);
	    
	    while (<BUFF>) {
		next unless /^message-id:\s+(<.*>)/i;
		
		# We give up if the article is missing in history, or else
		# we stand a high risk of checking the whole cycbuff...
		
		last do_seek unless $time = &lookup_age ($1);
		# Is the article newer than the last update?
		if ($time > $update) {
		    $update = $time;
		    next do_seek;
		}
	  
		($update_str, $ago_str) = &make_time ($time);
		print "  Oldest: $update_str, $ago_str ago\n";
		last do_seek;
	    }
	}
    }
    close(BUFF);
}

sub lookup_age {
    my ($msgid) = @_;

    my $history = `grephistory -l '$msgid' 2>&1`;
    if ($history =~ /\t(\d+)~/) {
	return $1;
    }
    print "  (Missing $msgid)\n";
    return 0;
}

sub make_time {
    my ($t) = @_;
    my (@ret);

    my ($sec,$min,$hour,$mday,$mon,$year) =
	    (localtime($t))[0..5];
    push (@ret, sprintf("%04d-%02d-%02d %2d:%02d:%02d",
			$year + 1900, $mon + 1, $mday, $hour, $min, $sec));
    $t = time - $t;

    $mday = int($t/86400); $t = $t % 86400;
    $hour = int($t/3600);  $t = $t % 3600;
    $min  = int($t/60);    $t = $t % 60;

    push (@ret, sprintf("%4d days, %2d:%02d:%02d",
			$mday, $hour, $min, $t));
    return @ret;
}
