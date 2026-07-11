package t::Util;

use strict;
use warnings;
use File::Temp qw(tempfile);

use base qw(Exporter);
our @EXPORT = qw(
	run_prog
);

sub run_prog {
	my $cmd = shift;
	if ($ENV{RUN}) {
		$cmd = "$ENV{RUN} $cmd";
	}
	my ($tempfh, $tempfn) = tempfile(UNLINK => 1);
	my $stderr = `$cmd 2>&1 > $tempfn`;
	my $exit_code = $?;
	print STDERR "run_prog '$cmd' exited with status: $exit_code\n";
	seek($tempfh, 0, 0);
	my $stdout = do { local $/; <$tempfh> };
	close $tempfh;
	return ($stderr, $stdout);
}

1;
