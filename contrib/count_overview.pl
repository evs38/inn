#!/usr/local/bin/perl
#
# count_overview.pl: Count the groups in a bunch of Xref records.

while (<>) {

chop;
@xreflist = split(/\t/); # Split apart record.

$_ = $xreflist[$#xreflist];  # Xref is last.

@xreflist = reverse(split(/ /));  # Break part Xref header field body.

pop @xreflist;  # Get rid of Xref header field.
pop @xreflist;

while ($current = pop @xreflist) {
	($current) = split(/:/,$current);  # Get newsgroup name.
	$groups{$current}++;  # Tally.
}

}

# Display accumulated groups and counts.
foreach $current (sort keys %groups) {
	printf "%-50s\t%5d\n", $current, $groups{$current};
}
